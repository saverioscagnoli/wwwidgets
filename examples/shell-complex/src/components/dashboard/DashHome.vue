<script setup>
import { computed, onActivated, onDeactivated, onMounted, onUnmounted, ref, watch } from "vue";
import { player } from "../../lib/services";
import { art, next, playPause, previous, progress } from "../../lib/media";
import { now, resources, user, watchResources, weather } from "../../lib/system";
import { bytes } from "../../lib/format";
import AppIcon from "../AppIcon.vue";
import Calendar from "../Calendar.vue";
import MIcon from "../MIcon.vue";

let props = defineProps({ active: Boolean });

let calendar = ref(null);
let artFailed = ref(false);
let stop = null;
let visible = ref(true);

watch(art, () => (artFailed.value = false));

let date = computed(() => new Date(now.value));
let hours = computed(() => String(date.value.getHours()).padStart(2, "0"));
let minutes = computed(() => String(date.value.getMinutes()).padStart(2, "0"));
let day = computed(() => date.value.toLocaleDateString(undefined, { weekday: "long" }));
let fullDate = computed(() => date.value.toLocaleDateString(undefined, { day: "numeric", month: "long" }));

function sync() {
  let want = props.active && visible.value;
  if (want && !stop) stop = watchResources();
  if (!want && stop) {
    stop();
    stop = null;
  }
}

watch(() => props.active, open => {
  sync();
  if (open) calendar.value?.reset();
});

onMounted(sync);
onActivated(() => {
  visible.value = true;
  sync();
});
onDeactivated(() => {
  visible.value = false;
  sync();
});
onUnmounted(() => stop?.());

let bars = computed(() => [
  { icon: "memory", label: "CPU", value: resources.cpu, text: `${Math.round(resources.cpu * 100)}%` },
  { icon: "memory_alt", label: "Memory", value: resources.memory, text: bytes(resources.memoryUsed) },
  { icon: "hard_drive", label: "Storage", value: resources.disk, text: `${Math.round(resources.disk * 100)}%` },
]);
</script>

<template>
  <div class="home">
    <section class="card user">
      <AppIcon :name="user.home ? `${user.home}/.face` : ''" :label="user.name || 'user'" :size="72" class="avatar" />
      <div class="who">
        <span class="name ellipsis">{{ user.name || "…" }}</span>
        <span class="label ellipsis">{{ user.login }}@{{ user.host }}</span>
        <span class="uptime"><MIcon name="timer" :size="16" />{{ user.uptime || "…" }}</span>
      </div>
    </section>

    <section class="card weather">
      <template v-if="weather">
        <MIcon :name="weather.icon" :fill="true" :size="48" class="weather-icon" />
        <div class="weather-main">
          <span class="temp">{{ weather.temp }}°</span>
          <span class="label ellipsis">{{ weather.description }}</span>
        </div>
        <div class="weather-side label">
          <span v-if="weather.high != null">↑ {{ weather.high }}° ↓ {{ weather.low }}°</span>
          <span class="ellipsis">{{ weather.place }}</span>
        </div>
      </template>
      <span v-else class="label muted"><MIcon name="cloud_off" :size="18" /> Weather unavailable</span>
    </section>

    <section class="card resources">
      <div v-for="b in bars" :key="b.label" class="res">
        <MIcon :name="b.icon" :size="18" />
        <div class="res-main">
          <div class="res-top">
            <span>{{ b.label }}</span>
            <span class="label">{{ b.text }}</span>
          </div>
          <div class="meter"><div class="meter-fill" :style="{ width: `${Math.round(b.value * 100)}%` }"></div></div>
        </div>
      </div>
    </section>

    <section class="card clock">
      <span class="hh">{{ hours }}</span>
      <span class="sep"></span>
      <span class="mm">{{ minutes }}</span>
      <span class="day">{{ day }}</span>
      <span class="label">{{ fullDate }}</span>
    </section>

    <section class="card cal">
      <Calendar ref="calendar" />
    </section>

    <section class="card media" :class="{ playing: player?.playing }">
      <template v-if="player">
        <div class="cover">
          <img v-if="art && !artFailed" :src="art" alt="" @error="artFailed = true" />
          <MIcon v-else name="music_note" :size="40" />
        </div>
        <span class="track ellipsis">{{ player.title || "Unknown" }}</span>
        <span class="label ellipsis">{{ player.artist || player.album || player.identity }}</span>
        <div class="line"><div class="line-fill" :style="{ width: `${progress * 100}%` }"></div></div>
        <div class="controls">
          <button class="icon-btn" :disabled="!player.canPrev" @click="previous"><MIcon name="skip_previous" :fill="true" /></button>
          <button class="icon-btn filled play" @click="playPause"><MIcon :name="player.playing ? 'pause' : 'play_arrow'" :fill="true" /></button>
          <button class="icon-btn" :disabled="!player.canNext" @click="next"><MIcon name="skip_next" :fill="true" /></button>
        </div>
      </template>
      <div v-else class="idle">
        <MIcon name="music_off" :size="40" />
        <span class="label">Nothing playing</span>
      </div>
    </section>
  </div>
</template>

<style scoped>
.home {
  display: grid;
  grid-template-columns: 300px 130px 260px 200px;
  grid-template-rows: auto auto 1fr;
  gap: 10px;
}

.card {
  padding: 14px;
  min-width: 0;
}

.user {
  grid-column: 1;
  grid-row: 1;
  display: flex;
  align-items: center;
  gap: 14px;
}

.avatar {
  border-radius: 999px;
  box-shadow: 0 0 0 3px var(--primary-container);
}

.who {
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}

.name {
  font-size: 17px;
  font-weight: 600;
}

.uptime {
  display: inline-flex;
  align-items: center;
  gap: 4px;
  margin-top: 4px;
  font-size: 12px;
  color: var(--tertiary);
}

.weather {
  grid-column: 1;
  grid-row: 2;
  display: flex;
  align-items: center;
  gap: 12px;
}

.weather-icon {
  color: var(--tertiary);
}

.weather-main {
  display: flex;
  flex-direction: column;
  min-width: 0;
  flex: 1;
}

.temp {
  font-size: 30px;
  font-weight: 600;
  line-height: 1;
}

.weather-side {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 2px;
  min-width: 0;
  max-width: 110px;
}

.muted {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}

.resources {
  grid-column: 1;
  grid-row: 3;
  display: flex;
  flex-direction: column;
  justify-content: center;
  gap: 12px;
}

.res {
  display: flex;
  align-items: center;
  gap: 12px;
  color: var(--primary);
}

.res-main {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 4px;
  color: var(--on-surface);
}

.res-top {
  display: flex;
  justify-content: space-between;
  font-size: 12px;
}

.meter {
  height: 6px;
  border-radius: 999px;
  background: var(--surface-container-highest);
  overflow: hidden;
}

.meter-fill {
  height: 100%;
  border-radius: inherit;
  background: var(--primary);
  transition: width 0.8s var(--emph);
}

.clock {
  grid-column: 2;
  grid-row: 1 / 4;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 2px;
  background: var(--primary-container);
  color: var(--on-primary-container);
  text-align: center;
}

.hh,
.mm {
  font-size: 52px;
  font-weight: 600;
  line-height: 1;
  font-variant-numeric: tabular-nums;
}

.mm {
  color: var(--primary);
}

.sep {
  width: 40px;
  height: 3px;
  margin: 8px 0;
  border-radius: 3px;
  background: currentColor;
  opacity: 0.4;
}

.day {
  margin-top: 14px;
  font-weight: 600;
}

.clock .label {
  color: inherit;
  opacity: 0.8;
}

.cal {
  grid-column: 3;
  grid-row: 1 / 4;
}

.media {
  grid-column: 4;
  grid-row: 1 / 4;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  text-align: center;
}

.cover {
  display: grid;
  place-items: center;
  width: 130px;
  height: 130px;
  margin: 4px 0 10px;
  border-radius: 999px;
  overflow: hidden;
  background: var(--secondary-container);
  color: var(--on-secondary-container);
  box-shadow: 0 0 0 6px var(--surface-container-high);
  animation: spin 14s linear infinite;
  animation-play-state: paused;
}

.playing .cover {
  animation-play-state: running;
}

.cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.track {
  max-width: 100%;
  font-weight: 600;
}

.media .label {
  max-width: 100%;
}

.line {
  width: 100%;
  height: 4px;
  margin: 10px 0 4px;
  border-radius: 999px;
  background: var(--surface-container-highest);
  overflow: hidden;
}

.line-fill {
  height: 100%;
  background: var(--primary);
  transition: width 1s linear;
}

.controls {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: auto;
}

.play {
  width: 46px;
  height: 46px;
  border-radius: 16px;
}

.idle {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  color: var(--outline);
}
</style>
