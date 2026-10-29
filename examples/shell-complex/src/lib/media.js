import { computed, ref, watch } from "vue";
import { player, players } from "./services";
import { now } from "./system";
import { artSrc, ww } from "./ww";

let fetchedAt = ref(Date.now());
let offset = ref(null);

watch(players, () => {
  fetchedAt.value = Date.now();
  offset.value = null;
});

export let position = computed(() => {
  let p = player.value;
  if (!p) return 0;
  let base = offset.value ?? p.position;
  let elapsed = p.playing ? (now.value - fetchedAt.value) / 1000 : 0;
  return Math.min(p.length > 0 ? p.length : Infinity, base + Math.max(0, elapsed));
});

export let progress = computed(() => {
  let p = player.value;
  return p && p.length > 0 ? position.value / p.length : 0;
});

export let art = computed(() => artSrc(player.value?.art));

export function seek(ratio) {
  let p = player.value;
  if (!p?.canSeek || !(p.length > 0)) return;
  let target = ratio * p.length;
  ww.media.setPosition(p.id, target);
  offset.value = target;
  fetchedAt.value = Date.now();
}

export function playPause() {
  if (player.value) ww.media.playPause(player.value.id);
}

export function next() {
  if (player.value?.canNext) ww.media.next(player.value.id);
}

export function previous() {
  if (player.value?.canPrev) ww.media.previous(player.value.id);
}
