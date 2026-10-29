<script setup>
import { computed, onBeforeUnmount, onMounted, ref } from "vue";

let props = defineProps({
  edge: { type: String, default: "top" },
  open: Boolean,
  fillets: { type: Array, default: () => [] },
  corners: String,
});

let emit = defineEmits(["size", "enter", "leave"]);

let content = ref(null);
let width = ref(0);
let height = ref(0);
let observer = null;

onMounted(() => {
  observer = new ResizeObserver(() => {
    width.value = content.value.offsetWidth;
    height.value = content.value.offsetHeight;
    emit("size", { width: width.value, height: height.value });
  });
  observer.observe(content.value);
});

onBeforeUnmount(() => observer?.disconnect());

let vertical = computed(() => props.edge == "top" || props.edge == "bottom");

let style = computed(() => {
  if (vertical.value) return { width: `${width.value}px`, height: props.open ? `${height.value}px` : "0px" };
  return { width: props.open ? `${width.value}px` : "0px", height: `${height.value}px` };
});

let radius = computed(() => {
  if (props.corners) return props.corners;
  return {
    top: "0 0 var(--r) var(--r)",
    bottom: "var(--r) var(--r) 0 0",
    left: "0 var(--r) var(--r) 0",
    right: "var(--r) 0 0 var(--r)",
  }[props.edge];
});
</script>

<template>
  <div
    class="drawer"
    :class="[edge, { open }]"
    :style="style"
    :data-region="open ? '' : null"
    @mouseenter="emit('enter')"
    @mouseleave="emit('leave')"
  >
    <span v-for="f in fillets" :key="f" class="fillet" :class="f"></span>
    <div class="clip" :style="{ borderRadius: radius }">
      <div ref="content" class="content" :inert="!open">
        <slot />
      </div>
    </div>
  </div>
</template>

<style scoped>
.drawer {
  position: absolute;
  transition: width 0.5s var(--emph), height 0.5s var(--emph);
}

.drawer:not(.open) {
  transition: width 0.35s var(--emph-accel), height 0.35s var(--emph-accel);
}

.clip {
  position: absolute;
  inset: 0;
  overflow: hidden;
  background: var(--frame);
}

.content {
  position: absolute;
  width: max-content;
  opacity: 0;
  transition: opacity 0.2s var(--emph-accel), transform 0.35s var(--emph-accel);
}

.drawer.open .content {
  opacity: 1;
  transition: opacity 0.35s 0.06s var(--standard), transform 0.5s var(--emph);
}

.top .content {
  top: 0;
  left: 50%;
  transform: translate(-50%, -24px);
}

.bottom .content {
  bottom: 0;
  left: 50%;
  transform: translate(-50%, 24px);
}

.left .content {
  left: 0;
  top: 50%;
  transform: translate(-24px, -50%);
}

.right .content {
  right: 0;
  top: 50%;
  transform: translate(24px, -50%);
}

.drawer.open.top .content,
.drawer.open.bottom .content {
  transform: translate(-50%, 0);
}

.drawer.open.left .content,
.drawer.open.right .content {
  transform: translate(0, -50%);
}

.fillet {
  position: absolute;
  width: var(--r);
  height: var(--r);
  pointer-events: none;
  opacity: 0;
  transform: scale(0);
  transition: transform 0.3s var(--emph-accel), opacity 0.2s;
}

.open .fillet {
  opacity: 1;
  transform: none;
  transition: transform 0.45s var(--emph), opacity 0.2s;
}

.fillet.top-left {
  left: calc(-1 * var(--r));
  top: 0;
  background: radial-gradient(circle at 0 100%, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 100% 0;
}

.fillet.top-right {
  left: 100%;
  top: 0;
  background: radial-gradient(circle at 100% 100%, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 0 0;
}

.fillet.bottom-left {
  left: calc(-1 * var(--r));
  bottom: 0;
  background: radial-gradient(circle at 0 0, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 100% 100%;
}

.fillet.bottom-right {
  left: 100%;
  bottom: 0;
  background: radial-gradient(circle at 100% 0, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 0 100%;
}

.fillet.right-above {
  right: 0;
  top: calc(-1 * var(--r));
  background: radial-gradient(circle at 0 0, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 100% 100%;
}

.fillet.right-below {
  right: 0;
  top: 100%;
  background: radial-gradient(circle at 0 100%, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 100% 0;
}

.fillet.left-above {
  left: 0;
  top: calc(-1 * var(--r));
  background: radial-gradient(circle at 100% 0, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 0 100%;
}

.fillet.left-below {
  left: 0;
  top: 100%;
  background: radial-gradient(circle at 100% 100%, transparent calc(var(--r) - 0.5px), var(--frame) var(--r));
  transform-origin: 0 0;
}
</style>
