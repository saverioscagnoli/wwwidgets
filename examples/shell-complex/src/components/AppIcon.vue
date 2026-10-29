<script setup>
import { computed, ref, watch } from "vue";
import { iconSrc } from "../lib/ww";

let props = defineProps({
  name: String,
  label: String,
  size: { type: Number, default: 24 },
});

let failed = ref(false);
let src = computed(() => iconSrc(props.name));

watch(src, () => (failed.value = false));

let letter = computed(() => {
  let text = props.label || props.name || "?";
  return text.replace(/^.*[./]/, "").trim().charAt(0).toUpperCase() || "?";
});

let hue = computed(() => [...(props.label || props.name || "")].reduce((a, c) => a + c.charCodeAt(0), 0) % 360);
</script>

<template>
  <span class="app-icon" :style="{ width: `${size}px`, height: `${size}px` }">
    <img v-if="src && !failed" :src="src" alt="" draggable="false" @error="failed = true" />
    <span v-else class="letter" :style="{ '--hue': hue, fontSize: `${size * 0.5}px` }">{{ letter }}</span>
  </span>
</template>

<style scoped>
.app-icon {
  display: inline-grid;
  place-items: center;
  flex: none;
  border-radius: 28%;
  overflow: hidden;
}

img {
  width: 100%;
  height: 100%;
  object-fit: contain;
}

.letter {
  display: grid;
  place-items: center;
  width: 100%;
  height: 100%;
  background: hsl(var(--hue) 35% 32%);
  color: hsl(var(--hue) 70% 88%);
  font-weight: 600;
}
</style>
