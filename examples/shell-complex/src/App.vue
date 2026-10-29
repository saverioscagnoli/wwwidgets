<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from "vue";
import { cancelClose, dashboardOpen, geometry, later, launcherOpen, sessionOpen, setPanel, ui } from "./lib/ui";
import { loadUser } from "./lib/system";
import { ww } from "./lib/ww";
import Bar from "./components/Bar.vue";
import Dashboard from "./components/Dashboard.vue";
import Launcher from "./components/Launcher.vue";
import Notifications from "./components/Notifications.vue";
import Osd from "./components/Osd.vue";
import Popout from "./components/Popout.vue";
import Session from "./components/Session.vue";

let osd = ref(null);
let dashboard = ref(null);

let modal = computed(() => !!launcherOpen.value || !!sessionOpen.value);

watch(modal, value => ww.window.setKeyboard(value ? "exclusive" : "none"), { immediate: true });

watch(modal, value => value && (ui.popout = null));

let vars = {
  "--bar": `${geometry.bar}px`,
  "--border": `${geometry.border}px`,
  "--r": `${geometry.radius}px`,
};

function closeModal() {
  if (launcherOpen.value) setPanel("launcher", false);
  if (sessionOpen.value) setPanel("session", false);
}

function key(e) {
  if (e.key == "Escape" && modal.value) {
    e.preventDefault();
    closeModal();
  }
}

function topEnter() {
  if (modal.value) return;
  later("dashboard", () => setPanel("dashboard", true), 120);
}

function topLeave() {
  if (dashboardOpen.value) dashboard.value?.leave();
  else cancelClose("dashboard");
}

let lastRegion = "";
let until = 0;
let frame = 0;
let observer = null;

function measure() {
  let rects = [];
  if (modal.value) rects.push({ x: 0, y: 0, width: window.innerWidth, height: window.innerHeight });
  else {
    for (let el of document.querySelectorAll("[data-region]")) {
      let r = el.getBoundingClientRect();
      if (r.width < 1 || r.height < 1) continue;
      rects.push({ x: Math.floor(r.x), y: Math.floor(r.y), width: Math.ceil(r.width), height: Math.ceil(r.height) });
    }
  }
  let json = JSON.stringify(rects);
  if (json == lastRegion) return;
  lastRegion = json;
  ww.window.setInputRegion(rects);
}

function loop() {
  measure();
  frame = performance.now() < until ? requestAnimationFrame(loop) : 0;
}

function kick() {
  until = performance.now() + 700;
  if (!frame) frame = requestAnimationFrame(loop);
}

watch(modal, kick);

onMounted(() => {
  loadUser();
  observer = new MutationObserver(kick);
  observer.observe(document.body, { subtree: true, childList: true, attributes: true, attributeFilter: ["class", "style", "data-region"] });
  window.addEventListener("resize", kick);
  window.addEventListener("keydown", key);
  kick();
});

onBeforeUnmount(() => {
  observer?.disconnect();
  window.removeEventListener("resize", kick);
  window.removeEventListener("keydown", key);
});
</script>

<template>
  <div class="shell" :style="vars">
    <div class="frame"></div>

    <Transition name="backdrop">
      <div v-if="modal" class="backdrop" :class="{ dim: sessionOpen }" @click="closeModal"></div>
    </Transition>

    <div class="hot top" data-region @mouseenter="topEnter" @mouseleave="topLeave"></div>
    <div class="hot right" data-region @mouseenter="osd?.enter()" @mouseleave="osd?.leave()"></div>

    <Bar />
    <Popout :blocked="modal" />
    <Dashboard ref="dashboard" />
    <Notifications />
    <Osd ref="osd" />
    <Session />
    <Launcher />
  </div>
</template>

<style scoped>
.shell {
  position: fixed;
  inset: 0;
}

.frame {
  position: absolute;
  inset: var(--border) var(--border) var(--border) var(--bar);
  border-radius: var(--r);
  box-shadow: 0 0 0 200vmax var(--frame);
  pointer-events: none;
  animation: frame-in 0.9s var(--emph) both;
}

@keyframes frame-in {
  from {
    inset: 0;
    border-radius: 0;
  }
}

.backdrop {
  position: absolute;
  inset: 0;
  background: transparent;
  transition: background 0.4s var(--standard);
}

.backdrop.dim {
  background: rgba(0, 0, 0, 0.35);
}

.backdrop-enter-from,
.backdrop-leave-to {
  background: transparent !important;
}

.backdrop-enter-active,
.backdrop-leave-active {
  transition: background 0.35s var(--standard);
}

.hot {
  position: absolute;
}

.hot.top {
  top: 0;
  left: calc(var(--bar) + (100% - var(--bar) - var(--border)) / 2 - 200px);
  width: 400px;
  height: var(--border);
}

.hot.right {
  right: 0;
  top: calc(50% - 150px);
  width: var(--border);
  height: 300px;
}
</style>
