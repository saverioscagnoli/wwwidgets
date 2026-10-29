<script setup>
import { computed, ref } from "vue";
import MIcon from "./MIcon.vue";

let today = ref(new Date());
let view = ref(new Date(today.value.getFullYear(), today.value.getMonth(), 1));
let direction = ref("next");

let weekdays = Array.from({ length: 7 }, (_, i) => new Date(2024, 0, 1 + i).toLocaleDateString(undefined, { weekday: "narrow" }));

let title = computed(() => view.value.toLocaleDateString(undefined, { month: "long", year: "numeric" }));

let days = computed(() => {
  let y = view.value.getFullYear();
  let m = view.value.getMonth();
  let offset = (new Date(y, m, 1).getDay() + 6) % 7;
  return Array.from({ length: 42 }, (_, i) => {
    let date = new Date(y, m, 1 - offset + i);
    return {
      key: date.toDateString(),
      day: date.getDate(),
      other: date.getMonth() != m,
      today: date.toDateString() == today.value.toDateString(),
      weekend: date.getDay() == 0 || date.getDay() == 6,
    };
  });
});

function shift(delta) {
  direction.value = delta > 0 ? "next" : "prev";
  view.value = new Date(view.value.getFullYear(), view.value.getMonth() + delta, 1);
}

function reset() {
  today.value = new Date();
  direction.value = view.value < today.value ? "next" : "prev";
  view.value = new Date(today.value.getFullYear(), today.value.getMonth(), 1);
}

defineExpose({ reset });
</script>

<template>
  <div class="calendar" @wheel.prevent="shift(Math.sign($event.deltaY))">
    <header>
      <button class="icon-btn" @click="shift(-1)"><MIcon name="chevron_left" /></button>
      <button class="month" @click="reset">{{ title }}</button>
      <button class="icon-btn" @click="shift(1)"><MIcon name="chevron_right" /></button>
    </header>
    <div class="weekdays">
      <span v-for="(d, i) in weekdays" :key="i" :class="{ weekend: i >= 5 }">{{ d }}</span>
    </div>
    <div class="viewport">
      <Transition :name="direction">
        <div :key="title" class="grid">
          <span v-for="d in days" :key="d.key" class="day" :class="{ other: d.other, today: d.today, weekend: d.weekend }">{{ d.day }}</span>
        </div>
      </Transition>
    </div>
  </div>
</template>

<style scoped>
.calendar {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.month {
  font-weight: 600;
  font-size: 14px;
  padding: 6px 12px;
  border-radius: 999px;
  transition: background 0.2s;
}

.month:hover {
  background: var(--state-hover);
}

.weekdays,
.grid {
  display: grid;
  grid-template-columns: repeat(7, 32px);
  justify-content: space-between;
  text-align: center;
}

.weekdays span {
  padding: 4px 0;
  font-size: 11px;
  font-weight: 600;
  color: var(--on-surface-variant);
}

.weekdays .weekend {
  color: var(--tertiary);
}

.viewport {
  position: relative;
  overflow: hidden;
  height: 192px;
}

.grid {
  position: absolute;
  inset: 0;
  row-gap: 0;
}

.day {
  display: grid;
  place-items: center;
  height: 32px;
  border-radius: 999px;
  font-variant-numeric: tabular-nums;
  transition: background 0.2s;
}

.day:hover {
  background: var(--state-hover);
}

.day.weekend {
  color: var(--tertiary);
}

.day.other {
  color: var(--outline);
}

.day.today {
  background: var(--primary);
  color: var(--on-primary);
  font-weight: 600;
}

.next-enter-active,
.next-leave-active,
.prev-enter-active,
.prev-leave-active {
  transition: transform 0.45s var(--emph), opacity 0.3s;
}

.next-enter-from,
.prev-leave-to {
  transform: translateX(40%);
  opacity: 0;
}

.next-leave-to,
.prev-enter-from {
  transform: translateX(-40%);
  opacity: 0;
}
</style>
