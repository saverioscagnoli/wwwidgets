<script setup>
import { computed, nextTick, ref, watch } from "vue";
import { apps, launch, loadApps, readUsage, score } from "../lib/apps";
import { calculate } from "../lib/calc";
import { clipboard, toplevels } from "../lib/services";
import { SESSION, sessionAction } from "../lib/system";
import { geometry, keys, launcherOpen, setPanel } from "../lib/ui";
import { copyText, run, ww } from "../lib/ww";
import AppIcon from "./AppIcon.vue";
import Drawer from "./Drawer.vue";
import MIcon from "./MIcon.vue";

let VISIBLE = 7;

let query = ref("");
let selected = ref(0);
let input = ref(null);
let listEl = ref(null);
let usage = ref({});

let ACTIONS = [
  { id: "calc", icon: "calculate", name: "Calculator", description: "Do simple math", arg: true },
  { id: "run", icon: "terminal", name: "Run", description: "Run a shell command", arg: true },
  { id: "clip", icon: "content_paste", name: "Clipboard", description: "Search clipboard history", arg: true },
  { id: "dnd", icon: "notifications_off", name: "Do not disturb", description: "Toggle notification popups" },
  ...SESSION.map(s => ({ id: s.id, icon: s.icon, name: s.label, description: `Session: ${s.label.toLowerCase()}`, session: true })),
];

watch(launcherOpen, async open => {
  if (!open) {
    input.value?.blur();
    return;
  }
  query.value = "";
  selected.value = 0;
  usage.value = readUsage();
  loadApps();
  await nextTick();
  setTimeout(() => input.value?.focus(), 40);
});

loadApps();

function close() {
  return setPanel("launcher", false);
}

let mode = computed(() => {
  let q = query.value;
  if (!q.startsWith(">")) return { kind: "apps", text: q.trim().toLowerCase() };
  let [word, ...rest] = q.slice(1).split(" ");
  let action = rest.length ? ACTIONS.find(a => a.id == word.toLowerCase() && a.arg) : null;
  if (action) return { kind: action.id, text: rest.join(" ").trim() };
  return { kind: "actions", text: q.slice(1).trim().toLowerCase() };
});

let results = computed(() => {
  let m = mode.value;

  if (m.kind == "calc") {
    let value = "";
    try {
      value = m.text ? calculate(m.text) : "";
    } catch {
      value = "";
    }
    return [
      {
        key: "calc",
        big: true,
        icon: "calculate",
        name: value || (m.text ? "…" : "Type an expression"),
        description: value ? `${m.text} · Enter to copy` : "e.g. sqrt(2) * pi ^ 2",
        run: () => value && copyText(value).then(close),
      },
    ];
  }

  if (m.kind == "run") {
    return [
      {
        key: "run",
        icon: "terminal",
        name: m.text || "Type a command",
        description: "Runs in the background",
        run: () => m.text && (run(m.text), close()),
      },
    ];
  }

  if (m.kind == "clip") {
    return clipboard.value
      .filter(c => (c.image ? !m.text || "image".includes(m.text) : (c.text ?? "").toLowerCase().includes(m.text.toLowerCase())))
      .slice(0, 40)
      .map(c => ({
        key: `clip-${c.id}`,
        icon: c.image ? "image" : "content_paste",
        image: c.image ? c.url : null,
        name: c.image ? "Image" : (c.text ?? "").replace(/\s+/g, " ").slice(0, 100),
        description: c.image ? "Copied image" : `${(c.text ?? "").length} characters`,
        run: () => ww.clipboard.copy(c.id).then(close),
      }));
  }

  if (m.kind == "actions") {
    return ACTIONS.map(a => ({ a, s: score(a.name, m.text, `${a.id} ${a.description}`) }))
      .filter(r => r.s >= 0)
      .sort((x, y) => y.s - x.s)
      .map(({ a }) => ({
        key: `action-${a.id}`,
        icon: a.icon,
        name: a.name,
        description: a.description,
        hint: a.arg ? "tab" : null,
        complete: a.arg ? `>${a.id} ` : null,
        run: () => {
          if (a.arg) {
            query.value = `>${a.id} `;
            return;
          }
          close();
          if (a.id == "dnd") ww.state.toggle(keys.dnd);
          else setTimeout(() => sessionAction(a.id), 250);
        },
      }));
  }

  let q = m.text;
  let windows = q
    ? toplevels.value
        .filter(t => t.title.toLowerCase().includes(q) || t.appId.toLowerCase().includes(q))
        .slice(0, 3)
        .map(t => ({
          key: `win-${t.id}`,
          app: t.appId,
          label: t.appId || t.title,
          name: t.title,
          description: `Switch to ${t.appId || "window"}`,
          hint: "window",
          run: () => {
            if (t.minimized) ww.toplevels.setMinimized(t.id, false);
            ww.toplevels.activate(t.id);
            close();
          },
        }))
    : [];

  let found = apps.value
    .map(app => ({ app, s: score(app.name, q, `${app.generic} ${app.keywords} ${app.comment} ${app.id}`) }))
    .filter(r => r.s >= 0)
    .sort((x, y) => y.s - x.s || (usage.value[y.app.id] ?? 0) - (usage.value[x.app.id] ?? 0) || x.app.name.localeCompare(y.app.name))
    .slice(0, 50)
    .map(({ app }) => ({
      key: `app-${app.id}`,
      app: app.icon,
      label: app.name,
      name: app.name,
      description: app.comment || app.generic,
      run: () => {
        launch(app);
        close();
      },
    }));

  let list = [...windows, ...found];

  if (q && !list.length) {
    try {
      let value = calculate(q);
      list.push({ key: "calc", big: true, icon: "calculate", name: value, description: `${q} · Enter to copy`, run: () => copyText(value).then(close) });
    } catch {
      list.push({ key: "run", icon: "terminal", name: query.value.trim(), description: "Run as a command", run: () => (run(query.value.trim()), close()) });
    }
  }

  return list;
});

watch(results, list => {
  selected.value = Math.min(selected.value, Math.max(0, list.length - 1));
});

watch(query, () => {
  selected.value = 0;
  if (listEl.value) listEl.value.scrollTop = 0;
});

async function move(delta) {
  let count = results.value.length;
  if (!count) return;
  selected.value = (selected.value + delta + count) % count;
  await nextTick();
  listEl.value?.querySelector(".item.selected")?.scrollIntoView({ block: "nearest" });
}

function key(e) {
  if (e.key == "Escape") {
    e.preventDefault();
    if (query.value) query.value = "";
    else close();
  } else if (e.key == "ArrowDown" || (e.ctrlKey && e.key == "n")) {
    e.preventDefault();
    move(1);
  } else if (e.key == "ArrowUp" || (e.ctrlKey && e.key == "p")) {
    e.preventDefault();
    move(-1);
  } else if (e.key == "Tab") {
    e.preventDefault();
    let current = results.value[selected.value];
    if (current?.complete) query.value = current.complete;
    else if (!query.value) query.value = ">";
    else move(e.shiftKey ? -1 : 1);
  } else if (e.key == "Enter") {
    e.preventDefault();
    results.value[selected.value]?.run();
  }
}

let placeholder = computed(() => (query.value.startsWith(">") ? "Search actions…" : 'Search apps, or type ">" for actions'));

let modeIcon = computed(() => {
  let kind = mode.value.kind;
  if (kind == "calc") return "calculate";
  if (kind == "run") return "terminal";
  if (kind == "clip") return "content_paste";
  if (kind == "actions") return "bolt";
  return "search";
});

let style = {
  bottom: `${geometry.border}px`,
  left: `calc(${geometry.bar}px + (100% - ${geometry.bar + geometry.border}px) / 2)`,
  transform: "translateX(-50%)",
};
</script>

<template>
  <Drawer edge="bottom" :open="!!launcherOpen" :fillets="['bottom-left', 'bottom-right']" :style="style">
    <div class="launcher" @keydown="key">
      <div ref="listEl" class="list" :style="{ maxHeight: `${VISIBLE * 56}px` }">
        <TransitionGroup name="item">
          <button
            v-for="(r, i) in results"
            :key="r.key"
            class="item"
            :class="{ selected: i == selected, big: r.big }"
            @mousemove="selected = i"
            @click="r.run()"
          >
            <img v-if="r.image" :src="r.image" class="thumb" alt="" />
            <AppIcon v-else-if="r.app !== undefined" :name="r.app" :label="r.label" :size="34" />
            <span v-else class="glyph"><MIcon :name="r.icon" :size="22" /></span>
            <span class="text">
              <span class="name ellipsis">{{ r.name }}</span>
              <span v-if="r.description" class="desc ellipsis">{{ r.description }}</span>
            </span>
            <span v-if="r.hint" class="hint">{{ r.hint }}</span>
          </button>
        </TransitionGroup>
        <div v-if="!results.length" class="empty">
          <MIcon name="manage_search" :size="32" />
          <span>No results</span>
        </div>
      </div>
      <label class="search">
        <Transition name="swap" mode="out-in">
          <MIcon :key="modeIcon" :name="modeIcon" :size="22" class="mode" />
        </Transition>
        <input ref="input" v-model="query" :placeholder="placeholder" spellcheck="false" autocomplete="off" />
        <Transition name="fade">
          <button v-if="query" class="icon-btn clear" @click="query = ''"><MIcon name="close" :size="18" /></button>
        </Transition>
      </label>
    </div>
  </Drawer>
</template>

<style scoped>
.launcher {
  width: 620px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 14px 14px 12px;
}

.list {
  position: relative;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  transition: max-height 0.4s var(--emph);
}

.item {
  display: flex;
  align-items: center;
  gap: 14px;
  width: 100%;
  min-height: 56px;
  padding: 8px 14px;
  border-radius: var(--rounding-md);
  text-align: left;
  transition: background 0.2s var(--standard), border-radius 0.3s var(--standard);
}

.item.selected {
  background: var(--surface-container-high);
  border-radius: 999px;
}

.glyph {
  display: grid;
  place-items: center;
  width: 34px;
  height: 34px;
  border-radius: 12px;
  background: var(--secondary-container);
  color: var(--on-secondary-container);
  flex: none;
}

.thumb {
  width: 34px;
  height: 34px;
  border-radius: 10px;
  object-fit: cover;
  flex: none;
}

.text {
  display: flex;
  flex-direction: column;
  min-width: 0;
  flex: 1;
}

.name {
  font-size: 14px;
  font-weight: 500;
}

.desc {
  font-size: 12px;
  color: var(--on-surface-variant);
}

.big .name {
  font-size: 26px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.hint {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 999px;
  background: var(--surface-container-highest);
  color: var(--on-surface-variant);
}

.item-enter-active {
  transition: opacity 0.3s var(--standard), transform 0.4s var(--emph);
}

.item-enter-from {
  opacity: 0;
  transform: translateY(10px);
}

.item-leave-active {
  display: none;
}

.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  padding: 24px 0;
  color: var(--outline);
}

.search {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 52px;
  padding: 0 8px 0 18px;
  border-radius: 999px;
  background: var(--surface-container);
}

.mode {
  color: var(--primary);
}

input {
  flex: 1;
  font-size: 15px;
}

input::placeholder {
  color: var(--outline);
}

.clear {
  width: 32px;
  height: 32px;
}
</style>
