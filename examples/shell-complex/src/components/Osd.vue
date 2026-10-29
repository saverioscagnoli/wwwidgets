<script setup>
import { computed, watch } from "vue";
import { audio, brightness } from "../lib/services";
import { brightnessIcon, pct, volumeIcon } from "../lib/format";
import { cancelClose, geometry, later, sessionOpen, ui } from "../lib/ui";
import { ww } from "../lib/ww";
import Drawer from "./Drawer.vue";
import Slider from "./Slider.vue";

let sink = computed(() => audio.value?.sink ?? null);
let source = computed(() => audio.value?.source ?? null);
let hovering = false;

function flash() {
  ui.osd = true;
  if (!hovering) later("osd", () => (ui.osd = false), 1500);
}

watch(
  () => sink.value && `${sink.value.name}|${Math.round(sink.value.volume * 100)}|${sink.value.muted}`,
  (next, prev) => prev && next && flash()
);

watch(
  () => source.value?.muted,
  (next, prev) => prev != null && next != null && next != prev && flash()
);

watch(
  () => brightness.value && Math.round(brightness.value.value * 100),
  (next, prev) => prev != null && next != null && next != prev && flash()
);

function enter() {
  hovering = true;
  cancelClose("osd");
  ui.osd = true;
}

function leave() {
  hovering = false;
  later("osd", () => (ui.osd = false), 400);
}

defineExpose({ enter, leave });

let style = { right: `${geometry.border}px`, top: "50%", transform: "translateY(-50%)" };
</script>

<template>
  <Drawer edge="right" :open="ui.osd && !sessionOpen" :fillets="['right-above', 'right-below']" :style="style" @enter="enter" @leave="leave">
    <div class="osd">
      <div v-if="sink" class="col">
        <Slider
          vertical
          :value="sink.volume"
          :muted="sink.muted"
          :icon="volumeIcon(sink.volume, sink.muted)"
          @input="v => ww.audio.setVolume('sink', v)"
          @icon="ww.audio.setMute('sink', !sink.muted)"
        />
        <span class="label">{{ sink.muted ? "Off" : pct(sink.volume) }}</span>
      </div>
      <div v-if="source" class="col">
        <Slider
          vertical
          :value="source.volume"
          :muted="source.muted"
          :icon="source.muted ? 'mic_off' : 'mic'"
          @input="v => ww.audio.setVolume('source', v)"
          @icon="ww.audio.setMute('source', !source.muted)"
        />
        <span class="label">{{ source.muted ? "Off" : pct(source.volume) }}</span>
      </div>
      <div v-if="brightness" class="col">
        <Slider vertical :value="brightness.value" :icon="brightnessIcon(brightness.value)" @input="v => ww.brightness.set(Math.max(0.01, v))" />
        <span class="label">{{ pct(brightness.value) }}</span>
      </div>
    </div>
  </Drawer>
</template>

<style scoped>
.osd {
  display: flex;
  gap: 10px;
  padding: 14px 12px 12px 14px;
}

.col {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  height: 220px;
}

.col .slider {
  flex: 1;
  height: auto;
}

.label {
  font-variant-numeric: tabular-nums;
}
</style>
