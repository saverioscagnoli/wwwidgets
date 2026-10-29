<script setup>
import { computed, ref } from "vue";
import { trayItems } from "../../lib/services";
import { ui } from "../../lib/ui";
import { quiet, ww } from "../../lib/ww";
import AppIcon from "../AppIcon.vue";
import MIcon from "../MIcon.vue";

let props = defineProps({ data: null, active: Boolean });

let item = computed(() => trayItems.value.find(i => i.id == props.data) ?? null);
let entries = ref([]);
let loading = ref(true);

function flatten(items, depth) {
  let out = [];
  for (let m of items) {
    if (!m.visible) continue;
    out.push({ ...m, depth });
    if (m.children?.length) out.push(...flatten(m.children, depth + 1));
  }
  return out;
}

quiet(ww.tray.menu(props.data)).then(menu => {
  entries.value = flatten(menu ?? [], 0);
  loading.value = false;
});

function click(m) {
  if (!m.enabled || m.children?.length) return;
  ww.tray.menuClick(props.data, m.id);
  ui.popout = null;
}

function label(text) {
  return text.replace(/_(?!_)/g, "");
}
</script>

<template>
  <div class="tray">
    <div class="head">
      <AppIcon :name="item?.icon" :label="item?.title" :size="28" />
      <div class="info">
        <span class="title ellipsis">{{ item?.title || item?.appId || "Tray" }}</span>
        <span v-if="item?.tooltip" class="label ellipsis">{{ item.tooltip }}</span>
      </div>
    </div>
    <div class="menu">
      <template v-for="m in entries" :key="m.id">
        <hr v-if="m.type == 'separator'" />
        <button
          v-else
          class="list-item"
          :class="{ disabled: !m.enabled, parent: m.children?.length }"
          :style="{ paddingLeft: `${12 + m.depth * 16}px` }"
          @click="click(m)"
        >
          <MIcon v-if="m.toggleType == 'checkmark'" :name="m.toggleState == 1 ? 'check_box' : 'check_box_outline_blank'" :size="18" />
          <MIcon v-else-if="m.toggleType == 'radio'" :name="m.toggleState == 1 ? 'radio_button_checked' : 'radio_button_unchecked'" :size="18" />
          <span class="ellipsis">{{ label(m.label) }}</span>
        </button>
      </template>
      <span v-if="!loading && !entries.length" class="label">No menu</span>
    </div>
  </div>
</template>

<style scoped>
.tray {
  width: 260px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.head {
  display: flex;
  gap: 10px;
  align-items: center;
  padding: 0 4px;
}

.info {
  min-width: 0;
  display: flex;
  flex-direction: column;
}

.menu {
  display: flex;
  flex-direction: column;
  max-height: 420px;
  overflow-y: auto;
}

.list-item {
  min-height: 36px;
}

.disabled {
  opacity: 0.4;
  pointer-events: none;
}

.parent {
  font-weight: 600;
  pointer-events: none;
}

hr {
  border: none;
  height: 1px;
  margin: 4px 8px;
  background: var(--outline-variant);
}
</style>
