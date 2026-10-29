<script setup>
import { computed, onActivated, onDeactivated, onMounted, onUnmounted, ref, watch } from "vue";
import { resources, watchResources } from "../../lib/system";
import { bytes } from "../../lib/format";
import Gauge from "../Gauge.vue";
import MIcon from "../MIcon.vue";

let props = defineProps({ active: Boolean });

let stop = null;
let visible = ref(true);

function sync() {
  let want = props.active && visible.value;
  if (want && !stop) stop = watchResources();
  if (!want && stop) {
    stop();
    stop = null;
  }
}

watch(() => props.active, sync);
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

let tempRatio = computed(() => (resources.temp == null ? 0 : Math.min(1, resources.temp / 100)));
let tempColor = computed(() => (resources.temp >= 80 ? "var(--error)" : resources.temp >= 65 ? "var(--tertiary)" : "var(--secondary)"));

let spark = computed(() => {
  let points = resources.history;
  if (points.length < 2) return "";
  let w = 520;
  let hgt = 70;
  let step = w / 39;
  let offset = (40 - points.length) * step;
  return points.map((v, i) => `${(offset + i * step).toFixed(1)},${(hgt - v * hgt).toFixed(1)}`).join(" ");
});

let area = computed(() => {
  if (!spark.value) return "";
  let first = spark.value.split(" ")[0].split(",")[0];
  return `${first},70 ${spark.value} 520,70`;
});
</script>

<template>
  <div class="perf">
    <div class="gauges">
      <Gauge :value="resources.cpu" icon="memory" label="CPU" :text="`${Math.round(resources.cpu * 100)}%`" />
      <Gauge :value="resources.memory" icon="memory_alt" label="Memory" :text="`${Math.round(resources.memory * 100)}%`" color="var(--tertiary)" />
      <Gauge :value="resources.disk" icon="hard_drive" label="Storage" :text="`${Math.round(resources.disk * 100)}%`" color="var(--secondary)" />
      <Gauge
        :value="tempRatio"
        icon="thermostat"
        label="Temperature"
        :text="resources.temp == null ? '--' : `${Math.round(resources.temp)}°`"
        :color="tempColor"
      />
    </div>

    <div class="card history">
      <div class="history-head">
        <span class="title">CPU usage</span>
        <span class="label">last 80 seconds</span>
      </div>
      <svg viewBox="0 0 520 70" preserveAspectRatio="none">
        <polygon v-if="area" :points="area" class="area" />
        <polyline v-if="spark" :points="spark" class="line" />
      </svg>
    </div>

    <div class="facts">
      <div class="fact card">
        <MIcon name="developer_board" :size="20" />
        <span class="label">Cores</span>
        <b>{{ resources.cores || "--" }}</b>
      </div>
      <div class="fact card">
        <MIcon name="speed" :size="20" />
        <span class="label">Load</span>
        <b>{{ resources.load || "--" }}</b>
      </div>
      <div class="fact card">
        <MIcon name="memory_alt" :size="20" />
        <span class="label">RAM</span>
        <b>{{ bytes(resources.memoryUsed) }} / {{ bytes(resources.memoryTotal) }}</b>
      </div>
      <div class="fact card">
        <MIcon name="hard_drive" :size="20" />
        <span class="label">Disk</span>
        <b>{{ bytes(resources.diskUsed) }} / {{ bytes(resources.diskTotal) }}</b>
      </div>
    </div>
  </div>
</template>

<style scoped>
.perf {
  width: 560px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 6px 0;
}

.gauges {
  display: flex;
  justify-content: space-around;
  padding-top: 4px;
}

.history {
  padding: 12px 14px;
}

.history-head {
  display: flex;
  justify-content: space-between;
  align-items: baseline;
  margin-bottom: 8px;
}

svg {
  width: 100%;
  height: 70px;
  display: block;
}

.area {
  fill: color-mix(in srgb, var(--primary) 22%, transparent);
}

.line {
  fill: none;
  stroke: var(--primary);
  stroke-width: 2;
  stroke-linejoin: round;
  vector-effect: non-scaling-stroke;
}

.facts {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
}

.fact {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 12px;
  color: var(--primary);
}

.fact b {
  color: var(--on-surface);
  font-weight: 600;
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
</style>
