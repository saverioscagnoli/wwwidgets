<script setup>
defineProps({
  name: { type: String, required: true },
  fill: Boolean,
  size: { type: Number, default: 20 },
});
</script>

<template>
  <span class="ms" :class="{ fill }" :style="{ fontSize: `${size}px` }" aria-hidden="true">{{ name }}</span>
</template>
