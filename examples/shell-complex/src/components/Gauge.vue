<script setup>
import { computed } from "vue";
import { clamp } from "../lib/ww";
import MIcon from "./MIcon.vue";

let props = defineProps({
  value: { type: Number, default: 0 },
  icon: String,
  label: String,
  text: String,
  size: { type: Number, default: 120 },
  color: { type: String, default: "var(--primary)" },
});

let stroke = 10;
let radius = computed(() => (props.size - stroke) / 2);
let circumference = computed(() => 2 * Math.PI * radius.value);
let arc = computed(() => circumference.value * 0.75);
let offset = computed(() => arc.value * (1 - clamp(props.value, 0, 1)));
</script>

<template>
  <div class="gauge" :style="{ width: `${size}px` }">
    <svg :width="size" :height="size" :viewBox="`0 0 ${size} ${size}`">
      <circle
        class="track"
        :cx="size / 2"
        :cy="size / 2"
        :r="radius"
        :stroke-width="stroke"
        :stroke-dasharray="`${arc} ${circumference}`"
      />
      <circle
        class="bar"
        :cx="size / 2"
        :cy="size / 2"
        :r="radius"
        :stroke-width="stroke"
        :stroke-dasharray="`${arc} ${circumference}`"
        :stroke-dashoffset="offset"
        :style="{ stroke: color }"
      />
    </svg>
    <div class="center">
      <MIcon v-if="icon" :name="icon" :size="22" :style="{ color }" />
      <span class="text">{{ text }}</span>
    </div>
    <span class="label">{{ label }}</span>
  </div>
</template>

<style scoped>
.gauge {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
}

svg {
  transform: rotate(135deg);
}

circle {
  fill: none;
  stroke-linecap: round;
}

.track {
  stroke: var(--surface-container-highest);
}

.bar {
  transition: stroke-dashoffset 0.8s var(--emph);
}

.center {
  position: absolute;
  top: 0;
  left: 0;
  right: 0;
  aspect-ratio: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
}

.text {
  font-size: 18px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.label {
  margin-top: -14px;
}
</style>
