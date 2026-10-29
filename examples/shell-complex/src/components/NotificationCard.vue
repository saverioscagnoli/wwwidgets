<script setup>
import { computed, ref } from "vue";
import { ago } from "../lib/format";
import { now } from "../lib/system";
import { plainText, ww } from "../lib/ww";
import AppIcon from "./AppIcon.vue";
import MIcon from "./MIcon.vue";

let props = defineProps({
  notification: { type: Object, required: true },
  popup: Boolean,
  compact: Boolean,
});

let emit = defineEmits(["dismiss"]);

let expanded = ref(false);
let n = computed(() => props.notification);
let body = computed(() => plainText(n.value.body));
let actions = computed(() => n.value.actions.filter(a => a.key != "default"));
let hasDefault = computed(() => n.value.actions.some(a => a.key == "default"));
let time = computed(() => ago(n.value.time, now.value));

function dismiss() {
  if (props.popup) emit("dismiss");
  else ww.notifications.close(n.value.id);
}

function invoke(key) {
  ww.notifications.invoke(n.value.id, key);
  emit("dismiss");
}

function click(e) {
  if (e.target.closest("button")) return;
  if (hasDefault.value) invoke("default");
  else expanded.value = !expanded.value;
}
</script>

<template>
  <article class="note" :class="{ critical: n.urgency == 2, compact, expanded }" @click="click">
    <div class="icon-wrap">
      <AppIcon :name="n.image || n.icon || n.app.toLowerCase()" :label="n.app" :size="compact ? 36 : 42" />
      <span v-if="n.image && n.icon" class="badge"><AppIcon :name="n.icon" :label="n.app" :size="16" /></span>
    </div>
    <div class="main">
      <div class="top">
        <span class="app ellipsis">{{ n.app || "Notification" }}</span>
        <span class="dot">•</span>
        <span class="time">{{ time }}</span>
      </div>
      <span class="summary ellipsis">{{ n.summary }}</span>
      <p v-if="body" class="body">{{ body }}</p>
      <div v-if="actions.length" class="actions">
        <button v-for="a in actions" :key="a.key" class="btn tonal" @click="invoke(a.key)">{{ a.label }}</button>
      </div>
    </div>
    <button class="icon-btn close" title="Dismiss" @click="dismiss"><MIcon name="close" :size="16" /></button>
  </article>
</template>

<style scoped>
.note {
  position: relative;
  display: flex;
  gap: 12px;
  padding: 12px;
  border-radius: var(--rounding-md);
  background: var(--surface-container);
  cursor: pointer;
  transition: background 0.2s;
}

.note:hover {
  background: var(--surface-container-high);
}

.note.critical {
  background: var(--error-container);
  color: var(--on-error-container);
}

.icon-wrap {
  position: relative;
  flex: none;
  align-self: flex-start;
}

.badge {
  position: absolute;
  right: -4px;
  bottom: -4px;
  padding: 2px;
  border-radius: 8px;
  background: var(--surface-container-highest);
}

.main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.top {
  display: flex;
  gap: 4px;
  align-items: baseline;
  padding-right: 24px;
  font-size: 11px;
  color: var(--on-surface-variant);
}

.critical .top {
  color: inherit;
  opacity: 0.8;
}

.dot,
.time {
  flex: none;
}

.summary {
  font-weight: 600;
  font-size: 14px;
}

.body {
  color: var(--on-surface-variant);
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 3;
  -webkit-box-orient: vertical;
  word-break: break-word;
  line-height: 1.35;
}

.expanded .body {
  -webkit-line-clamp: 20;
}

.critical .body {
  color: inherit;
}

.compact .body {
  -webkit-line-clamp: 2;
}

.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
  margin-top: 8px;
}

.actions .btn {
  flex: 1;
  height: 32px;
  font-size: 12px;
}

.close {
  position: absolute;
  top: 6px;
  right: 6px;
  width: 26px;
  height: 26px;
  opacity: 0;
  transform: scale(0.6);
  transition: opacity 0.2s, transform 0.3s var(--spring), background 0.2s;
}

.note:hover .close {
  opacity: 1;
  transform: none;
}
</style>
