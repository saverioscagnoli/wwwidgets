<script setup>
import { nextTick, ref, watch } from "vue";
import { SESSION, sessionAction, user } from "../lib/system";
import { geometry, sessionOpen, setPanel } from "../lib/ui";
import Drawer from "./Drawer.vue";
import MIcon from "./MIcon.vue";

let buttons = ref([]);
let focused = ref(SESSION.length - 1);

watch(sessionOpen, async open => {
  if (!open) return;
  focused.value = SESSION.length - 1;
  await nextTick();
  setTimeout(() => buttons.value[focused.value]?.focus(), 60);
});

function choose(id) {
  setPanel("session", false);
  setTimeout(() => sessionAction(id), 250);
}

function key(e) {
  if (!sessionOpen.value) return;
  if (e.key == "ArrowDown" || e.key == "ArrowUp" || e.key == "Tab") {
    e.preventDefault();
    let delta = e.key == "ArrowUp" || (e.key == "Tab" && e.shiftKey) ? -1 : 1;
    focused.value = (focused.value + delta + SESSION.length) % SESSION.length;
    buttons.value[focused.value]?.focus();
  }
}

let style = { right: `${geometry.border}px`, top: "50%", transform: "translateY(-50%)" };
</script>

<template>
  <Drawer edge="right" :open="!!sessionOpen" :fillets="['right-above', 'right-below']" :style="style">
    <div class="session" @keydown="key">
      <span class="greet label">Goodbye{{ user.name ? `, ${user.name.split(" ")[0]}` : "" }}</span>
      <button
        v-for="(a, i) in SESSION"
        :key="a.id"
        :ref="el => (buttons[i] = el)"
        class="action"
        :class="[a.id, { focused: focused == i }]"
        @mouseenter="focused = i"
        @focus="focused = i"
        @click="choose(a.id)"
      >
        <MIcon :name="a.icon" :size="28" :fill="focused == i" />
        <span class="name">{{ a.label }}</span>
      </button>
    </div>
  </Drawer>
</template>

<style scoped>
.session {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 8px;
  padding: 16px 14px 16px 16px;
}

.greet {
  text-align: center;
  padding-bottom: 4px;
}

.action {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  width: 92px;
  height: 80px;
  border-radius: var(--rounding);
  background: var(--surface-container);
  color: var(--on-surface-variant);
  outline: none;
  transition: background 0.3s var(--standard), color 0.3s, border-radius 0.4s var(--emph), transform 0.3s var(--spring);
}

.action .name {
  font-size: 11px;
  font-weight: 500;
}

.action.focused {
  background: var(--secondary-container);
  color: var(--on-secondary-container);
  border-radius: 999px;
}

.action.poweroff.focused {
  background: var(--error-container);
  color: var(--on-error-container);
}

.action:active {
  transform: scale(0.94);
}
</style>
