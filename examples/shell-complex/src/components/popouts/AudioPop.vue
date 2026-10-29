<script setup>
import { computed } from "vue";
import { audio } from "../../lib/services";
import { pct, volumeIcon } from "../../lib/format";
import { ww } from "../../lib/ww";
import AppIcon from "../AppIcon.vue";
import MIcon from "../MIcon.vue";
import Slider from "../Slider.vue";

defineProps({ data: null, active: Boolean });

let sink = computed(() => audio.value?.sink ?? null);
let source = computed(() => audio.value?.source ?? null);
let sinks = computed(() => audio.value?.sinks ?? []);
let streams = computed(() => audio.value?.playback ?? []);
</script>

<template>
  <div class="audio">
    <template v-if="sink">
      <div class="head">
        <span class="title">Output</span>
        <span class="label">{{ sink.muted ? "Muted" : pct(sink.volume) }}</span>
      </div>
      <Slider
        :value="sink.volume"
        :muted="sink.muted"
        :icon="volumeIcon(sink.volume, sink.muted)"
        @input="v => ww.audio.setVolume('sink', v)"
        @icon="ww.audio.setMute('sink', !sink.muted)"
      />
    </template>

    <template v-if="source">
      <div class="head">
        <span class="title">Input</span>
        <span class="label">{{ source.muted ? "Muted" : pct(source.volume) }}</span>
      </div>
      <Slider
        :value="source.volume"
        :muted="source.muted"
        :icon="source.muted ? 'mic_off' : 'mic'"
        @input="v => ww.audio.setVolume('source', v)"
        @icon="ww.audio.setMute('source', !source.muted)"
      />
    </template>

    <div v-if="sinks.length > 1" class="section">
      <span class="label">Devices</span>
      <button
        v-for="d in sinks"
        :key="d.name"
        class="list-item"
        :class="{ active: d.default }"
        @click="ww.audio.setDefault('sink', d.name)"
      >
        <MIcon :name="d.default ? 'radio_button_checked' : 'radio_button_unchecked'" :fill="d.default" />
        <span class="ellipsis">{{ d.description }}</span>
      </button>
    </div>

    <div v-if="streams.length" class="section">
      <span class="label">Apps</span>
      <TransitionGroup name="fade" tag="div" class="streams">
        <div v-for="s in streams" :key="s.index" class="stream">
          <AppIcon :name="s.icon || s.app.toLowerCase()" :label="s.app" :size="28" />
          <div class="stream-main">
            <span class="ellipsis stream-name">{{ s.app }}</span>
            <Slider
              class="thin"
              :value="s.volume"
              :muted="s.muted"
              :icon="volumeIcon(s.volume, s.muted)"
              @input="v => ww.audio.setStreamVolume('sink', s.index, v)"
              @icon="ww.audio.setStreamMute('sink', s.index, !s.muted)"
            />
          </div>
        </div>
      </TransitionGroup>
    </div>
  </div>
</template>

<style scoped>
.audio {
  width: 300px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.head {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  padding: 0 4px;
}

.section {
  display: flex;
  flex-direction: column;
  gap: 2px;
}

.section > .label {
  padding: 4px 4px 2px;
}

.streams {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.stream {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 4px;
}

.stream-main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.stream-name {
  font-size: 12px;
  color: var(--on-surface-variant);
}

.thin {
  --thick: 28px;
}
</style>
