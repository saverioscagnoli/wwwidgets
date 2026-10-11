"use strict";

// Commands for the session menu and launcher actions. Tuned for labwc.
const SESSION = {
  lock: ["loginctl", "lock-session"],
  logout: ["labwc", "--exit"],
  suspend: ["systemctl", "suspend"],
  reboot: ["systemctl", "reboot"],
  shutdown: ["systemctl", "poweroff"],
};

const HERE = decodeURIComponent(new URL(".", location.href).pathname);
const w = window.wwwidgets;

// ------------------------------------------------------------------ helpers

const $ = (sel, root = document) => root.querySelector(sel);
const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

function h(tag, props = {}, ...children) {
  const el = document.createElement(tag);

  for (const [k, v] of Object.entries(props)) {
    if (v == null || v === false) continue;
    if (k.startsWith("on")) el.addEventListener(k.slice(2), v);
    else if (k === "class") el.className = v;
    else if (k === "style") el.style.cssText = v;
    else if (k in el && typeof v !== "string") el[k] = v;
    else el.setAttribute(k, v === true ? "" : v);
  }

  el.append(...children.flat().filter((c) => c != null && c !== false));
  return el;
}

const icon = (name, cls = "") => h("span", { class: `icon ${cls}` }, name);
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));
const pad = (n) => String(n).padStart(2, "0");

const exec = (argv) => w.exec(argv).catch((e) => ({ stdout: "", stderr: String(e), code: -1 }));

function fmtDuration(seconds) {
  seconds = Math.max(0, Math.floor(seconds));
  const hh = Math.floor(seconds / 3600);
  const mm = Math.floor((seconds % 3600) / 60);
  const ss = seconds % 60;
  return hh ? `${hh}:${pad(mm)}:${pad(ss)}` : `${mm}:${pad(ss)}`;
}

function fmtUptime(seconds) {
  const d = Math.floor(seconds / 86400);
  const hh = Math.floor((seconds % 86400) / 3600);
  const mm = Math.floor((seconds % 3600) / 60);
  return [d && `${d}d`, hh && `${hh}h`, `${mm}m`].filter(Boolean).join(" ");
}

function stripMarkup(text) {
  return new DOMParser().parseFromString(text ?? "", "text/html").body.textContent ?? "";
}

function fileUrl(pathOrUrl) {
  if (!pathOrUrl) return null;
  if (/^[a-z]+:/.test(pathOrUrl)) return pathOrUrl;
  if (pathOrUrl.startsWith("/")) return "file://" + pathOrUrl.split("/").map(encodeURIComponent).join("/");
  return null;
}

// Lines from a long-running process. Restarts it if it dies.
async function follow(argv, onLine, retry = 5000) {
  let buf = "";

  try {
    await w.spawn(argv, {
      onStdout: (chunk) => {
        buf += chunk.endsWith("\n") ? chunk : chunk + "\n";
        let i;
        while ((i = buf.indexOf("\n")) >= 0) {
          const line = buf.slice(0, i);
          buf = buf.slice(i + 1);
          if (line) onLine(line);
        }
      },
      onExit: () => retry && setTimeout(() => follow(argv, onLine, retry), retry),
    });
  } catch (e) {
    console.warn(`spawn ${argv[0]} failed:`, e);
  }
}

// ------------------------------------------------------------------ drawers

const drawers = new Map();
const backdrop = $("#backdrop");

class Drawer {
  constructor(id, { keyboard = "on-demand", onOpen, onClose } = {}) {
    this.id = id;
    this.el = $(`#${id}`);
    this.keyboard = keyboard;
    this.onOpen = onOpen;
    this.onClose = onClose;
    this.pinned = false;
    this.timer = null;
    drawers.set(id, this);
  }

  get isOpen() {
    return this.el.classList.contains("open");
  }

  // Pinned drawers stay open until toggled, Escape, or a click outside.
  open(pinned = false) {
    clearTimeout(this.timer);
    this.pinned ||= pinned;

    if (!this.isOpen) {
      this.el.classList.add("open", "hit");
      this.onOpen?.();
    }

    syncModal();
  }

  close() {
    clearTimeout(this.timer);
    if (!this.isOpen) return;

    this.pinned = false;
    this.el.classList.remove("open", "hit");
    this.onClose?.();
    syncModal();

    if (ipcOpen === this.id) w.state.set("caelestia.open", null);
  }

  toggle() {
    this.isOpen && this.pinned ? this.close() : this.open(true);
  }

  closeSoon(delay = 300) {
    if (this.pinned) return;
    clearTimeout(this.timer);
    this.timer = setTimeout(() => this.close(), delay);
  }

  // Open while the pointer is over any of the triggers or the drawer itself.
  hover(...triggers) {
    for (const t of triggers) {
      t.addEventListener("mouseenter", () => this.open());
      t.addEventListener("mouseleave", () => this.closeSoon());
    }

    this.el.addEventListener("mouseenter", () => clearTimeout(this.timer));
    this.el.addEventListener("mouseleave", () => this.closeSoon());
    return this;
  }
}

let keyboardMode = "none";

function syncModal() {
  const pinned = [...drawers.values()].filter((d) => d.isOpen && d.pinned);
  backdrop.classList.toggle("hit", pinned.length > 0);

  const mode = pinned.some((d) => d.keyboard === "exclusive")
    ? "exclusive"
    : pinned.some((d) => d.keyboard === "on-demand")
      ? "on-demand"
      : "none";

  if (mode !== keyboardMode) {
    keyboardMode = mode;
    w.window.setKeyboard(mode);
  }
}

backdrop.addEventListener("mousedown", () => {
  for (const d of drawers.values()) if (d.pinned) d.close();
});

document.addEventListener("keydown", (e) => {
  if (e.key !== "Escape") return;
  for (const d of drawers.values()) if (d.pinned) d.close();
});

// ------------------------------------------------------------------ ipc + monitor

let connector = null;
let pointerMonitor = null;
let ipcOpen = null;

async function setupIpc() {
  connector = (await w.monitors.get()).connector;

  w.state.onChange("caelestia.monitor", (m) => (pointerMonitor = m));

  document.addEventListener("pointerover", () => {
    if (pointerMonitor !== connector) {
      pointerMonitor = connector;
      w.state.set("caelestia.monitor", connector);
    }
  });

  // `wwwidgets set caelestia.open '"launcher"'` opens a drawer on the monitor
  // the pointer was last on; null closes them.
  w.state.onChange("caelestia.open", (name) => {
    ipcOpen = name;
    const here = !pointerMonitor || pointerMonitor === connector;

    for (const d of drawers.values()) {
      if (d.id === name && here) d.open(true);
      else if (d.pinned) d.close();
    }
  });
}

// Everything tagged .hit receives input; the rest of the window is click-through.
w.window.inputRegionFrom(".hit");

// ------------------------------------------------------------------ bar: clock

function tickClock() {
  const now = new Date();
  $("#bar-clock").replaceChildren(icon("schedule"), h("span", {}, pad(now.getHours())), h("span", {}, pad(now.getMinutes())));

  if (dashboard.isOpen) {
    $("#dash-time").textContent = `${pad(now.getHours())}:${pad(now.getMinutes())}`;
    $("#dash-date").textContent = now.toLocaleDateString(undefined, { weekday: "long", day: "numeric", month: "long" });
  }
}

// ------------------------------------------------------------------ bar: workspaces

function renderWorkspaces(list) {
  const box = $("#bar-workspaces");
  const indicator = $(".ws-indicator", box);
  const visible = list.filter((ws) => !ws.hidden && (ws.outputs.length === 0 || ws.outputs.includes(connector)));

  const buttons = visible.map((ws) =>
    h(
      "button",
      {
        class: [ws.active && "active", ws.urgent && "urgent"].filter(Boolean).join(" "),
        disabled: !ws.canActivate,
        onclick: () => w.workspaces.activate(ws.id),
      },
      (ws.name || ws.id).replace(/^\D*(?=\d)/, ""),
    ),
  );

  box.replaceChildren(indicator, ...buttons);

  const active = visible.findIndex((ws) => ws.active);
  indicator.style.opacity = active < 0 ? 0 : 1;
  indicator.style.transform = `translateY(${Math.max(active, 0) * 28}px)`;
}

$("#bar-workspaces").addEventListener("wheel", (e) => {
  const buttons = $$("#bar-workspaces button");
  const i = buttons.findIndex((b) => b.classList.contains("active"));
  const next = buttons[clamp(i + Math.sign(e.deltaY), 0, buttons.length - 1)];
  if (next && next !== buttons[i]) next.click();
});

// ------------------------------------------------------------------ bar: tray

function renderTray(items) {
  $("#bar-tray").replaceChildren(
    ...items.map((item) => {
      const src = item.status === "NeedsAttention" ? item.attentionIcon ?? item.icon : item.icon;

      const button = h(
        "button",
        {
          class: item.status === "NeedsAttention" ? "attention" : "",
          title: item.tooltip || item.title,
          onclick: () => (item.itemIsMenu ? showTrayMenu(item, button) : w.tray.activate(item.id)),
          onauxclick: (e) => e.button === 1 && w.tray.secondary(item.id),
          oncontextmenu: (e) => {
            e.preventDefault();
            showTrayMenu(item, button);
          },
          onwheel: (e) => w.tray.scroll(item.id, Math.sign(e.deltaY)),
        },
        src ? h("img", { src }) : icon("apps"),
      );

      return button;
    }),
  );
}

async function showTrayMenu(item, anchor) {
  let root;

  try {
    root = await w.tray.menu(item.id);
  } catch {
    root = null;
  }

  if (!root?.children?.length) {
    w.tray.contextMenu(item.id);
    return;
  }

  const build = (nodes) =>
    nodes.flatMap((node) => {
      if (node.type === "separator") return [h("div", { class: "menu-sep" })];

      const check =
        node.toggleType === "checkmark"
          ? icon(node.toggleState === 1 ? "check_box" : "check_box_outline_blank")
          : node.toggleType === "radio"
            ? icon(node.toggleState === 1 ? "radio_button_checked" : "radio_button_unchecked")
            : icon("");

      const row = h(
        "button",
        {
          class: "menu-item",
          disabled: !node.enabled,
          onclick: () => {
            if (node.children.length) return;
            w.tray.click(item.id, node.id);
            popout.close();
          },
        },
        check,
        h("span", {}, node.label),
        node.children.length ? icon("chevron_right") : null,
      );

      return node.children.length ? [row, h("div", { class: "menu-sub" }, build(node.children))] : [row];
    });

  showPopout("tray", anchor, () => [h("div", { class: "list" }, build(root.children))], { pinned: true, compact: true });
}

// ------------------------------------------------------------------ bar popouts

const popout = new Drawer("popout", {
  onClose: () => {
    popoutKind = null;
  },
});

let popoutKind = null;
let popoutAnchor = null;
let popoutRender = null;
let popoutBusy = false; // a slider drag or text entry is in progress

function showPopout(kind, anchor, render, { pinned = false, compact = false } = {}) {
  popoutKind = kind;
  popoutAnchor = anchor;
  popoutRender = render;

  const content = $("#popout-content");
  content.classList.toggle("compact", compact);
  content.replaceChildren(...render());

  placePopout();
  popout.open(pinned);
}

function refreshPopout(kind) {
  if (popoutKind !== kind || popoutBusy || !popout.isOpen) return;
  $("#popout-content").replaceChildren(...popoutRender());
  placePopout();
}

function placePopout() {
  const el = popout.el;
  const r = popoutAnchor.getBoundingClientRect();
  const height = el.offsetHeight;
  const top = clamp(r.top + r.height / 2 - height / 2, 0, innerHeight - height);

  el.style.top = `${Math.round(top)}px`;
}

// The input region is computed from layout, so re-sync once a move finishes.
popout.el.addEventListener("transitionend", (e) => {
  if (e.target === popout.el) popout.el.dataset.settled = e.timeStamp;
});

const renderers = {
  audio: renderAudioPopout,
  network: renderNetworkPopout,
  bluetooth: renderBluetoothPopout,
  battery: renderBatteryPopout,
};

for (const el of $$("[data-popout]")) {
  const kind = el.dataset.popout;
  el.addEventListener("mouseenter", () => {
    if (popout.pinned && popoutKind !== kind) popout.close();
    showPopout(kind, el, renderers[kind]);
  });
  el.addEventListener("mouseleave", () => popout.closeSoon());
  el.addEventListener("click", () => {
    showPopout(kind, el, renderers[kind], { pinned: true });
  });
}

popout.el.addEventListener("mouseenter", () => clearTimeout(popout.timer));
popout.el.addEventListener("mouseleave", () => popout.closeSoon());

function slider(value, max, onInput) {
  const input = h("input", {
    type: "range",
    class: "slider",
    min: 0,
    max: 100,
    step: 1,
  });

  const paint = () => input.style.setProperty("--p", `${(input.value / 100) * 100}%`);

  input.value = Math.round((value / max) * 100);
  paint();

  input.addEventListener("pointerdown", () => (popoutBusy = true));
  input.addEventListener("pointerup", () => (popoutBusy = false));
  input.addEventListener("input", () => {
    paint();
    onInput((input.value / 100) * max);
  });

  return input;
}

function toggleSwitch(on, onToggle) {
  return h("button", { class: `switch ${on ? "on" : ""}`, onclick: () => onToggle(!on) });
}

// ------------------------------------------------------------------ audio

let audio = null;
let lastVolume = null;

function defaultSink() {
  return audio?.sinks.find((s) => s.id === audio.default) ?? audio?.sinks[0] ?? null;
}

function volumeIcon(sink) {
  if (!sink) return "volume_off";
  if (sink.muted) return "volume_off";
  if (sink.volume < 0.01) return "volume_mute";
  if (sink.volume < 0.5) return "volume_down";
  return "volume_up";
}

function onAudio(state) {
  audio = state;
  const sink = defaultSink();
  $("#st-audio").textContent = volumeIcon(sink);

  if (sink) {
    const key = `${sink.id}:${sink.volume.toFixed(3)}:${sink.muted}`;
    if (lastVolume !== null && lastVolume !== key && lastVolume.startsWith(`${sink.id}:`)) showOsd();
    lastVolume = key;
  }

  renderOsd();
  refreshPopout("audio");
}

function renderAudioPopout() {
  const sink = defaultSink();

  if (!sink) return [h("div", { class: "popout-head" }, "No audio output")];

  return [
    h(
      "div",
      { class: "popout-head" },
      h("span", {}, "Volume"),
      h("button", { class: "chip", onclick: () => w.audio.setMute(sink.id, !sink.muted) }, icon(volumeIcon(sink), "small"), sink.muted ? "Unmute" : "Mute"),
    ),
    h("div", { class: "muted", style: "margin-bottom: 10px; font-size: 13px" }, sink.description),
    h(
      "div",
      { class: "row" },
      slider(Math.min(sink.volume, 1), 1, (v) => w.audio.setVolume(sink.id, v)),
      h("span", { style: "width: 3em; text-align: right" }, `${Math.round(sink.volume * 100)}%`),
    ),
    audio.sinks.length > 1
      ? h(
          "div",
          { class: "list", style: "margin-top: 12px" },
          audio.sinks.map((s) =>
            h("div", { class: `list-item ${s.id === sink.id ? "active" : ""}` }, icon("speaker"), h("span", { class: "grow" }, s.description), h("span", { class: "meta" }, `${Math.round(s.volume * 100)}%`)),
          ),
        )
      : null,
  ];
}

// ------------------------------------------------------------------ network

let network = null;
let wifiPasswordFor = null;

function wifiIcon(strength) {
  if (strength >= 80) return "signal_wifi_4_bar";
  if (strength >= 60) return "network_wifi_3_bar";
  if (strength >= 40) return "network_wifi_2_bar";
  if (strength >= 20) return "network_wifi_1_bar";
  return "signal_wifi_0_bar";
}

function networkIcon() {
  if (!network) return "wifi_off";

  const wired = network.devices.find((d) => d.type === "ethernet" && d.state === "connected");
  if (wired) return "lan";

  const active = network.networks.find((n) => n.active);
  if (active) return wifiIcon(active.strength);

  // Connected without an active wifi network: wired, possibly unmanaged by NM.
  if (network.state === "connected") return "lan";

  if (!network.wifiEnabled) return "wifi_off";
  return network.state === "connecting" ? "network_wifi" : "signal_wifi_statusbar_not_connected";
}

function onNetwork(state) {
  network = state;
  $("#st-network").textContent = networkIcon();
  refreshPopout("network");
}

function renderNetworkPopout() {
  if (!network) return [h("div", { class: "popout-head" }, "Network unavailable")];

  const seen = new Set();
  const nets = network.networks
    .filter((n) => n.ssid && !seen.has(n.ssid) && seen.add(n.ssid))
    .sort((a, b) => b.active - a.active || b.known - a.known || b.strength - a.strength)
    .slice(0, 10);

  const wired = network.devices.filter((d) => d.type === "ethernet");

  return [
    h(
      "div",
      { class: "popout-head" },
      h("span", {}, "Wi-Fi"),
      toggleSwitch(network.wifiEnabled, (on) => w.network.setWifiEnabled(on)),
    ),
    wired.map((d) =>
      h("div", { class: `list-item ${d.state === "connected" ? "active" : ""}` }, icon("lan"), h("span", { class: "grow" }, d.connection ?? d.interface), h("span", { class: "meta" }, d.state)),
    ),
    network.wifiEnabled
      ? h(
          "div",
          { class: "list" },
          nets.map((n) => {
            const row = h(
              "button",
              {
                class: `list-item ${n.active ? "active" : ""}`,
                onclick: () => {
                  if (n.active) w.network.disconnect(n.id);
                  else if (n.secure && !n.known) {
                    wifiPasswordFor = n.ssid;
                    popoutBusy = false;
                    showPopout("network", popoutAnchor, renderNetworkPopout, { pinned: true });
                    setTimeout(() => $("#wifi-password")?.focus(), 50);
                  } else w.network.connect(n.id).catch(console.warn);
                },
              },
              icon(wifiIcon(n.strength)),
              h("span", { class: "grow" }, n.ssid),
              n.secure ? icon("lock", "small") : null,
              n.active ? h("span", { class: "meta" }, "Connected") : null,
            );

            if (wifiPasswordFor !== n.ssid) return row;

            const input = h("input", { id: "wifi-password", type: "password", placeholder: "Password" });
            const submit = () => {
              w.network.connect(n.id, input.value).catch(console.warn);
              wifiPasswordFor = null;
              popoutBusy = false;
              refreshPopout("network");
            };

            input.addEventListener("focus", () => (popoutBusy = true));
            input.addEventListener("keydown", (e) => e.key === "Enter" && submit());

            return h("div", {}, row, h("div", { class: "field" }, input, h("button", { class: "chip", onclick: submit }, "Connect")));
          }),
        )
      : null,
    network.wifiEnabled
      ? h("div", { style: "margin-top: 10px" }, h("button", { class: "chip", onclick: () => w.network.scan() }, icon("refresh", "small"), "Rescan"))
      : null,
  ];
}

// ------------------------------------------------------------------ bluetooth

let bluetooth = null;

function onBluetooth(state) {
  bluetooth = state;
  const adapter = state.adapters[0];
  const connected = state.devices.some((d) => d.connected);

  $("#st-bluetooth").textContent = !adapter?.powered ? "bluetooth_disabled" : connected ? "bluetooth_connected" : "bluetooth";
  $("#st-bluetooth").hidden = !adapter;

  if (state.request) {
    const anchor = $("#st-bluetooth");
    showPopout("bluetooth", anchor, renderBluetoothPopout, { pinned: true });
  }

  refreshPopout("bluetooth");
}

function deviceIcon(d) {
  const map = {
    "audio-headset": "headset_mic",
    "audio-headphones": "headphones",
    "audio-card": "speaker",
    "input-mouse": "mouse",
    "input-keyboard": "keyboard",
    "input-gaming": "sports_esports",
    phone: "smartphone",
    computer: "computer",
  };
  return map[d.icon] ?? "bluetooth";
}

function renderBluetoothPopout() {
  const adapter = bluetooth?.adapters[0];
  if (!adapter) return [h("div", { class: "popout-head" }, "No Bluetooth adapter")];

  const req = bluetooth.request;
  const devices = bluetooth.devices
    .filter((d) => d.adapter === adapter.id && (d.paired || adapter.discovering))
    .sort((a, b) => b.connected - a.connected || b.paired - a.paired || a.name.localeCompare(b.name));

  return [
    h("div", { class: "popout-head" }, h("span", {}, "Bluetooth"), toggleSwitch(adapter.powered, (on) => w.bluetooth.setPowered(adapter.id, on))),
    req
      ? h(
          "div",
          { class: "card", style: "margin-bottom: 10px" },
          h("div", {}, `${req.name ?? "A device"} wants to pair`),
          req.code ? h("div", { class: "big-time", style: "font-size: 28px; margin: 6px 0" }, req.code) : null,
          h("div", { class: "row", style: "margin-top: 8px" }, h("button", { class: "chip", onclick: () => w.bluetooth.respond(true) }, "Accept"), h("button", { class: "chip", onclick: () => w.bluetooth.respond(false) }, "Decline")),
        )
      : null,
    adapter.powered
      ? h(
          "div",
          { class: "list" },
          devices.length === 0 ? h("div", { class: "muted" }, "No devices") : null,
          devices.map((d) =>
            h(
              "button",
              {
                class: `list-item ${d.connected ? "active" : ""}`,
                onclick: () => (d.connected ? w.bluetooth.disconnect(d.id) : d.paired ? w.bluetooth.connect(d.id) : w.bluetooth.pair(d.id)).catch(console.warn),
              },
              icon(deviceIcon(d)),
              h("span", { class: "grow" }, d.name || d.address),
              d.battery != null ? h("span", { class: "meta" }, `${d.battery}%`) : null,
              h("span", { class: "meta" }, d.connected ? "Connected" : d.paired ? "" : "Pair"),
            ),
          ),
        )
      : null,
    adapter.powered
      ? h(
          "div",
          { style: "margin-top: 10px" },
          h("button", { class: "chip", onclick: () => w.bluetooth.setDiscovering(adapter.id, !adapter.discovering) }, icon(adapter.discovering ? "stop" : "search", "small"), adapter.discovering ? "Stop scanning" : "Scan"),
        )
      : null,
  ];
}

// ------------------------------------------------------------------ battery

let battery = null;

function batteryIcon(b) {
  if (b.state === "charging") return "battery_charging_full";
  if (b.state === "fully-charged" || b.percentage >= 95) return "battery_full";
  return `battery_${clamp(Math.floor(b.percentage / 14.3), 0, 6)}_bar`;
}

function onBattery(state) {
  battery = state;
  const el = $("#st-battery");
  el.hidden = !state.present;
  el.textContent = batteryIcon(state);
  el.style.color = state.warning === "low" || state.warning === "critical" ? "var(--error)" : "";
  refreshPopout("battery");
}

function renderBatteryPopout() {
  if (!battery?.present) return [h("div", { class: "popout-head" }, "No battery")];

  const remaining = battery.state === "charging" ? battery.timeToFull : battery.timeToEmpty;
  const label = battery.state === "charging" ? "until full" : "remaining";

  return [
    h(
      "div",
      { class: "battery-big" },
      icon(batteryIcon(battery), "fill"),
      h(
        "div",
        {},
        h("div", { class: "pct" }, `${Math.round(battery.percentage)}%`),
        h("div", { class: "muted" }, battery.state.replace(/-/g, " ")),
        remaining ? h("div", { class: "muted" }, `${fmtUptime(remaining)} ${label}`) : null,
        battery.energyRate ? h("div", { class: "muted" }, `${battery.energyRate.toFixed(1)} W`) : null,
      ),
    ),
  ];
}

// ------------------------------------------------------------------ osd (volume + brightness)

const osd = new Drawer("osd");
osd.hover($("#osd-trigger"));

let brightness = null; // 0..1
let osdTimer = null;

function showOsd() {
  if (osd.isOpen && !osdTimer) return; // opened by hover; leave it alone
  osd.open();
  clearTimeout(osdTimer);
  osdTimer = setTimeout(() => {
    osdTimer = null;
    if (!osd.el.matches(":hover")) osd.close();
  }, 1500);
}

function renderOsd() {
  const sink = defaultSink();
  const vol = $("#osd-volume");
  vol.style.setProperty("--p", sink && !sink.muted ? Math.min(sink.volume, 1) * 100 : 0);
  $(".icon", vol).textContent = volumeIcon(sink);

  const bri = $("#osd-brightness");
  bri.hidden = brightness == null;
  bri.style.setProperty("--p", (brightness ?? 0) * 100);
}

function dragSlider(el, onValue) {
  const set = (e) => {
    const r = el.getBoundingClientRect();
    onValue(clamp((r.bottom - e.clientY) / r.height, 0, 1));
  };

  el.addEventListener("pointerdown", (e) => {
    el.setPointerCapture(e.pointerId);
    set(e);
  });
  el.addEventListener("pointermove", (e) => el.hasPointerCapture(e.pointerId) && set(e));
  el.addEventListener("wheel", (e) => {
    e.preventDefault();
    const current = parseFloat(el.style.getPropertyValue("--p")) / 100 || 0;
    onValue(clamp(current - Math.sign(e.deltaY) * 0.05, 0, 1));
  });
}

dragSlider($("#osd-volume"), (v) => {
  const sink = defaultSink();
  if (sink) w.audio.setVolume(sink.id, v);
});

dragSlider($("#osd-brightness"), (v) => {
  brightness = v;
  renderOsd();
  exec(["brightnessctl", "-q", "set", `${Math.max(1, Math.round(v * 100))}%`]);
});

function watchBrightness() {
  const script = `d=$(ls -d /sys/class/backlight/* 2>/dev/null | head -n1)
[ -n "$d" ] || exit 0
read m < "$d/max_brightness"
p=
while :; do
  read b < "$d/brightness"
  [ "$b" != "$p" ] && echo "$b $m" && p=$b
  sleep 0.5
done`;

  let first = true;

  follow(["sh", "-c", script], (line) => {
    const [b, m] = line.split(" ").map(Number);
    if (!m) return;
    brightness = b / m;
    renderOsd();
    if (!first) showOsd();
    first = false;
  }, 0);
}

// ------------------------------------------------------------------ notifications

const notifDrawer = new Drawer("notifications");
const notifTimers = new Map();
const notifSticky = new Set();
const NOTIF_TIMEOUT = 6000;

function notifIcon(n) {
  const url = fileUrl(n.icon);
  if (url) return h("div", { class: "notif-icon" }, h("img", { src: url }));
  return h("div", { class: "notif-icon" }, icon(n.urgency === 2 ? "release_alert" : "notifications", "fill"));
}

function renderNotifications(list) {
  const box = $("#notif-list");
  const ids = new Set(list.map((n) => n.id));

  for (const [id, t] of notifTimers) {
    if (!ids.has(id)) {
      clearTimeout(t);
      notifTimers.delete(id);
      notifSticky.delete(id);
    }
  }

  // Keep existing cards so their entry animation does not replay.
  const existing = new Map($$(".notif", box).map((el) => [Number(el.dataset.id), el]));

  const cards = list
    .slice()
    .reverse()
    .slice(0, 5)
    .map((n) => {
      if (!notifTimers.has(n.id)) {
        const timeout = n.timeout > 0 ? n.timeout : NOTIF_TIMEOUT;
        if (n.urgency !== 2 && n.timeout !== 0) notifTimers.set(n.id, setTimeout(() => w.notifications.dismiss(n.id), timeout));
        else {
          notifTimers.set(n.id, null);
          notifSticky.add(n.id);
        }
      }

      const old = existing.get(n.id);
      const card = h(
        "div",
        {
          class: `notif ${n.urgency === 2 ? "critical" : ""}`,
          "data-id": n.id,
          onclick: () => {
            if (n.actions.some((a) => a.key === "default")) w.notifications.invoke(n.id);
            w.notifications.dismiss(n.id);
          },
        },
        notifIcon(n),
        h(
          "div",
          { class: "notif-body" },
          h(
            "div",
            { class: "notif-top" },
            h("span", {}, `${n.app || "Notification"} • ${new Date(n.time).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })}`),
            h(
              "button",
              {
                class: "notif-close",
                onclick: (e) => {
                  e.stopPropagation();
                  w.notifications.dismiss(n.id);
                },
              },
              icon("close"),
            ),
          ),
          h("div", { class: "notif-summary" }, stripMarkup(n.summary)),
          n.body ? h("div", { class: "notif-text" }, stripMarkup(n.body)) : null,
          n.actions.filter((a) => a.key !== "default").length
            ? h(
                "div",
                { class: "notif-actions" },
                n.actions
                  .filter((a) => a.key !== "default")
                  .map((a) =>
                    h(
                      "button",
                      {
                        class: "chip",
                        onclick: (e) => {
                          e.stopPropagation();
                          w.notifications.invoke(n.id, a.key);
                          w.notifications.dismiss(n.id);
                        },
                      },
                      a.label,
                    ),
                  ),
              )
            : null,
        ),
      );

      if (old) card.style.animation = "none";
      return card;
    });

  if (cards.length) {
    box.replaceChildren(...cards);
    notifDrawer.open();
  } else {
    notifDrawer.close();
  }
}

// Hovering the popups pauses their expiry so they can be read.
notifDrawer.el.addEventListener("mouseenter", () => {
  for (const [id, t] of notifTimers) {
    clearTimeout(t);
    notifTimers.set(id, null);
  }
});
notifDrawer.el.addEventListener("mouseleave", () => {
  for (const id of notifTimers.keys()) {
    if (!notifSticky.has(id)) notifTimers.set(id, setTimeout(() => w.notifications.dismiss(id), 3000));
  }
});

// ------------------------------------------------------------------ media (MPRIS)

let media = null;
let mediaAt = 0;

function mediaPosition() {
  if (!media) return 0;
  const pos = media.position / 1e6;
  return media.status === "Playing" ? pos + (performance.now() - mediaAt) / 1000 : pos;
}

function mediaCall(method) {
  if (!media) return;
  exec(["gdbus", "call", "--session", "--dest", media.bus, "--object-path", "/org/mpris/MediaPlayer2", "--method", `org.mpris.MediaPlayer2.Player.${method}`]);
}

function cover(cls = "") {
  const art = fileUrl(media?.art);
  const el = h("div", { class: `cover ${cls} ${media?.status === "Playing" ? "playing" : ""}` }, art ? null : icon("music_note"));
  if (art) el.style.backgroundImage = `url("${art}")`;
  return el;
}

function renderMedia() {
  if (!dashboard.isOpen) return;

  $("#dash-mini-media").replaceChildren(
    cover("small"),
    h(
      "div",
      { style: "min-width: 0; flex: 1" },
      h("div", { class: "media-title", style: "font-size: 15px" }, media?.title || "No media"),
      h("div", { class: "muted", style: "white-space: nowrap; overflow: hidden; text-overflow: ellipsis; font-size: 13px" }, media?.artist || media?.identity || "Nothing is playing"),
    ),
    media ? h("button", { onclick: () => mediaCall("PlayPause") }, icon(media.status === "Playing" ? "pause" : "play_arrow", "fill")) : null,
  );

  const box = $("#dash-media");

  if (!media) {
    box.replaceChildren(cover(), h("div", { class: "media-info" }, h("div", { class: "media-title" }, "No media playing"), h("div", { class: "media-artist" }, "Start something in an MPRIS-capable player")));
    return;
  }

  const length = media.length / 1e6;
  const pos = Math.min(mediaPosition(), length || Infinity);
  const playing = media.status === "Playing";

  box.replaceChildren(
    cover(),
    h(
      "div",
      { class: "media-info" },
      h("div", { class: "media-title" }, media.title || "Unknown title"),
      h("div", { class: "media-artist" }, [media.artist, media.album].filter(Boolean).join(" — ") || media.identity),
      length ? h("div", { class: "progress" }, h("div", { style: `width: ${(pos / length) * 100}%` })) : null,
      length ? h("div", { class: "media-time" }, h("span", {}, fmtDuration(pos)), h("span", {}, fmtDuration(length))) : null,
      h(
        "div",
        { class: "media-controls" },
        h("button", { disabled: !media.canPrevious, onclick: () => mediaCall("Previous") }, icon("skip_previous", "fill")),
        h("button", { class: `play ${playing ? "playing" : ""}`, onclick: () => mediaCall("PlayPause") }, icon(playing ? "pause" : "play_arrow", "fill")),
        h("button", { disabled: !media.canNext, onclick: () => mediaCall("Next") }, icon("skip_next", "fill")),
        h("span", { class: "muted", style: "margin-left: 8px" }, media.identity),
      ),
    ),
  );
}

function watchMedia() {
  follow(["python3", HERE + "mpris.py"], (line) => {
    try {
      media = JSON.parse(line);
    } catch {
      return;
    }
    mediaAt = performance.now();
    renderMedia();
  });
}

// ------------------------------------------------------------------ system resources

let prevCpu = null;
let stats = null;

async function pollStats() {
  const script = `head -n1 /proc/stat
grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree):' /proc/meminfo
df -B1 --output=size,used / | tail -n1
for f in /sys/class/hwmon/hwmon*; do n=$(cat "$f/name" 2>/dev/null); case "$n" in k10temp|coretemp|zenpower|cpu_thermal|acpitz) cat "$f/temp1_input"; break;; esac; done`;

  const { stdout } = await exec(["sh", "-c", script]);
  const lines = stdout.trim().split("\n");

  const cpu = lines[0].split(/\s+/).slice(1).map(Number);
  const idle = cpu[3] + (cpu[4] || 0);
  const total = cpu.reduce((a, b) => a + b, 0);
  let cpuPct = 0;
  if (prevCpu) cpuPct = 1 - (idle - prevCpu.idle) / Math.max(1, total - prevCpu.total);
  prevCpu = { idle, total };

  const mem = Object.fromEntries(lines.slice(1, 5).map((l) => [l.split(":")[0], Number(l.match(/\d+/)[0]) * 1024]));
  const [diskSize, diskUsed] = (lines[5] ?? "0 0").trim().split(/\s+/).map(Number);
  const temp = lines[6] ? Number(lines[6]) / 1000 : null;

  stats = {
    cpu: clamp(cpuPct, 0, 1),
    memUsed: mem.MemTotal - mem.MemAvailable,
    memTotal: mem.MemTotal,
    swapUsed: mem.SwapTotal - mem.SwapFree,
    swapTotal: mem.SwapTotal,
    diskUsed,
    diskSize,
    temp,
  };

  renderStats();
}

const GiB = 1024 ** 3;

function gauge(value, iconName, label, sub, size = 92) {
  const stroke = 8;
  const r = (size - stroke) / 2;
  const c = 2 * Math.PI * r;
  const arc = c * 0.75;
  const svgNS = "http://www.w3.org/2000/svg";

  const svg = document.createElementNS(svgNS, "svg");
  svg.setAttribute("width", size);
  svg.setAttribute("height", size);

  for (const [cls, len] of [["track", arc], ["value", arc * clamp(value, 0, 1)]]) {
    const circle = document.createElementNS(svgNS, "circle");
    circle.setAttribute("class", cls);
    circle.setAttribute("cx", size / 2);
    circle.setAttribute("cy", size / 2);
    circle.setAttribute("r", r);
    circle.setAttribute("fill", "none");
    circle.setAttribute("stroke-width", stroke);
    circle.setAttribute("stroke-linecap", "round");
    circle.setAttribute("stroke-dasharray", `${len} ${c}`);
    svg.append(circle);
  }

  return h("div", { class: "gauge" }, svg, h("div", { class: "label" }, icon(iconName), h("span", {}, label), sub ? h("small", {}, sub) : null));
}

function renderStats() {
  if (!stats || !dashboard.isOpen) return;

  const memPct = stats.memUsed / stats.memTotal;
  const diskPct = stats.diskUsed / stats.diskSize;

  $("#dash-resources").replaceChildren(
    gauge(stats.cpu, "memory", `${Math.round(stats.cpu * 100)}%`, null, 80),
    gauge(memPct, "memory_alt", `${Math.round(memPct * 100)}%`, null, 80),
    gauge(diskPct, "hard_drive", `${Math.round(diskPct * 100)}%`, null, 80),
  );

  $("#dash-perf").replaceChildren(
    h("div", { class: "card" }, gauge(stats.cpu, "memory", `${Math.round(stats.cpu * 100)}%`, "CPU", 130)),
    h("div", { class: "card" }, gauge(memPct, "memory_alt", `${Math.round(memPct * 100)}%`, "Memory", 130), h("div", { class: "muted" }, `${(stats.memUsed / GiB).toFixed(1)} / ${(stats.memTotal / GiB).toFixed(1)} GiB`)),
    h("div", { class: "card" }, gauge(diskPct, "hard_drive", `${Math.round(diskPct * 100)}%`, "Disk /", 130), h("div", { class: "muted" }, `${(stats.diskUsed / GiB).toFixed(0)} / ${(stats.diskSize / GiB).toFixed(0)} GiB`)),
    stats.temp != null
      ? h("div", { class: "card" }, gauge(stats.temp / 100, "device_thermostat", `${Math.round(stats.temp)}°C`, "CPU temp", 130))
      : h("div", { class: "card" }, gauge(stats.swapTotal ? stats.swapUsed / stats.swapTotal : 0, "swap_horiz", stats.swapTotal ? `${Math.round((stats.swapUsed / stats.swapTotal) * 100)}%` : "—", "Swap", 130)),
  );
}

// ------------------------------------------------------------------ dashboard

let calMonth = new Date();
calMonth.setDate(1);

function renderCalendar() {
  const today = new Date();
  const year = calMonth.getFullYear();
  const month = calMonth.getMonth();
  const first = (new Date(year, month, 1).getDay() + 6) % 7; // Monday first
  const start = new Date(year, month, 1 - first);

  const days = [];
  for (let i = 0; i < 42; i++) {
    const d = new Date(start);
    d.setDate(start.getDate() + i);
    const isToday = d.toDateString() === today.toDateString();
    const weekend = d.getDay() === 0 || d.getDay() === 6;
    days.push(h("div", { class: ["day", d.getMonth() !== month && "other", isToday && "today", weekend && "weekend"].filter(Boolean).join(" ") }, d.getDate()));
  }

  const dow = [...Array(7)].map((_, i) => new Date(2024, 0, 1 + i).toLocaleDateString(undefined, { weekday: "narrow" }));

  const shift = (n) => {
    calMonth = new Date(year, month + n, 1);
    renderCalendar();
  };

  $("#dash-calendar").replaceChildren(
    h(
      "div",
      { class: "cal-head" },
      h("button", { onclick: () => shift(-1) }, icon("chevron_left")),
      h("span", {}, calMonth.toLocaleDateString(undefined, { month: "long", year: "numeric" })),
      h("button", { onclick: () => shift(1) }, icon("chevron_right")),
    ),
    h("div", { class: "cal-grid" }, dow.map((d) => h("div", { class: "dow" }, d)), days),
  );
}

let statsTimer = null;
let mediaTimer = null;

const dashboard = new Drawer("dashboard", {
  onOpen: async () => {
    tickClock();
    renderCalendar();
    renderMedia();
    renderStats();
    pollStats();
    statsTimer = setInterval(pollStats, 2000);
    mediaTimer = setInterval(() => media?.status === "Playing" && renderMedia(), 1000);
    moveTabIndicator();

    const { stdout } = await exec(["sh", "-c", "id -un; cat /proc/sys/kernel/hostname; cut -d' ' -f1 /proc/uptime"]);
    const [user, host, uptime] = stdout.trim().split("\n");
    $("#dash-user").textContent = user ?? "";
    $("#dash-host").textContent = host ?? "";
    $("#dash-uptime").textContent = `up ${fmtUptime(Number(uptime) || 0)}`;
  },
  onClose: () => {
    clearInterval(statsTimer);
    clearInterval(mediaTimer);
    calMonth = new Date();
    calMonth.setDate(1);
  },
});

dashboard.hover($("#dash-trigger"));

function moveTabIndicator() {
  const active = $("#dash-tabs button.active");
  const ind = $("#dash-tabs .tab-indicator");
  ind.style.width = `${active.offsetWidth}px`;
  ind.style.transform = `translateX(${active.offsetLeft}px)`;
}

for (const tab of $$("#dash-tabs button")) {
  tab.addEventListener("click", () => {
    $$("#dash-tabs button").forEach((b) => b.classList.toggle("active", b === tab));
    $$("#dashboard .tab").forEach((s) => (s.hidden = s.dataset.tab !== tab.dataset.tab));
    moveTabIndicator();
  });
}

// ------------------------------------------------------------------ launcher

const launcherInput = $("#launcher-input");
const launcherResults = $("#launcher-results");
let apps = [];
let results = [];
let selected = 0;

const ACTIONS = [
  { name: "Shutdown", desc: "Power off the computer", icon: "power_settings_new", run: () => exec(SESSION.shutdown) },
  { name: "Reboot", desc: "Restart the computer", icon: "cached", run: () => exec(SESSION.reboot) },
  { name: "Suspend", desc: "Suspend to RAM", icon: "bedtime", run: () => exec(SESSION.suspend) },
  { name: "Lock", desc: "Lock the session", icon: "lock", run: () => exec(SESSION.lock) },
  { name: "Logout", desc: "End the session", icon: "logout", run: () => exec(SESSION.logout) },
  { name: "Dashboard", desc: "Open the dashboard", icon: "dashboard", run: () => setTimeout(() => dashboard.open(true), 50) },
  { name: "Session", desc: "Open the session menu", icon: "settings_power", run: () => setTimeout(() => session.open(true), 50) },
];

let appsJson = null;

async function loadApps() {
  const { stdout, stderr, code } = await exec(["python3", HERE + "apps.py"]);
  if (code !== 0) {
    console.warn("apps.py failed:", stderr);
    return;
  }

  // Rebuilding the list (and its <img> icons) right after the launcher opens
  // makes WebKit drop a frame, so only re-render when the apps changed.
  if (stdout === appsJson) return;
  appsJson = stdout;

  try {
    apps = JSON.parse(stdout);
  } catch (e) {
    console.warn("apps.py returned bad JSON", e);
  }

  if (launcher.isOpen) search();
}

// Prefix > substring > in-order subsequence; shorter names win ties.
function score(query, text) {
  text = text.toLowerCase();
  if (text.startsWith(query)) return 300 - text.length;
  const i = text.indexOf(query);
  if (i >= 0) return 200 - i - text.length / 10;

  let t = 0;
  let gaps = 0;
  for (const ch of query) {
    const j = text.indexOf(ch, t);
    if (j < 0) return -1;
    gaps += j - t;
    t = j + 1;
  }
  return 100 - gaps - text.length / 10;
}

function rank(items, query, fields) {
  if (!query) return items;
  return items
    .map((item) => [item, Math.max(...fields(item).map((f) => (f ? score(query, f) : -1)))])
    .filter(([, s]) => s >= 0)
    .sort((a, b) => b[1] - a[1])
    .map(([item]) => item);
}

function calculate(expr) {
  if (!/^[\d\s+\-*/%().,^e]+$/i.test(expr)) return null;

  try {
    const value = Function(`"use strict"; return (${expr.replace(/\^/g, "**").replace(/,/g, ".")})`)();
    return typeof value === "number" && Number.isFinite(value) ? value : null;
  } catch {
    return null;
  }
}

function search() {
  const raw = launcherInput.value;
  const q = raw.trim().toLowerCase();

  if (raw.startsWith("=")) {
    const value = calculate(raw.slice(1).trim());
    results = [
      {
        name: value == null ? "Invalid expression" : String(+value.toPrecision(12)),
        desc: raw.slice(1).trim() || "Type a math expression",
        icon: "calculate",
        run: null,
      },
    ];
  } else if (raw.startsWith(">")) {
    results = rank(ACTIONS, q.slice(1).trim(), (a) => [a.name, a.desc]);
  } else {
    results = rank(apps, q, (a) => [a.name, a.id, ...a.keywords]).map((a) => ({
      name: a.name,
      desc: a.comment,
      image: fileUrl(a.icon),
      icon: "apps",
      run: () => exec(["gtk-launch", a.id]),
    }));
  }

  selected = 0;
  renderResults();
}

function renderResults() {
  const shown = results.slice(0, 50);

  launcherResults.replaceChildren(
    ...(shown.length
      ? shown.map((r, i) =>
          h(
            "li",
            {
              class: i === selected ? "selected" : "",
              onmousemove: () => {
                if (selected !== i) {
                  selected = i;
                  renderResults();
                }
              },
              onclick: () => runResult(r),
            },
            r.image ? h("img", { src: r.image }) : h("div", { class: "app-icon" }, icon(r.icon)),
            h("div", { class: "text" }, h("div", { class: "name" }, r.name), r.desc ? h("div", { class: "desc" }, r.desc) : null),
          ),
        )
      : [h("li", { class: "empty" }, apps.length ? "No results" : "Loading applications…")]),
  );

  launcherResults.children[selected]?.scrollIntoView({ block: "nearest" });
}

function runResult(r) {
  if (!r?.run) return;
  launcher.close();
  r.run();
}

const launcher = new Drawer("launcher", {
  keyboard: "exclusive",
  onOpen: () => {
    launcherInput.value = "";
    search();
    setTimeout(() => launcherInput.focus(), 50);
    loadApps();
  },
  onClose: () => launcherInput.blur(),
});

launcherInput.addEventListener("input", search);
launcherInput.addEventListener("keydown", (e) => {
  const n = Math.min(results.length, 50);

  if (e.key === "ArrowDown" || (e.key === "Tab" && !e.shiftKey)) {
    e.preventDefault();
    selected = n ? (selected + 1) % n : 0;
    renderResults();
  } else if (e.key === "ArrowUp" || (e.key === "Tab" && e.shiftKey)) {
    e.preventDefault();
    selected = n ? (selected - 1 + n) % n : 0;
    renderResults();
  } else if (e.key === "Enter") {
    runResult(results[selected]);
  }
});

$("#bar-logo").addEventListener("click", () => launcher.toggle());

// ------------------------------------------------------------------ session

const session = new Drawer("session", {
  keyboard: "exclusive",
  onOpen: () => setTimeout(() => $("#session button")?.focus(), 50),
});

for (const button of $$("#session button")) {
  button.addEventListener("click", () => {
    session.close();
    exec(SESSION[button.dataset.action]);
  });
}

$("#session").addEventListener("keydown", (e) => {
  const buttons = $$("#session button");
  const i = buttons.indexOf(document.activeElement);
  if (e.key === "ArrowDown") buttons[(i + 1) % buttons.length].focus();
  if (e.key === "ArrowUp") buttons[(i - 1 + buttons.length) % buttons.length].focus();
});

$("#bar-power").addEventListener("click", () => session.toggle());

// ------------------------------------------------------------------ start

(async () => {
  await setupIpc();

  tickClock();
  setInterval(tickClock, 1000);

  w.workspaces.onChange(renderWorkspaces);
  w.tray.onChange(renderTray);
  w.audio.onChange(onAudio);
  w.network.onChange(onNetwork);
  w.bluetooth.onChange(onBluetooth);
  w.battery.onChange(onBattery);
  w.state.onChange("notifications", renderNotifications);

  watchBrightness();
  watchMedia();
  renderOsd();

  // Have the app list ready before the launcher first opens, so the sheet
  // doesn't change height mid-slide. Opening still refreshes it.
  loadApps();
})();
