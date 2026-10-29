<script setup>
import { computed, ref } from "vue";
import { clamp, throttle } from "../lib/ww";
import MIcon from "./MIcon.vue";

let props = defineProps({
  value: { type: Number, default: 0 },
  vertical: Boolean,
  icon: String,
  muted: Boolean,
});

let emit = defineEmits(["input", "icon"]);

let track = ref(null);
let dragging = ref(false);
let local = ref(0);
let lockedUntil = 0;

let send = throttle(v => emit("input", v), 40);

let shown = computed(() => {
  let v = dragging.value || performance.now() < lockedUntil ? local.value : props.value;
  return clamp(v, 0, 1);
});

function fromEvent(e) {
  let rect = track.value.getBoundingClientRect();
  return props.vertical ? clamp(1 - (e.clientY - rect.top) / rect.height, 0, 1) : clamp((e.clientX - rect.left) / rect.width, 0, 1);
}

function commit(v) {
  local.value = v;
  send(v);
}

function down(e) {
  if (e.target.closest(".slider-icon")) return;
  dragging.value = true;
  track.value.setPointerCapture(e.pointerId);
  commit(fromEvent(e));
}

function move(e) {
  if (dragging.value) commit(fromEvent(e));
}

function up() {
  if (!dragging.value) return;
  dragging.value = false;
  lockedUntil = performance.now() + 500;
}

function wheel(e) {
  local.value = shown.value;
  lockedUntil = performance.now() + 500;
  commit(clamp(shown.value - Math.sign(e.deltaY) * 0.05, 0, 1));
}
</script>

<template>
  <div
    ref="track"
    class="slider"
    :class="{ vertical, dragging, muted }"
    :style="{ '--v': shown }"
    @pointerdown="down"
    @pointermove="move"
    @pointerup="up"
    @pointercancel="up"
    @wheel.prevent="wheel"
  >
    <div class="fill">
      <button v-if="icon" class="slider-icon" @click="emit('icon')">
        <MIcon :name="icon" :fill="true" :size="18" />
      </button>
    </div>
    <div class="handle"></div>
  </div>
</template>

<style scoped>
.slider {
  --v: 0;
  --thick: 36px;
  position: relative;
  flex: none;
  width: 100%;
  height: var(--thick);
  border-radius: 999px;
  background: var(--secondary-container);
  cursor: pointer;
  touch-action: none;
  overflow: hidden;
}

.slider.vertical {
  flex: none;
  width: var(--thick);
  height: 100%;
}

.fill {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: max(var(--thick), calc(var(--v) * 100%));
  border-radius: 999px;
  background: var(--primary);
  color: var(--on-primary);
  transition: width 0.35s var(--emph), height 0.35s var(--emph), background 0.3s;
}

.vertical .fill {
  top: auto;
  right: 0;
  width: auto;
  height: max(var(--thick), calc(var(--v) * 100%));
}

.dragging .fill {
  transition: background 0.3s;
}

.muted .fill {
  background: var(--outline);
}

.slider-icon {
  position: absolute;
  left: 0;
  top: 0;
  width: var(--thick);
  height: var(--thick);
  display: grid;
  place-items: center;
  border-radius: 999px;
  color: inherit;
  transition: transform 0.25s var(--spring);
}

.vertical .slider-icon {
  top: auto;
  bottom: 0;
}

.slider-icon:active {
  transform: scale(0.85);
}

.handle {
  position: absolute;
  top: 50%;
  left: max(calc(var(--thick) - 10px), calc(var(--v) * 100% - 10px));
  width: 4px;
  height: 16px;
  border-radius: 999px;
  background: var(--on-primary);
  opacity: 0.5;
  transform: translateY(-50%);
  transition: left 0.35s var(--emph), bottom 0.35s var(--emph), height 0.25s var(--spring);
  pointer-events: none;
}

.vertical .handle {
  top: auto;
  left: 50%;
  bottom: max(calc(var(--thick) - 10px), calc(var(--v) * 100% - 10px));
  width: 16px;
  height: 4px;
  transform: translateX(-50%);
}

.dragging .handle {
  transition: none;
}

.slider:hover .handle {
  height: 22px;
}

.vertical:hover .handle {
  height: 4px;
  width: 22px;
}
</style>
