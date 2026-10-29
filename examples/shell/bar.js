let logo = $("logo");
let workspacesEl = $("workspaces");
let tasksEl = $("tasks");
let titleEl = $("title");
let timeEl = $("time");
let dateEl = $("date");
let mediaEl = $("media");
let mediaText = $("media-text");
let trayEl = $("tray");
let statusEl = $("status");
let netEl = $("st-net");
let btEl = $("st-bt");
let volEl = $("st-vol");
let batEl = $("st-bat");
let bellEl = $("bell");

let workspaces = [];
let toplevels = new Map();
let trayItems = new Map();
let player = null;
let sink = null;
let notifCount = 0;

logo.append(icon("grid"));
logo.addEventListener("click", () => togglePanel(keys.launcher));
bellEl.querySelector(".icon").replaceWith(icon("bell"));

for (let el of [$("clock"), statusEl, bellEl]) {
  el.addEventListener("click", () => togglePanel(keys.center));
}

subscribe("workspace", list => {
  workspaces = list.filter(w => !w.hidden);
  workspacesEl.classList.toggle("hidden", workspaces.length == 0);
  reconcile(
    workspacesEl,
    workspaces,
    w => w.id,
    w => h("button", { class: "ws", onclick: () => ww.workspace.set(w.id) }),
    (node, w) => {
      node.classList.toggle("active", w.active);
      node.classList.toggle("urgent", w.urgent);
      node.textContent = w.active ? w.name : /^\d+$/.test(w.name) ? w.name : "";
      node.title = w.name;
    }
  );
});

workspacesEl.addEventListener(
  "wheel",
  e => {
    e.preventDefault();
    let index = workspaces.findIndex(w => w.active);
    let next = workspaces[clamp(index + Math.sign(e.deltaY), 0, workspaces.length - 1)];
    if (next && !next.active) ww.workspace.set(next.id);
  },
  { passive: false }
);

subscribe("toplevels", list => {
  toplevels = new Map(list.map(t => [t.id, t]));
  reconcile(
    tasksEl,
    list,
    t => t.id,
    t => {
      let node = h("button", { class: "task" }, appIcon(t.appId, t.appId || t.title), h("span", { class: "task-dot" }));
      node.addEventListener("click", () => focusTask(t.id));
      node.addEventListener("auxclick", e => e.button == 1 && ww.toplevels.close(t.id));
      return node;
    },
    (node, t) => {
      node.classList.toggle("active", t.activated);
      node.classList.toggle("minimized", t.minimized);
      node.title = t.title;
    }
  );
  let active = list.find(t => t.activated);
  swapText(titleEl, active ? active.title : "");
});

function focusTask(id) {
  let t = toplevels.get(id);
  if (!t) return;
  if (t.activated && !t.minimized) return ww.toplevels.setMinimized(id, true);
  if (t.minimized) ww.toplevels.setMinimized(id, false);
  ww.toplevels.activate(id);
}

let timeFormat = new Intl.DateTimeFormat(undefined, { hour: "2-digit", minute: "2-digit" });
let dateFormat = new Intl.DateTimeFormat(undefined, { weekday: "short", day: "numeric", month: "short" });

function tick() {
  let now = new Date();
  swapText(timeEl, timeFormat.format(now));
  dateEl.textContent = dateFormat.format(now);
  setTimeout(tick, 1000 - now.getMilliseconds() + 5);
}

tick();

subscribe("media", players => {
  player = pickPlayer(players);
  mediaEl.classList.toggle("hidden", !player || !player.title);
  if (!player) return;
  mediaEl.classList.toggle("playing", player.playing);
  let text = player.artist ? `${player.title} · ${player.artist}` : player.title;
  swapText(mediaText, text);
  mediaEl.title = `${player.identity}: ${text}`;
});

mediaEl.addEventListener("click", () => player && ww.media.playPause(player.id));
mediaEl.addEventListener("contextmenu", e => {
  e.preventDefault();
  if (player?.canNext) ww.media.next(player.id);
});

subscribe("tray", items => {
  trayItems = new Map(items.map(i => [i.id, i]));
  reconcile(
    trayEl,
    items.filter(i => i.status != "Passive"),
    i => i.id,
    i => {
      let node = h("button", { class: "tray-item" }, appIcon(i.icon, i.title || i.appId));
      node.addEventListener("click", () => {
        let item = trayItems.get(i.id);
        if (item?.itemIsMenu) ww.tray.contextMenu(i.id);
        else ww.tray.activate(i.id);
      });
      node.addEventListener("auxclick", e => e.button == 1 && ww.tray.secondaryActivate(i.id));
      node.addEventListener("contextmenu", e => {
        e.preventDefault();
        ww.tray.contextMenu(i.id);
      });
      node.addEventListener("wheel", e => ww.tray.scroll(i.id, Math.sign(e.deltaY)));
      return node;
    },
    (node, i) => {
      setAppIcon(node.firstChild, i.icon, i.title || i.appId);
      node.classList.toggle("attention", i.status == "NeedsAttention");
      node.title = i.tooltip || i.title || i.appId;
    }
  );
});

subscribe("network", net => {
  let info = networkInfo(net);
  setIcon(netEl, info.icon);
  netEl.classList.toggle("busy", info.busy);
  netEl.title = info.label;
});

subscribe("bluetooth", bt => {
  btEl.classList.toggle("hidden", !bt.available || !bt.powered);
  let connected = bt.devices.filter(d => d.connected);
  btEl.classList.toggle("connected", connected.length > 0);
  setIcon(btEl, "bluetooth");
  btEl.title = connected.length ? connected.map(d => d.name).join(", ") : "Bluetooth on";
});

subscribe("audio", audio => {
  sink = audio.sink ?? null;
  if (!sink) return;
  setIcon(volEl, volumeIconName(sink.volume, sink.muted));
  volEl.title = sink.muted ? "Muted" : pct(sink.volume);
});

statusEl.addEventListener(
  "wheel",
  e => {
    e.preventDefault();
    if (!sink) return;
    ww.audio.setVolume("sink", clamp(sink.volume - Math.sign(e.deltaY) * 0.05, 0, 1));
  },
  { passive: false }
);

statusEl.addEventListener("auxclick", e => e.button == 1 && sink && ww.audio.setMute("sink", !sink.muted));

subscribe("battery", bat => {
  let present = bat.state != "unknown";
  batEl.classList.toggle("hidden", !present);
  if (!present) return;
  let charging = bat.state == "charging" || bat.state == "full" || bat.state == "pending charge";
  setBatteryIcon(batEl.querySelector(".icon"), bat.percent, charging);
  batEl.querySelector(".battery-text").textContent = `${Math.round(bat.percent)}%`;
  batEl.classList.toggle("charging", charging);
  batEl.classList.toggle("low", !charging && bat.percent <= 20);
  batEl.classList.toggle("critical", !charging && bat.percent <= 8);
  let left = charging ? bat.timeToFull : bat.timeToEmpty;
  batEl.title = left > 0 ? `${Math.floor(left / 3600)}h ${Math.round((left % 3600) / 60)}m ${charging ? "to full" : "left"}` : bat.state;
});

subscribe("notifications", list => {
  let count = list.length;
  bellEl.classList.toggle("has", count > 0);
  if (count > notifCount) {
    bellEl.classList.remove("ring");
    bellEl.offsetWidth;
    bellEl.classList.add("ring");
  }
  notifCount = count;
});

watch(keys.dnd, dnd => {
  bellEl.classList.toggle("dnd", !!dnd);
  setIcon(bellEl.querySelector(".icon"), dnd ? "bell-off" : "bell");
});
