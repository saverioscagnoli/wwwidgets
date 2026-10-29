import { computed, ref } from "vue";
import { service, ww } from "./ww";

export let workspaces = service("workspace", []);
export let toplevels = service("toplevels", []);
export let trayItems = service("tray", []);
export let audio = service("audio", null);
export let network = service("network", null);
export let bluetooth = service("bluetooth", null);
export let battery = service("battery", null);
export let brightness = service("brightness", null);
export let notifications = service("notifications", []);
export let players = service("media", []);
export let clipboard = service("clipboard", []);

export let chosenPlayer = ref(null);

export let activeWindow = computed(() => toplevels.value.find(t => t.activated) ?? null);

export let player = computed(() => {
  let list = players.value.filter(p => p.title || p.playing);
  return (
    list.find(p => p.id == chosenPlayer.value) ??
    list.find(p => p.playing) ??
    list.find(p => p.status == "Paused") ??
    list[0] ??
    null
  );
});

export let activeAp = computed(() => network.value?.accessPoints.find(ap => ap.active) ?? null);

export let wired = computed(() => {
  let type = network.value?.primary?.type ?? "";
  return type.includes("ethernet") || type.includes("802-3");
});

export let btRequest = ref(null);

ww.bluetooth.onRequest(req => {
  btRequest.value = req.type == "cancel" ? null : req;
});
