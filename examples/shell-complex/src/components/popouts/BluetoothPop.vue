<script setup>
import { computed, onBeforeUnmount, ref, watch } from "vue";
import { bluetooth, btRequest as request } from "../../lib/services";
import { quiet, ww } from "../../lib/ww";
import MIcon from "../MIcon.vue";

let props = defineProps({ data: null, active: Boolean });

let pending = ref(null);
let failed = ref(null);
let answer = ref("");

let bt = computed(() => bluetooth.value);

let devices = computed(() =>
  (bt.value?.powered ? bt.value.devices : [])
    .filter(d => d.name)
    .sort((a, b) => b.connected - a.connected || b.paired - a.paired || a.name.localeCompare(b.name))
);

watch(
  () => props.active && bt.value?.powered,
  value => quiet(ww.bluetooth.scan(!!value)),
  { immediate: true }
);

onBeforeUnmount(() => bt.value?.discovering && quiet(ww.bluetooth.scan(false)));

watch(request, () => (answer.value = ""));

function deviceIcon(d) {
  let icon = d.icon ?? "";
  if (icon.includes("headset") || icon.includes("headphone")) return "headphones";
  if (icon.includes("audio")) return "speaker";
  if (icon.includes("phone")) return "smartphone";
  if (icon.includes("keyboard")) return "keyboard";
  if (icon.includes("mouse")) return "mouse";
  if (icon.includes("gaming") || icon.includes("joystick")) return "sports_esports";
  if (icon.includes("computer")) return "computer";
  return "bluetooth";
}

async function click(d) {
  pending.value = d.path;
  failed.value = null;
  let action = d.connected
    ? ww.bluetooth.disconnect(d.path)
    : d.paired
      ? ww.bluetooth.connect(d.path)
      : ww.bluetooth.pair(d.path).then(() => ww.bluetooth.connect(d.path));
  let ok = await action.then(
    () => true,
    () => false
  );
  pending.value = null;
  if (!ok) failed.value = d.path;
}

function accept() {
  let req = request.value;
  let value = req.type == "passkey" ? Number(answer.value) : req.type == "pin" ? answer.value : undefined;
  request.value = null;
  quiet(ww.bluetooth.reply(value));
}

function reject() {
  request.value = null;
  quiet(ww.bluetooth.reject());
}

let prompts = {
  display: "Type this code on the device",
  confirm: "Does this code match?",
  authorize: "Allow this device to connect?",
  pin: "Enter the PIN",
  passkey: "Enter the passkey",
};
</script>

<template>
  <div class="bluetooth">
    <div class="header">
      <div class="badge"><MIcon :name="bt?.powered ? 'bluetooth' : 'bluetooth_disabled'" :fill="true" :size="22" /></div>
      <div class="info">
        <span class="title">Bluetooth</span>
        <span class="label">{{ !bt?.available ? "Unavailable" : bt.powered ? (bt.discovering ? "Searching…" : "On") : "Off" }}</span>
      </div>
      <button v-if="bt?.available" class="switch" :class="{ on: bt.powered }" @click="ww.bluetooth.setPowered(!bt.powered)"></button>
    </div>

    <Transition name="fade">
      <div v-if="request" class="request">
        <b>{{ request.name || request.device }}</b>
        <span class="label">{{ prompts[request.type] }}</span>
        <span v-if="request.code" class="code">{{ request.code }}</span>
        <input v-if="request.type == 'pin' || request.type == 'passkey'" v-model="answer" class="text-field" inputmode="numeric" />
        <div class="actions">
          <button class="btn text" @click="reject">{{ request.type == "display" ? "Cancel" : "Reject" }}</button>
          <button v-if="request.type != 'display'" class="btn filled" @click="accept">Accept</button>
        </div>
      </div>
    </Transition>

    <TransitionGroup v-if="bt?.powered" name="fade" tag="div" class="list">
      <button
        v-for="d in devices"
        :key="d.path"
        class="list-item"
        :class="{ active: d.connected, pending: pending == d.path, error: failed == d.path }"
        @click="click(d)"
      >
        <MIcon :name="deviceIcon(d)" :fill="d.connected" />
        <span class="ellipsis grow">{{ d.name }}</span>
        <span v-if="d.battery != null" class="label">{{ d.battery }}%</span>
        <span class="label">{{ d.connected ? "Connected" : d.paired ? "Paired" : "" }}</span>
      </button>
    </TransitionGroup>
    <span v-if="bt?.powered && !devices.length" class="label empty">No devices nearby</span>
  </div>
</template>

<style scoped>
.bluetooth {
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
  background: var(--tertiary-container);
  color: var(--on-tertiary-container);
}

.info {
  flex: 1;
  display: flex;
  flex-direction: column;
}

.list {
  display: flex;
  flex-direction: column;
  max-height: 320px;
  overflow-y: auto;
}

.grow {
  flex: 1;
}

.request {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 14px;
  border-radius: var(--rounding-md);
  background: var(--secondary-container);
  color: var(--on-secondary-container);
}

.code {
  font-size: 24px;
  font-weight: 700;
  letter-spacing: 0.2em;
  text-align: center;
}

.actions {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}

.empty {
  padding: 4px;
}
</style>
