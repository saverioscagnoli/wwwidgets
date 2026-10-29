<script setup>
import { computed } from "vue";
import { notifications } from "../../lib/services";
import { dnd, keys } from "../../lib/ui";
import { quiet, ww } from "../../lib/ww";
import MIcon from "../MIcon.vue";
import NotificationCard from "../NotificationCard.vue";

defineProps({ data: null, active: Boolean });

let list = computed(() => [...notifications.value].sort((a, b) => b.time - a.time));

function clear() {
  for (let n of list.value) quiet(ww.notifications.close(n.id));
}
</script>

<template>
  <div class="notifs">
    <div class="head">
      <span class="title">Notifications</span>
      <button class="icon-btn" :class="{ tonal: dnd }" title="Do not disturb" @click="ww.state.toggle(keys.dnd)">
        <MIcon :name="dnd ? 'notifications_off' : 'notifications_active'" :fill="!!dnd" :size="18" />
      </button>
      <button class="icon-btn" title="Clear all" :disabled="!list.length" @click="clear">
        <MIcon name="clear_all" :size="20" />
      </button>
    </div>
    <TransitionGroup name="note" tag="div" class="list">
      <NotificationCard v-for="n in list" :key="n.id" :notification="n" compact />
    </TransitionGroup>
    <div v-if="!list.length" class="empty">
      <MIcon name="notifications_paused" :size="40" />
      <span class="label">You're all caught up</span>
    </div>
  </div>
</template>

<style scoped>
.notifs {
  width: 340px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.head {
  display: flex;
  align-items: center;
  gap: 2px;
  padding-left: 4px;
}

.head .title {
  flex: 1;
}

.list {
  display: flex;
  flex-direction: column;
  gap: 6px;
  max-height: 460px;
  overflow-y: auto;
}

.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 24px 0;
  color: var(--outline);
}

.note-enter-active,
.note-leave-active {
  transition: transform 0.4s var(--emph), opacity 0.3s;
}

.note-enter-from,
.note-leave-to {
  transform: translateX(-30px);
  opacity: 0;
}

.note-move {
  transition: transform 0.4s var(--emph);
}
</style>
