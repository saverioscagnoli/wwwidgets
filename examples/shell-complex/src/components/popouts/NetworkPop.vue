<script setup>
import { computed, nextTick, ref, watch } from "vue";
import { activeAp, network, wired } from "../../lib/services";
import { networkIcon, wifiIcon } from "../../lib/format";
import { quiet, ww } from "../../lib/ww";
import MIcon from "../MIcon.vue";

let props = defineProps({ data: null, active: Boolean });

let scanning = ref(false);
let asking = ref(null);
let password = ref("");
let pending = ref(null);
let failed = ref(null);
let input = ref(null);

let net = computed(() => network.value);

let aps = computed(() => {
  let best = new Map();
  for (let ap of net.value?.wifiEnabled ? net.value.accessPoints : []) {
    if (!ap.ssid) continue;
    let current = best.get(ap.ssid);
    if (!current || ap.active || (!current.active && ap.strength > current.strength)) best.set(ap.ssid, ap);
  }
  return [...best.values()].sort((a, b) => b.active - a.active || b.strength - a.strength).slice(0, 8);
});

let status = computed(() => {
  let n = net.value;
  if (!n?.available) return "Unavailable";
  if (n.state == "connecting") return "Connecting…";
  if (wired.value) return `Wired · ${n.primary.name}`;
  if (activeAp.value) return activeAp.value.ssid;
  return n.wifiEnabled ? "Not connected" : "Wi-Fi off";
});

function scan() {
  scanning.value = true;
  quiet(ww.network.scan());
  setTimeout(() => (scanning.value = false), 3000);
}

watch(
  () => props.active,
  value => value && net.value?.wifiEnabled && scan(),
  { immediate: true }
);

async function connect(ap, pass) {
  pending.value = ap.ssid;
  failed.value = null;
  let ok = await ww.network.connect(ap.path, pass || null).then(
    () => true,
    () => false
  );
  pending.value = null;
  if (ok) asking.value = null;
  else failed.value = ap.ssid;
}

async function click(ap) {
  if (ap.active) return ww.network.disconnect();
  if (!ap.secure) return connect(ap, null);
  if (asking.value == ap.ssid) return (asking.value = null);
  asking.value = ap.ssid;
  password.value = "";
  await nextTick();
  input.value?.[0]?.focus();
}
</script>

<template>
  <div class="network">
    <div class="header">
      <div class="badge"><MIcon :name="networkIcon(net, activeAp, wired)" :fill="true" :size="22" /></div>
      <div class="info">
        <span class="title">Network</span>
        <span class="label ellipsis">{{ status }}</span>
      </div>
      <button v-if="net?.available" class="switch" :class="{ on: net.wifiEnabled }" @click="ww.network.setWifi(!net.wifiEnabled)"></button>
    </div>

    <div v-if="net?.wifiEnabled" class="section">
      <div class="section-head">
        <span class="label">Wi-Fi networks</span>
        <button class="icon-btn small" :class="{ spinning: scanning }" @click="scan"><MIcon name="refresh" :size="18" /></button>
      </div>
      <TransitionGroup name="fade" tag="div" class="list">
        <div v-for="ap in aps" :key="ap.ssid" class="ap" :class="{ error: failed == ap.ssid }">
          <button class="list-item" :class="{ active: ap.active, pending: pending == ap.ssid }" @click="click(ap)">
            <MIcon :name="wifiIcon(ap.strength)" :fill="true" />
            <span class="ellipsis grow">{{ ap.ssid }}</span>
            <MIcon v-if="ap.secure" name="lock" :size="16" class="dim" />
            <MIcon v-if="ap.active" name="check" :size="18" />
          </button>
          <Transition name="fade">
            <form v-if="asking == ap.ssid" class="password" @submit.prevent="connect(ap, password)">
              <input ref="input" v-model="password" class="text-field" type="password" placeholder="Password (empty for saved)" />
              <button class="btn filled" type="submit">Join</button>
            </form>
          </Transition>
        </div>
      </TransitionGroup>
      <span v-if="!aps.length" class="label empty">{{ scanning ? "Scanning…" : "No networks found" }}</span>
    </div>
  </div>
</template>

<style scoped>
.network {
  width: 300px;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.header {
  display: flex;
  align-items: center;
  gap: 12px;
}

.badge {
  display: grid;
  place-items: center;
  width: 44px;
  height: 44px;
  border-radius: 14px;
  background: var(--primary-container);
  color: var(--on-primary-container);
}

.info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}

.section-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 0 0 2px 4px;
}

.small {
  width: 30px;
  height: 30px;
}

.spinning .ms {
  animation: spin 1s linear infinite;
}

.list {
  display: flex;
  flex-direction: column;
}

.grow {
  flex: 1;
}

.dim {
  color: var(--outline);
}

.password {
  display: flex;
  gap: 6px;
  padding: 4px 4px 8px 40px;
}

.password input {
  flex: 1;
  min-width: 0;
}

.empty {
  padding: 8px 4px;
}
</style>
