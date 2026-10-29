<script setup>
import { computed, ref, watch } from "vue";
import { cancelClose, dashboardOpen, geometry, later, setPanel } from "../lib/ui";
import { loadUser, loadWeather } from "../lib/system";
import Drawer from "./Drawer.vue";
import MIcon from "./MIcon.vue";
import DashHome from "./dashboard/DashHome.vue";
import DashMedia from "./dashboard/DashMedia.vue";
import DashPerformance from "./dashboard/DashPerformance.vue";

let TABS = [
  { id: "home", icon: "dashboard", label: "Dashboard", view: DashHome },
  { id: "media", icon: "queue_music", label: "Media", view: DashMedia },
  { id: "performance", icon: "monitoring", label: "Performance", view: DashPerformance },
];

let TAB_WIDTH = 130;

let tab = ref(0);
let direction = ref("next");

let current = computed(() => TABS[tab.value]);

watch(dashboardOpen, open => {
  if (!open) return;
  loadUser();
  loadWeather();
});

function select(i) {
  direction.value = i > tab.value ? "next" : "prev";
  tab.value = i;
}

function wheel(e) {
  let next = Math.max(0, Math.min(TABS.length - 1, tab.value + Math.sign(e.deltaY)));
  if (next != tab.value) select(next);
}

let indicator = computed(() => ({
  width: `${TAB_WIDTH - 40}px`,
  transform: `translateX(${tab.value * TAB_WIDTH + 20}px)`,
}));

function enter() {
  cancelClose("dashboard");
}

function leave() {
  later("dashboard", () => setPanel("dashboard", false), 350);
}

defineExpose({ enter, leave });

let style = {
  top: `${geometry.border}px`,
  left: `calc(${geometry.bar}px + (100% - ${geometry.bar + geometry.border}px) / 2)`,
  transform: "translateX(-50%)",
};
</script>

<template>
  <Drawer edge="top" :open="!!dashboardOpen" :fillets="['top-left', 'top-right']" :style="style" @enter="enter" @leave="leave">
    <div class="dashboard">
      <nav class="tabs" @wheel.prevent="wheel">
        <div class="tab-group">
          <button
            v-for="(t, i) in TABS"
            :key="t.id"
            class="tab"
            :class="{ active: i == tab }"
            :style="{ width: `${TAB_WIDTH}px` }"
            @click="select(i)"
          >
            <MIcon :name="t.icon" :fill="i == tab" :size="20" />
            <span>{{ t.label }}</span>
          </button>
          <span class="indicator" :style="indicator"></span>
        </div>
      </nav>
      <div class="pages">
        <Transition :name="direction" mode="out-in">
          <KeepAlive>
            <component :is="current.view" :key="current.id" :active="!!dashboardOpen" />
          </KeepAlive>
        </Transition>
      </div>
    </div>
  </Drawer>
</template>

<style scoped>
.dashboard {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 6px 16px 16px;
}

.tabs {
  display: flex;
  justify-content: center;
  border-bottom: 1px solid var(--outline-variant);
}

.tab-group {
  position: relative;
  display: flex;
}

.tab {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 8px 0 10px;
  border-radius: var(--rounding-sm) var(--rounding-sm) 0 0;
  color: var(--on-surface-variant);
  font-size: 12px;
  font-weight: 500;
  transition: background 0.2s, color 0.3s;
}

.tab:hover {
  background: var(--state-hover);
}

.tab.active {
  color: var(--primary);
}

.indicator {
  position: absolute;
  left: 0;
  bottom: -1px;
  height: 3px;
  border-radius: 3px 3px 0 0;
  background: var(--primary);
  transition: transform 0.45s var(--emph);
}

.pages {
  position: relative;
}

.next-enter-active,
.prev-enter-active {
  transition: transform 0.4s var(--emph), opacity 0.3s var(--standard);
}

.next-leave-active,
.prev-leave-active {
  transition: transform 0.15s var(--emph-accel), opacity 0.15s;
}

.next-enter-from,
.prev-leave-to {
  transform: translateX(40px);
  opacity: 0;
}

.next-leave-to,
.prev-enter-from {
  transform: translateX(-40px);
  opacity: 0;
}
</style>
