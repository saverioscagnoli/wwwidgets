<script setup>
import { computed, ref, watch } from "vue";
import { activeAp, activeWindow, audio, battery, bluetooth, network, notifications, trayItems, wired, workspaces } from "../lib/services";
import { closePopout, dnd, openPopout, togglePanel, ui } from "../lib/ui";
import { now } from "../lib/system";
import { batteryIcon, isCharging, networkIcon, volumeIcon } from "../lib/format";
import { clamp, ww } from "../lib/ww";
import AppIcon from "./AppIcon.vue";
import MIcon from "./MIcon.vue";

let SIZE = 28;

let list = computed(() => workspaces.value.filter(w => !w.hidden));
let activeIndex = computed(() => Math.max(0, list.value.findIndex(w => w.active)));
let direction = ref("down");

watch(activeIndex, (next, prev) => (direction.value = next >= prev ? "down" : "up"));

let indicator = computed(() => ({
  top: `${activeIndex.value * SIZE}px`,
  bottom: `${(list.value.length - activeIndex.value - 1) * SIZE}px`,
}));

function wsLabel(w) {
  return /^\d+$/.test(w.name) ? w.name : w.name.charAt(0).toUpperCase();
}

function wheelWorkspaces(e) {
  let next = list.value[clamp(activeIndex.value + Math.sign(e.deltaY), 0, list.value.length - 1)];
  if (next && !next.active) ww.workspace.set(next.id);
}

let hours = computed(() => String(new Date(now.value).getHours()).padStart(2, "0"));
let minutes = computed(() => String(new Date(now.value).getMinutes()).padStart(2, "0"));

let sink = computed(() => audio.value?.sink ?? null);
let volIcon = computed(() => (sink.value ? volumeIcon(sink.value.volume, sink.value.muted) : "volume_off"));
let netIcon = computed(() => networkIcon(network.value, activeAp.value, wired.value));
let btShown = computed(() => bluetooth.value?.available);
let btIcon = computed(() => {
  let bt = bluetooth.value;
  if (!bt?.powered) return "bluetooth_disabled";
  if (bt.devices.some(d => d.connected)) return "bluetooth_connected";
  if (bt.discovering) return "bluetooth_searching";
  return "bluetooth";
});
let batShown = computed(() => battery.value && battery.value.state != "unknown");
let batIcon = computed(() => batteryIcon(battery.value?.percent ?? 0, isCharging(battery.value)));
let batLow = computed(() => batShown.value && !isCharging(battery.value) && battery.value.percent <= 15);

let tray = computed(() => trayItems.value.filter(i => i.status != "Passive"));

let ringing = ref(false);
watch(
  () => notifications.value.length,
  (next, prev) => {
    if (next <= prev) return;
    ringing.value = false;
    requestAnimationFrame(() => (ringing.value = true));
  }
);

function wheelVolume(e) {
  if (!sink.value) return;
  ww.audio.setVolume("sink", clamp(sink.value.volume - Math.sign(e.deltaY) * 0.05, 0, 1));
}

function trayClick(item) {
  if (item.itemIsMenu) return openTrayMenu(item);
  ww.tray.activate(item.id);
}

function openTrayMenu(item, e) {
  let el = e?.currentTarget ?? document.querySelector(`[data-tray="${item.id}"]`);
  if (el) openPopout("tray", el, item.id);
}

function enter(name, e) {
  openPopout(name, e.currentTarget);
}
</script>

<template>
  <nav class="bar" data-region @mouseleave="closePopout()">
    <button class="logo" title="Applications" @click="togglePanel('launcher')" @mouseenter="closePopout(150)">
      <MIcon name="blur_on" :size="26" />
    </button>

    <div class="workspaces" @wheel.prevent="wheelWorkspaces" @mouseenter="closePopout(150)">
      <div class="indicator" :class="direction" :style="indicator"></div>
      <button
        v-for="w in list"
        :key="w.id"
        class="ws"
        :class="{ active: w.active, urgent: w.urgent }"
        :title="w.name"
        @click="ww.workspace.set(w.id)"
      >
        {{ wsLabel(w) }}
      </button>
    </div>

    <button class="window" @mouseenter="enter('window', $event)">
      <Transition name="swap" mode="out-in">
        <AppIcon :key="activeWindow?.appId ?? 'none'" :name="activeWindow?.appId" :label="activeWindow?.appId || 'desktop'" :size="20" />
      </Transition>
      <Transition name="swap" mode="out-in">
        <span :key="activeWindow?.title ?? ''" class="vtitle">{{ activeWindow?.title ?? "Desktop" }}</span>
      </Transition>
    </button>

    <TransitionGroup v-if="tray.length" name="pop" tag="div" class="tray" @mouseenter="closePopout(150)">
      <button
        v-for="item in tray"
        :key="item.id"
        class="tray-item"
        :data-tray="item.id"
        :class="{ attention: item.status == 'NeedsAttention' }"
        :title="item.tooltip || item.title"
        @click="trayClick(item)"
        @auxclick="$event.button == 1 && ww.tray.secondaryActivate(item.id)"
        @contextmenu.prevent="openTrayMenu(item, $event)"
        @wheel="ww.tray.scroll(item.id, Math.sign($event.deltaY))"
      >
        <AppIcon :name="item.icon" :label="item.title || item.appId" :size="18" />
      </button>
    </TransitionGroup>

    <button class="clock" title="Dashboard" @click="togglePanel('dashboard')" @mouseenter="closePopout(150)">
      <MIcon name="calendar_month" :size="18" />
      <span>{{ hours }}</span>
      <span>{{ minutes }}</span>
    </button>

    <div class="status">
      <button class="status-item" :class="{ hot: ui.popout == 'audio' }" @mouseenter="enter('audio', $event)" @wheel.prevent="wheelVolume">
        <MIcon :name="volIcon" :fill="true" />
      </button>
      <button class="status-item" :class="{ hot: ui.popout == 'network' }" @mouseenter="enter('network', $event)">
        <MIcon :name="netIcon" :fill="true" />
      </button>
      <button v-if="btShown" class="status-item" :class="{ hot: ui.popout == 'bluetooth' }" @mouseenter="enter('bluetooth', $event)">
        <MIcon :name="btIcon" :fill="true" />
      </button>
      <button v-if="batShown" class="status-item battery" :class="{ hot: ui.popout == 'battery', low: batLow }" @mouseenter="enter('battery', $event)">
        <MIcon :name="batIcon" :fill="true" />
      </button>
      <button class="status-item" :class="{ hot: ui.popout == 'notifications', ringing }" @mouseenter="enter('notifications', $event)">
        <MIcon :name="dnd ? 'notifications_off' : notifications.length ? 'notifications_unread' : 'notifications'" :fill="true" />
      </button>
    </div>

    <button class="power" title="Session" @click="togglePanel('session')" @mouseenter="closePopout(150)">
      <MIcon name="power_settings_new" :size="20" />
    </button>
  </nav>
</template>

<style scoped>
.bar {
  position: absolute;
  left: 0;
  top: 0;
  bottom: 0;
  width: var(--bar);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 10px 0;
  animation: bar-in 0.8s var(--emph) both;
}

@keyframes bar-in {
  from {
    transform: translateX(-100%);
  }
}

.logo {
  display: grid;
  place-items: center;
  width: 36px;
  height: 36px;
  border-radius: 999px;
  color: var(--primary);
  transition: transform 0.5s var(--spring), background 0.2s;
}

.logo:hover {
  background: var(--state-hover);
  transform: rotate(90deg);
}

.workspaces {
  position: relative;
  display: flex;
  flex-direction: column;
  padding: 4px;
  border-radius: 999px;
  background: var(--surface-container);
  box-sizing: content-box;
}

.indicator {
  position: absolute;
  left: 4px;
  right: 4px;
  margin: 4px 0;
  border-radius: 999px;
  background: var(--primary);
}

.indicator.down {
  transition: bottom 0.25s var(--emph), top 0.5s var(--emph);
}

.indicator.up {
  transition: top 0.25s var(--emph), bottom 0.5s var(--emph);
}

.ws {
  position: relative;
  width: 28px;
  height: 28px;
  border-radius: 999px;
  font-size: 12px;
  font-weight: 500;
  color: var(--on-surface-variant);
  transition: color 0.3s var(--standard), background 0.2s;
}

.ws:hover:not(.active) {
  background: var(--state-hover);
}

.ws.active {
  color: var(--on-primary);
  font-weight: 700;
}

.ws.urgent:not(.active) {
  color: var(--error);
  animation: pulse 1s ease-in-out infinite;
}

.window {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 6px 0;
  border-radius: 999px;
  width: 36px;
  overflow: hidden;
  transition: background 0.2s;
}

.window:hover {
  background: var(--state-hover);
}

.vtitle {
  writing-mode: vertical-rl;
  min-height: 0;
  max-height: 100%;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-weight: 500;
  color: var(--on-surface-variant);
}

.tray {
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 4px;
  border-radius: 999px;
  background: var(--surface-container);
}

.tray-item {
  display: grid;
  place-items: center;
  width: 28px;
  height: 28px;
  border-radius: 999px;
  transition: background 0.2s, transform 0.3s var(--spring);
}

.tray-item:hover {
  background: var(--state-hover);
}

.tray-item.attention {
  animation: pulse 1s ease-in-out infinite;
}

.pop-enter-active,
.pop-leave-active {
  transition: transform 0.4s var(--spring), opacity 0.3s;
}

.pop-enter-from,
.pop-leave-to {
  transform: scale(0.3);
  opacity: 0;
}

.clock {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 8px 0;
  width: 36px;
  border-radius: 999px;
  font-weight: 600;
  font-size: 14px;
  line-height: 1.1;
  font-variant-numeric: tabular-nums;
  color: var(--tertiary);
  transition: background 0.2s;
}

.clock .ms {
  margin-bottom: 4px;
}

.clock:hover {
  background: var(--state-hover);
}

.status {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 6px 4px;
  border-radius: 999px;
  background: var(--surface-container);
}

.status-item {
  display: grid;
  place-items: center;
  width: 28px;
  height: 30px;
  border-radius: 999px;
  color: var(--on-surface);
  transition: background 0.2s, color 0.2s;
}

.status-item:hover,
.status-item.hot {
  background: var(--state-hover);
  color: var(--primary);
}

.status-item .ms {
  font-size: 18px;
}

.battery.low {
  color: var(--error);
  animation: pulse 1.2s ease-in-out infinite;
}

.ringing .ms {
  animation: ring 0.9s var(--standard);
  transform-origin: 50% 15%;
}

@keyframes ring {
  15% {
    transform: rotate(18deg);
  }
  30% {
    transform: rotate(-16deg);
  }
  45% {
    transform: rotate(12deg);
  }
  60% {
    transform: rotate(-8deg);
  }
  75% {
    transform: rotate(4deg);
  }
}

.power {
  display: grid;
  place-items: center;
  width: 36px;
  height: 36px;
  border-radius: 999px;
  color: var(--error);
  transition: background 0.2s, border-radius 0.3s var(--standard);
}

.power:hover {
  background: var(--error-container);
  color: var(--on-error-container);
  border-radius: 12px;
}
</style>
