import { reactive } from "vue";
import { monitor, quiet, storeRef, ww } from "./ww";

export let geometry = { bar: 48, border: 10, radius: 22 };

export let keys = {
  launcher: `complex.launcher.${monitor}`,
  dashboard: `complex.dashboard.${monitor}`,
  session: `complex.session.${monitor}`,
  dnd: "complex.dnd",
};

export let launcherOpen = storeRef(keys.launcher, false);
export let dashboardOpen = storeRef(keys.dashboard, false);
export let sessionOpen = storeRef(keys.session, false);
export let dnd = storeRef(keys.dnd, false);

export let ui = reactive({
  popout: null,
  popoutY: 0,
  popoutData: null,
  osd: false,
});

let panels = ["launcher", "dashboard", "session"];

export function setPanel(name, open) {
  if (open) {
    ui.popout = null;
    for (let other of panels) if (other != name) ww.state.set(keys[other], false);
  }
  return ww.state.set(keys[name], open);
}

export async function togglePanel(name) {
  let current = await quiet(ww.state.get(keys[name]));
  return setPanel(name, !current);
}

let timers = new Map();

export function cancelClose(id) {
  clearTimeout(timers.get(id));
}

export function later(id, fn, delay) {
  clearTimeout(timers.get(id));
  timers.set(id, setTimeout(fn, delay));
}

export function openPopout(name, el, data) {
  cancelClose("popout");
  let rect = el.getBoundingClientRect();
  ui.popout = name;
  ui.popoutY = rect.top + rect.height / 2;
  ui.popoutData = data ?? null;
}

export function closePopout(delay = 300) {
  later("popout", () => (ui.popout = null), delay);
}
