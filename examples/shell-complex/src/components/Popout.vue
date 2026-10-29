<script setup>
import { computed, ref, watch } from "vue";
import { cancelClose, closePopout, geometry, ui } from "../lib/ui";
import { clamp } from "../lib/ww";
import Drawer from "./Drawer.vue";
import AudioPop from "./popouts/AudioPop.vue";
import BatteryPop from "./popouts/BatteryPop.vue";
import BluetoothPop from "./popouts/BluetoothPop.vue";
import NetworkPop from "./popouts/NetworkPop.vue";
import NotificationsPop from "./popouts/NotificationsPop.vue";
import TrayPop from "./popouts/TrayPop.vue";
import WindowPop from "./popouts/WindowPop.vue";

let props = defineProps({ blocked: Boolean });

let views = {
  audio: AudioPop,
  battery: BatteryPop,
  bluetooth: BluetoothPop,
  network: NetworkPop,
  notifications: NotificationsPop,
  tray: TrayPop,
  window: WindowPop,
};

let shown = ref(null);
let data = ref(null);
let height = ref(0);

watch(
  () => [ui.popout, ui.popoutData],
  ([name, value]) => {
    if (!name) return;
    shown.value = name;
    data.value = value;
  }
);

let open = computed(() => !!ui.popout && !props.blocked);

let top = computed(() => {
  let min = geometry.border + geometry.radius * 2;
  let max = window.innerHeight - geometry.border - geometry.radius * 2 - height.value;
  return clamp(ui.popoutY - height.value / 2, min, Math.max(min, max));
});

let style = computed(() => ({
  left: `${geometry.bar}px`,
  top: `${top.value}px`,
  transition: `width 0.5s var(--emph), height 0.5s var(--emph), top 0.45s var(--emph)`,
}));
</script>

<template>
  <Drawer
    edge="left"
    :open="open"
    :fillets="['left-above', 'left-below']"
    :style="style"
    @size="s => (height = s.height)"
    @enter="cancelClose('popout')"
    @leave="closePopout()"
  >
    <div class="popout">
      <Transition name="swap" mode="out-in">
        <component :is="views[shown]" v-if="shown" :key="`${shown}-${data}`" :data="data" :active="open" />
      </Transition>
    </div>
  </Drawer>
</template>

<style scoped>
.popout {
  padding: 14px 14px 14px 10px;
}
</style>
