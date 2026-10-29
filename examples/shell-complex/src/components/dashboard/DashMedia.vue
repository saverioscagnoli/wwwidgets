<script setup>
import { computed, ref, watch } from "vue";
import { chosenPlayer, player, players } from "../../lib/services";
import { art, next, playPause, position, previous, progress, seek } from "../../lib/media";
import { duration } from "../../lib/format";
import MIcon from "../MIcon.vue";
import Slider from "../Slider.vue";

defineProps({ active: Boolean });

let failed = ref(false);
watch(art, () => (failed.value = false));

let list = computed(() => players.value.filter(p => p.title || p.playing));

function choose(id) {
  chosenPlayer.value = id;
}
</script>

<template>
  <div class="media" :class="{ playing: player?.playing }">
    <template v-if="player">
      <div class="disc-wrap">
        <div class="disc">
          <img v-if="art && !failed" :src="art" alt="" @error="failed = true" />
          <MIcon v-else name="album" :size="80" />
          <span class="hole"></span>
        </div>
      </div>

      <div class="side">
        <span class="label source"><MIcon name="speaker" :size="16" /> {{ player.identity }}</span>
        <Transition name="swap" mode="out-in">
          <div :key="player.title" class="meta">
            <span class="track">{{ player.title || "Unknown title" }}</span>
            <span class="artist ellipsis">{{ player.artist || "Unknown artist" }}</span>
            <span v-if="player.album" class="label ellipsis">{{ player.album }}</span>
          </div>
        </Transition>

        <div class="progress">
          <Slider v-if="player.canSeek && player.length > 0" class="seek" :value="progress" @input="seek" />
          <div v-else class="static"><div :style="{ width: `${progress * 100}%` }"></div></div>
          <div class="times label">
            <span>{{ duration(position) }}</span>
            <span>{{ player.length > 0 ? duration(player.length) : "--:--" }}</span>
          </div>
        </div>

        <div class="controls">
          <button class="icon-btn big" :disabled="!player.canPrev" @click="previous"><MIcon name="skip_previous" :fill="true" :size="28" /></button>
          <button class="icon-btn filled play" @click="playPause"><MIcon :name="player.playing ? 'pause' : 'play_arrow'" :fill="true" :size="32" /></button>
          <button class="icon-btn big" :disabled="!player.canNext" @click="next"><MIcon name="skip_next" :fill="true" :size="28" /></button>
        </div>

        <div v-if="list.length > 1" class="players">
          <button v-for="p in list" :key="p.id" class="chip" :class="{ on: p.id == player.id }" @click="choose(p.id)">
            <MIcon :name="p.playing ? 'graphic_eq' : 'pause_circle'" :size="16" />
            {{ p.identity }}
          </button>
        </div>
      </div>
    </template>

    <div v-else class="idle">
      <MIcon name="music_off" :size="56" />
      <span class="title">Nothing is playing</span>
      <span class="label">Start some music and it will show up here</span>
    </div>
  </div>
</template>

<style scoped>
.media {
  display: flex;
  gap: 28px;
  align-items: center;
  width: 720px;
  padding: 10px 8px;
}

.disc-wrap {
  padding: 10px;
  border-radius: 999px;
  background: var(--surface-container);
  flex: none;
}

.disc {
  position: relative;
  display: grid;
  place-items: center;
  width: 240px;
  height: 240px;
  border-radius: 999px;
  overflow: hidden;
  background: var(--secondary-container);
  color: var(--on-secondary-container);
  animation: spin 18s linear infinite;
  animation-play-state: paused;
  transition: transform 0.6s var(--spring);
}

.playing .disc {
  animation-play-state: running;
}

.media:not(.playing) .disc-wrap {
  transform: scale(0.96);
}

.disc-wrap {
  transition: transform 0.6s var(--spring);
}

.disc img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}

.hole {
  position: absolute;
  width: 34px;
  height: 34px;
  border-radius: 999px;
  background: var(--surface-container);
  box-shadow: 0 0 0 6px rgba(0, 0, 0, 0.25);
}

.side {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.source {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: var(--tertiary);
}

.meta {
  display: flex;
  flex-direction: column;
  gap: 4px;
  min-width: 0;
}

.track {
  font-size: 24px;
  font-weight: 600;
  line-height: 1.2;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}

.artist {
  font-size: 15px;
  color: var(--primary);
}

.progress {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.seek {
  --thick: 14px;
  flex: none;
}

.static {
  height: 14px;
  border-radius: 999px;
  background: var(--secondary-container);
  overflow: hidden;
}

.static div {
  height: 100%;
  background: var(--primary);
  border-radius: inherit;
  transition: width 1s linear;
}

.times {
  display: flex;
  justify-content: space-between;
  font-variant-numeric: tabular-nums;
}

.controls {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 18px;
}

.big {
  width: 52px;
  height: 52px;
  color: var(--on-surface);
}

.play {
  width: 72px;
  height: 64px;
  border-radius: 22px;
}

.playing .play {
  border-radius: 999px;
}

.players {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.chip {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  height: 32px;
  padding: 0 12px;
  border-radius: 10px;
  box-shadow: inset 0 0 0 1px var(--outline-variant);
  color: var(--on-surface-variant);
  font-size: 12px;
  transition: background 0.2s, color 0.2s;
}

.chip.on {
  box-shadow: none;
  background: var(--secondary-container);
  color: var(--on-secondary-container);
}

.idle {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 50px 0;
  color: var(--outline);
}

.idle .title {
  color: var(--on-surface);
}
</style>
