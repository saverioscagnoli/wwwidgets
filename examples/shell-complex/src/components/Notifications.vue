<script setup>
import { computed, reactive, watch } from "vue";
import { notifications } from "../lib/services";
import { dashboardOpen, dnd, geometry, sessionOpen, ui } from "../lib/ui";
import Drawer from "./Drawer.vue";
import NotificationCard from "./NotificationCard.vue";

let MAX = 4;
let dismissed = reactive(new Set());
let seen = new Map();

watch(notifications, list => {
  let ids = new Set(list.map(n => n.id));
  for (let id of [...dismissed]) if (!ids.has(id)) dismissed.delete(id);
  for (let n of list) {
    if (seen.has(n.id) && seen.get(n.id) != n.time) dismissed.delete(n.id);
    seen.set(n.id, n.time);
  }
});

watch(
  () => ui.popout == "notifications",
  open => {
    if (open) for (let n of notifications.value) dismissed.add(n.id);
  }
);

let visible = computed(() => {
  if (dnd.value || dashboardOpen.value || sessionOpen.value) return [];
  return notifications.value
    .filter(n => !n.expired && !dismissed.has(n.id))
    .sort((a, b) => b.time - a.time)
    .slice(0, MAX);
});

let style = { right: `${geometry.border}px`, top: `${geometry.border}px` };
</script>

<template>
  <Drawer edge="right" :open="visible.length > 0" :fillets="['top-left', 'right-below']" corners="0 0 0 var(--r)" :style="style">
    <TransitionGroup name="toast" tag="div" class="stack">
      <NotificationCard v-for="n in visible" :key="n.id" :notification="n" popup @dismiss="dismissed.add(n.id)" />
    </TransitionGroup>
  </Drawer>
</template>

<style scoped>
.stack {
  width: 380px;
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 4px 4px 12px 12px;
}

.toast-enter-active {
  transition: transform 0.5s var(--emph), opacity 0.3s;
}

.toast-leave-active {
  transition: transform 0.3s var(--emph-accel), opacity 0.25s;
}

.toast-enter-from,
.toast-leave-to {
  transform: translateX(110%);
  opacity: 0;
}

.toast-move {
  transition: transform 0.45s var(--emph);
}
</style>
