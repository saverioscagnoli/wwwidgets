<script setup>
import { computed, watch } from "vue";
import { battery } from "../../lib/services";
import { batteryIcon, hoursMinutes, isCharging } from "../../lib/format";
import { loadProfiles, profiles, setProfile } from "../../lib/system";
import MIcon from "../MIcon.vue";

let props = defineProps({ data: null, active: Boolean });

watch(
  () => props.active,
  value => value && loadProfiles(),
  { immediate: true }
);

let bat = computed(() => battery.value);
let charging = computed(() => isCharging(bat.value));

let detail = computed(() => {
  let b = bat.value;
  if (!b) return "";
  if (b.state == "full") return "Fully charged";
  if (charging.value && b.timeToFull > 0) return `${hoursMinutes(b.timeToFull)} until full`;
  if (!charging.value && b.timeToEmpty > 0) return `${hoursMinutes(b.timeToEmpty)} remaining`;
  return b.state.charAt(0).toUpperCase() + b.state.slice(1);
});

let PROFILE_ICONS = { "power-saver": "energy_savings_leaf", balanced: "balance", performance: "rocket_launch" };
</script>

<template>
  <div class="battery">
    <div class="top">
      <MIcon :name="batteryIcon(bat?.percent ?? 0, charging)" :fill="true" :size="40" class="big-icon" :class="{ charging }" />
      <div>
        <span class="percent">{{ Math.round(bat?.percent ?? 0) }}%</span>
        <span class="label">{{ detail }}</span>
      </div>
    </div>

    <div class="meter">
      <div class="meter-fill" :class="{ low: !charging && (bat?.percent ?? 0) <= 15, charging }" :style="{ width: `${bat?.percent ?? 0}%` }"></div>
    </div>

    <div v-if="profiles.available" class="profiles">
      <span class="label">Power profile</span>
      <div class="segmented">
        <button v-for="p in profiles.list" :key="p" :class="{ on: profiles.current == p }" @click="setProfile(p)">
          <MIcon :name="PROFILE_ICONS[p] ?? 'tune'" :fill="profiles.current == p" :size="18" />
          <span>{{ p.replace("-", " ") }}</span>
        </button>
      </div>
    </div>
  </div>
</template>

<style scoped>
.battery {
  width: 280px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}

.top {
  display: flex;
  align-items: center;
  gap: 12px;
}

.top > div {
  display: flex;
  flex-direction: column;
}

.big-icon {
  color: var(--primary);
}

.big-icon.charging {
  animation: pulse 1.6s ease-in-out infinite;
}

.percent {
  font-size: 28px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}

.meter {
  height: 10px;
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

.meter-fill.low {
  background: var(--error);
}

.meter-fill.charging {
  background: var(--tertiary);
}

.profiles {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.segmented {
  display: flex;
  gap: 2px;
  padding: 3px;
  border-radius: 999px;
  background: var(--surface-container-high);
}

.segmented button {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 2px;
  padding: 8px 4px;
  border-radius: 999px;
  font-size: 11px;
  text-transform: capitalize;
  color: var(--on-surface-variant);
  transition: background 0.3s var(--standard), color 0.3s, border-radius 0.3s;
}

.segmented button:hover:not(.on) {
  background: var(--state-hover);
}

.segmented button.on {
  background: var(--secondary-container);
  color: var(--on-secondary-container);
  font-weight: 600;
}
</style>
