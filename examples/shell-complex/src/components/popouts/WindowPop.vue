<script setup>
import { computed } from "vue";
import { activeWindow, toplevels } from "../../lib/services";
import { ww } from "../../lib/ww";
import AppIcon from "../AppIcon.vue";
import MIcon from "../MIcon.vue";

defineProps({ data: null, active: Boolean });

let others = computed(() => toplevels.value.filter(t => !t.activated).slice(0, 8));

function focus(t) {
  if (t.minimized) ww.toplevels.setMinimized(t.id, false);
  ww.toplevels.activate(t.id);
}
</script>

<template>
  <div class="window">
    <div v-if="activeWindow" class="current">
      <AppIcon :name="activeWindow.appId" :label="activeWindow.appId || activeWindow.title" :size="48" />
      <div class="info">
        <span class="title two-lines">{{ activeWindow.title }}</span>
        <span class="label">{{ activeWindow.appId }}</span>
      </div>
    </div>
    <div v-else class="current">
      <div class="desk"><MIcon name="desktop_windows" :size="26" /></div>
      <div class="info">
        <span class="title">Desktop</span>
        <span class="label">No focused window</span>
      </div>
    </div>

    <div v-if="activeWindow" class="actions">
      <button class="btn tonal" @click="ww.toplevels.setMinimized(activeWindow.id, true)"><MIcon name="minimize" :size="18" />Minimize</button>
      <button class="btn tonal danger" @click="ww.toplevels.close(activeWindow.id)"><MIcon name="close" :size="18" />Close</button>
    </div>

    <div v-if="others.length" class="others">
      <span class="label">Other windows</span>
      <button v-for="t in others" :key="t.id" class="list-item" @click="focus(t)">
        <AppIcon :name="t.appId" :label="t.appId || t.title" :size="22" />
        <span class="ellipsis">{{ t.title || t.appId }}</span>
        <MIcon v-if="t.minimized" name="minimize" :size="16" class="dim" />
      </button>
    </div>
  </div>
</template>

<style scoped>
.window {
  width: 300px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.current {
  display: flex;
  gap: 12px;
  align-items: center;
}

.desk {
  display: grid;
  place-items: center;
  width: 48px;
  height: 48px;
  border-radius: 14px;
  background: var(--surface-container-high);
}

.info {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.two-lines {
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  word-break: break-word;
}

.actions {
  display: flex;
  gap: 8px;
}

.actions .btn {
  flex: 1;
}

.danger:hover {
  background: var(--error-container);
  color: var(--on-error-container);
}

.others {
  display: flex;
  flex-direction: column;
}

.others .label {
  padding: 0 4px 4px;
}

.dim {
  margin-left: auto;
  color: var(--outline);
}
</style>
