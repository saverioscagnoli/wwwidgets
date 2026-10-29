let body = document.body;
let isOpen = false;

document.querySelectorAll("#panel > .block").forEach((el, i) => el.style.setProperty("--i", i));

function closePanel() {
  return openPanel(keys.center, false);
}

$("backdrop").addEventListener("click", closePanel);
window.addEventListener("keydown", e => e.key == "Escape" && closePanel());

watch(keys.center, value => {
  isOpen = !!value;
  body.classList.toggle("open", isOpen);
  if (isOpen) {
    resetCalendar();
    loadUptime();
    tickPlayer();
  } else {
    setDrawer(null);
    disarm();
  }
});

let avatarEl = $("avatar");
let userEl = $("user");
let uptimeEl = $("uptime");

async function loadUser() {
  let result = await quiet(ww.exec("sh", ["-c", 'id -un; printf "%s\\n" "$HOME"; getent passwd "$(id -un)" | cut -d: -f5 | cut -d, -f1']));
  if (!result) return;
  let [user, home, gecos] = result.stdout.split("\n");
  let name = gecos?.trim() || user;
  userEl.textContent = name;
  setAppIcon(avatarEl, `${home}/.face`, name);
}

async function loadUptime() {
  let result = await quiet(ww.exec("uptime", ["-p"]));
  if (result) uptimeEl.textContent = result.stdout.trim().replace(/^up\s+/, "Up ");
}

loadUser();

let lockBtn = $("lock");
let powerBtn = $("power");
lockBtn.append(icon("lock"));
powerBtn.append(icon("power"));

lockBtn.addEventListener("click", () => {
  closePanel();
  ww.exec("loginctl", ["lock-session"]);
});

powerBtn.addEventListener("click", () => setDrawer("power"));

let POWER = {
  suspend: { icon: "moon", label: "Sleep", command: "systemctl suspend" },
  logout: { icon: "logout", label: "Log out", command: 'loginctl terminate-user "$(id -un)"' },
  reboot: { icon: "restart", label: "Restart", command: "systemctl reboot" },
  poweroff: { icon: "power", label: "Shut down", command: "systemctl poweroff" },
};

let armed = null;
let armTimer = 0;

function disarm() {
  clearTimeout(armTimer);
  armed?.classList.remove("armed");
  if (armed) armed.lastChild.textContent = POWER[armed.dataset.action].label;
  armed = null;
}

for (let btn of document.querySelectorAll(".power-btn")) {
  let action = POWER[btn.dataset.action];
  btn.append(icon(action.icon), h("span", {}, action.label));
  btn.addEventListener("click", () => {
    if (armed == btn) {
      disarm();
      closePanel();
      ww.exec("sh", ["-c", action.command]);
      return;
    }
    disarm();
    armed = btn;
    btn.classList.add("armed");
    btn.lastChild.textContent = "Confirm?";
    armTimer = setTimeout(disarm, 3000);
  });
}

function tile(id) {
  let root = $(id);
  let more = root.querySelector(".tile-more");
  more?.append(icon("chevron"));
  return {
    root,
    more,
    main: root.querySelector(".tile-main"),
    icon: root.querySelector(".tile-icon"),
    sub: root.querySelector("small"),
  };
}

let wifiTile = tile("tile-wifi");
let btTile = tile("tile-bt");
let dndTile = tile("tile-dnd");
let micTile = tile("tile-mic");

let volRow = sliderRow("row-vol", v => ww.audio.setVolume("sink", v), () => sink && ww.audio.setMute("sink", !sink.muted));
let micRow = sliderRow("row-mic", v => ww.audio.setVolume("source", v), () => source && ww.audio.setMute("source", !source.muted));
let brightRow = sliderRow("row-bright", v => ww.brightness.set(Math.max(0.01, v)), () => ww.brightness.set(brightRow.slider.value > 0.5 ? 0.25 : 1));
let volMore = $("row-vol").querySelector(".more");
volMore.append(icon("chevron"));
setIcon(brightRow.glyph, "sun");

let drawers = {
  power: { el: $("power-menu"), trigger: powerBtn },
  wifi: { el: $("wifi-drawer"), trigger: wifiTile.root },
  bt: { el: $("bt-drawer"), trigger: btTile.root },
  mixer: { el: $("mixer-drawer"), trigger: volMore },
};

let openDrawer = null;

function setDrawer(name) {
  let next = name && openDrawer != name ? name : null;
  if (openDrawer == "bt" && next != "bt" && bt?.discovering) quiet(ww.bluetooth.scan(false));
  if (openDrawer == "power" && next != "power") disarm();
  openDrawer = next;
  for (let [key, d] of Object.entries(drawers)) {
    d.el.classList.toggle("open", key == openDrawer);
    d.trigger.classList.toggle("expanded", key == openDrawer);
  }
  if (openDrawer == "wifi") scanWifi();
  if (openDrawer == "bt") quiet(ww.bluetooth.scan(true));
}

wifiTile.more.addEventListener("click", () => setDrawer("wifi"));
btTile.more.addEventListener("click", () => setDrawer("bt"));
volMore.addEventListener("click", () => setDrawer("mixer"));

let net = null;
let wifiList = $("wifi-list");
let wifiScan = $("wifi-scan");
setIcon(wifiScan, "restart");

wifiTile.main.addEventListener("click", () => net && ww.network.setWifi(!net.wifiEnabled));

function scanWifi() {
  wifiScan.classList.add("spinning");
  quiet(ww.network.scan());
  setTimeout(() => wifiScan.classList.remove("spinning"), 3000);
}

subscribe("network", n => {
  net = n;
  let info = networkInfo(n);
  wifiTile.root.classList.toggle("unavailable", !n.available);
  wifiTile.root.classList.toggle("on", n.available && n.wifiEnabled);
  setIcon(wifiTile.icon, n.wifiEnabled ? (info.icon == "ethernet" ? "wifi-0" : info.icon) : "wifi-off");
  wifiTile.icon.classList.toggle("busy", info.busy);
  let active = n.accessPoints.find(ap => ap.active);
  wifiTile.sub.textContent = !n.wifiEnabled ? "Off" : active ? active.ssid : info.icon == "ethernet" ? "Wired" : "Not connected";

  let best = new Map();
  for (let ap of n.wifiEnabled ? n.accessPoints : []) {
    if (!ap.ssid) continue;
    let current = best.get(ap.ssid);
    if (!current || ap.active || (!current.active && ap.strength > current.strength)) best.set(ap.ssid, ap);
  }

  let aps = [...best.values()].sort((a, b) => b.active - a.active || b.strength - a.strength);

  reconcile(
    wifiList,
    aps,
    ap => ap.ssid,
    ap => {
      let node = h("div", { class: "item" });
      let main = h("button", { class: "item-main" }, icon("wifi-0"), h("span", { class: "item-name" }, ap.ssid), h("span", { class: "item-meta" }));
      main.addEventListener("click", () => wifiClick(node));
      node.append(main);
      return node;
    },
    (node, ap) => {
      node.ap = ap;
      node.classList.toggle("active", ap.active);
      setIcon(node.querySelector(".item-main > .icon"), `wifi-${wifiLevel(ap.strength)}`);
      let meta = node.querySelector(".item-meta");
      let key = `${ap.active}|${ap.secure}`;
      if (meta.dataset.key != key) {
        meta.dataset.key = key;
        meta.replaceChildren(ap.active ? "Connected" : "", ap.secure ? icon("lock") : "", ap.active ? icon("check") : "");
      }
      if (ap.active) node.querySelector(".password")?.remove();
    }
  );
});

function wifiClick(node) {
  let ap = node.ap;
  if (ap.active) return ww.network.disconnect();
  if (!ap.secure) return wifiConnect(node, null);
  let existing = node.querySelector(".password");
  if (existing) return existing.remove();
  for (let other of wifiList.querySelectorAll(".password")) other.remove();
  let input = h("input", { type: "password", placeholder: "Password (empty to use saved)" });
  let go = h("button", { class: "pill-btn" }, "Join");
  let row = h("div", { class: "password" }, input, go);
  go.addEventListener("click", () => wifiConnect(node, input.value || null));
  input.addEventListener("keydown", e => e.key == "Enter" && wifiConnect(node, input.value || null));
  node.append(row);
  setTimeout(() => input.focus(), 50);
}

async function wifiConnect(node, password) {
  node.classList.add("pending");
  node.classList.remove("error");
  let ok = await ww.network.connect(node.ap.path, password).then(
    () => true,
    () => false
  );
  node.classList.remove("pending");
  if (ok) node.querySelector(".password")?.remove();
  else {
    node.offsetWidth;
    node.classList.add("error");
  }
}

let bt = null;
let btList = $("bt-list");
let btScan = $("bt-scan");
let btRequest = $("bt-request");
setIcon(btScan, "restart");

btTile.main.addEventListener("click", () => bt && ww.bluetooth.setPowered(!bt.powered));

subscribe("bluetooth", b => {
  bt = b;
  btTile.root.classList.toggle("unavailable", !b.available);
  btTile.root.classList.toggle("on", b.available && b.powered);
  setIcon(btTile.icon, b.powered ? "bluetooth" : "bluetooth-off");
  let connected = b.devices.filter(d => d.connected);
  btTile.sub.textContent = !b.available ? "Unavailable" : !b.powered ? "Off" : connected.length ? connected.map(d => d.name).join(", ") : "On";
  btScan.classList.toggle("spinning", b.discovering);

  let devices = (b.powered ? b.devices : [])
    .filter(d => d.name)
    .sort((x, y) => y.connected - x.connected || y.paired - x.paired || x.name.localeCompare(y.name));

  reconcile(
    btList,
    devices,
    d => d.path,
    d => {
      let node = h("div", { class: "item" });
      let main = h("button", { class: "item-main" }, icon("bluetooth"), h("span", { class: "item-name" }), h("span", { class: "item-meta" }));
      main.addEventListener("click", () => btClick(node));
      node.append(main);
      return node;
    },
    (node, d) => {
      node.device = d;
      node.classList.toggle("active", d.connected);
      node.querySelector(".item-name").textContent = d.name;
      let parts = [];
      if (d.battery != null) parts.push(`${d.battery}%`);
      parts.push(d.connected ? "Connected" : d.paired ? "Paired" : "");
      node.querySelector(".item-meta").textContent = parts.filter(Boolean).join(" · ");
    }
  );
});

async function btClick(node) {
  let d = node.device;
  node.classList.add("pending");
  node.classList.remove("error");
  let ok = await (d.connected
    ? ww.bluetooth.disconnect(d.path)
    : d.paired
      ? ww.bluetooth.connect(d.path)
      : ww.bluetooth.pair(d.path).then(() => ww.bluetooth.connect(d.path))
  ).then(
    () => true,
    () => false
  );
  node.classList.remove("pending");
  if (!ok) {
    node.offsetWidth;
    node.classList.add("error");
  }
}

ww.bluetooth.onRequest(req => {
  if (req.type == "cancel") {
    btRequest.classList.add("hidden");
    return;
  }

  openPanel(keys.center, true);
  if (openDrawer != "bt") setDrawer("bt");

  let input = req.type == "pin" || req.type == "passkey" ? h("input", { placeholder: req.type == "pin" ? "PIN" : "Passkey", inputmode: "numeric" }) : null;
  let prompts = {
    display: "Type this code on the device",
    confirm: "Does this code match the one on the device?",
    authorize: "Allow this device to connect?",
    pin: "Enter the PIN shown on the device",
    passkey: "Enter the passkey shown on the device",
  };

  function finish(promise) {
    btRequest.classList.add("hidden");
    quiet(promise);
  }

  let reject = h("button", { class: "pill-btn", onclick: () => finish(ww.bluetooth.reject()) }, req.type == "display" ? "Cancel" : "Reject");
  let accept =
    req.type == "display"
      ? null
      : h(
          "button",
          {
            class: "pill-btn primary",
            onclick: () => finish(ww.bluetooth.reply(input ? (req.type == "passkey" ? Number(input.value) : input.value) : undefined)),
          },
          "Accept"
        );

  btRequest.replaceChildren(
    h("b", {}, req.name || req.device),
    h("span", {}, prompts[req.type] ?? ""),
    "code" in req ? h("div", { class: "code" }, req.code) : null,
    input,
    h("div", { class: "actions" }, reject, accept)
  );
  btRequest.classList.remove("hidden");
  input && setTimeout(() => input.focus(), 50);
});

dndTile.main.addEventListener("click", () => ww.state.toggle(keys.dnd));

watch(keys.dnd, dnd => {
  dndTile.root.classList.toggle("on", !!dnd);
  setIcon(dndTile.icon, dnd ? "bell-off" : "bell");
  dndTile.sub.textContent = dnd ? "Silenced" : "Off";
});

micTile.main.addEventListener("click", () => source && ww.audio.setMute("source", !source.muted));

function sliderRow(id, onInput, onButton) {
  let row = $(id);
  let value = row.querySelector(".value");
  let btn = row.querySelector(".round-btn");
  let glyph = icon("volume-2");
  let s = slider({ onInput, onPreview: v => (value.textContent = pct(v)) });
  btn.append(glyph);
  row.querySelector(".slot").append(s.el);
  btn.addEventListener("click", onButton);
  return {
    row,
    glyph,
    slider: s,
    set(v, muted) {
      s.set(v);
      value.textContent = pct(s.value);
      row.classList.toggle("muted", !!muted);
    },
  };
}

let sink = null;
let source = null;
let sinksEl = $("sinks");
let streamsEl = $("streams");

subscribe("audio", audio => {
  sink = audio.sink ?? null;
  source = audio.source ?? null;

  volRow.row.classList.toggle("hidden", !sink);
  if (sink) {
    volRow.set(sink.volume, sink.muted);
    setIcon(volRow.glyph, volumeIconName(sink.volume, sink.muted));
  }

  micRow.row.classList.toggle("hidden", !source);
  micTile.root.classList.toggle("unavailable", !source);
  if (source) {
    micRow.set(source.volume, source.muted);
    setIcon(micRow.glyph, source.muted ? "mic-off" : "mic");
    micTile.root.classList.toggle("on", !source.muted);
    setIcon(micTile.icon, source.muted ? "mic-off" : "mic");
    micTile.sub.textContent = source.muted ? "Muted" : source.description;
  }

  reconcile(
    sinksEl,
    audio.sinks,
    d => d.name,
    d => {
      let node = h("div", { class: "item" });
      let main = h("button", { class: "item-main" }, icon("speaker"), h("span", { class: "item-name" }), h("span", { class: "item-meta" }));
      main.addEventListener("click", () => ww.audio.setDefault("sink", d.name));
      node.append(main);
      return node;
    },
    (node, d) => {
      node.classList.toggle("active", d.default);
      node.querySelector(".item-name").textContent = d.description;
      let meta = node.querySelector(".item-meta");
      if (meta.dataset.on != String(d.default)) {
        meta.dataset.on = d.default;
        meta.replaceChildren(d.default ? icon("check") : "");
      }
    }
  );

  reconcile(
    streamsEl,
    audio.playback,
    s => s.index,
    s => {
      let mute = h("button", { class: "round-btn" }, icon("volume-2"));
      let control = slider({ onInput: v => ww.audio.setStreamVolume("sink", s.index, v) });
      let node = h(
        "div",
        { class: "item stream" },
        appIcon(s.icon || s.app.toLowerCase(), s.app),
        h("span", { class: "stream-text" }, h("b", {}, s.app), h("small", {})),
        control.el,
        mute
      );
      mute.addEventListener("click", () => ww.audio.setStreamMute("sink", s.index, !node.stream.muted));
      node.control = control;
      return node;
    },
    (node, s) => {
      node.stream = s;
      node.control.set(s.volume);
      node.classList.toggle("muted", s.muted);
      node.querySelector("small").textContent = s.title;
      setIcon(node.querySelector(".round-btn .icon"), volumeIconName(s.volume, s.muted));
    }
  );

  $("streams-empty").classList.toggle("hidden", audio.playback.length > 0);
});

subscribe("brightness", b => {
  brightRow.row.classList.remove("hidden");
  brightRow.set(b.value, false);
});

let playerEl = $("player");
let artEl = $("art");
let artBg = $("art-bg");
let progressEl = $("progress");
let progressFill = $("progress-fill");
let players = [];
let chosenPlayer = null;
let current = null;
let positionAt = 0;
let playerTimer = 0;

$("p-prev").append(icon("prev"));
$("p-next").append(icon("next"));
$("p-play").append(icon("play"));
$("p-prev").addEventListener("click", () => current && ww.media.previous(current.id));
$("p-next").addEventListener("click", () => current && ww.media.next(current.id));
$("p-play").addEventListener("click", () => current && ww.media.playPause(current.id));

progressEl.addEventListener("click", e => {
  if (!current?.canSeek || !current.length) return;
  let rect = progressEl.getBoundingClientRect();
  let target = clamp((e.clientX - rect.left) / rect.width, 0, 1) * current.length;
  ww.media.setPosition(current.id, target);
  current = { ...current, position: target };
  positionAt = performance.now();
  paintProgress(true);
});

subscribe("media", list => {
  players = list.filter(p => p.title || p.playing);
  let chosen = players.find(p => p.id == chosenPlayer);
  let next = chosen ?? pickPlayer(players);
  let trackChanged = !current || !next || current.id != next.id || current.track != next.track || current.title != next.title;
  current = next ?? null;
  positionAt = performance.now();
  renderPlayer(trackChanged);
});

function renderPlayer(trackChanged) {
  playerEl.classList.toggle("hidden", !current);
  if (!current) return;

  playerEl.classList.toggle("playing", current.playing);
  swapText($("p-title"), current.title || "Unknown title");
  swapText($("p-artist"), current.artist || current.album || "");
  $("p-app").textContent = current.identity;
  setIcon($("p-play").firstChild, current.playing ? "pause" : "play");
  $("p-prev").disabled = !current.canPrev;
  $("p-next").disabled = !current.canNext;
  progressEl.classList.toggle("seekable", current.canSeek && current.length > 0);

  let src = artSrc(current.art);
  if (artEl.dataset.src != src) {
    artEl.dataset.src = src;
    artBg.style.backgroundImage = src ? `url("${src.replace(/"/g, "%22")}")` : "";
    if (src) {
      let img = h("img", { src, alt: "" });
      img.addEventListener("error", () => artEl.replaceChildren(icon("music")), { once: true });
      artEl.replaceChildren(img);
    } else artEl.replaceChildren(icon("music"));
  }

  reconcile(
    $("players"),
    players.length > 1 ? players : [],
    p => p.id,
    p =>
      h("button", {
        title: p.identity,
        onclick: () => {
          chosenPlayer = p.id;
          current = players.find(x => x.id == p.id) ?? current;
          positionAt = performance.now();
          renderPlayer(true);
        },
      }),
    (node, p) => node.classList.toggle("current", p.id == current.id)
  );

  paintProgress(trackChanged);
  tickPlayer();
}

function livePosition() {
  if (!current) return 0;
  let elapsed = current.playing ? (performance.now() - positionAt) / 1000 : 0;
  return Math.min(current.length || Infinity, current.position + elapsed);
}

function paintProgress(jump) {
  if (!current) return;
  let pos = livePosition();
  let ratio = current.length > 0 ? pos / current.length : 0;
  progressFill.style.transition = jump || !current.playing ? "none" : "";
  progressFill.style.width = `${clamp(ratio, 0, 1) * 100}%`;
  $("p-pos").textContent = duration(pos);
  $("p-len").textContent = current.length > 0 ? duration(current.length) : "";
}

function tickPlayer() {
  clearTimeout(playerTimer);
  if (!isOpen || !current?.playing) return;
  playerTimer = setTimeout(() => {
    paintProgress(false);
    tickPlayer();
  }, 1000);
}

let calGrid = $("cal-grid");
let view = new Date();
let weekdays = Array.from({ length: 7 }, (_, i) => new Date(2024, 0, 1 + i).toLocaleDateString(undefined, { weekday: "narrow" }));

$("cal-prev").append(icon("chevron-left"));
$("cal-next").append(icon("chevron"));
$("cal-prev").addEventListener("click", () => shiftMonth(-1));
$("cal-next").addEventListener("click", () => shiftMonth(1));
$("calendar").addEventListener(
  "wheel",
  e => {
    e.preventDefault();
    shiftMonth(Math.sign(e.deltaY));
  },
  { passive: false }
);

function resetCalendar() {
  view = new Date();
  view.setDate(1);
  renderCalendar(null);
}

function shiftMonth(delta) {
  view = new Date(view.getFullYear(), view.getMonth() + delta, 1);
  renderCalendar(delta > 0 ? "slide-left" : "slide-right");
}

function renderCalendar(animation) {
  let today = new Date();
  $("cal-day").textContent = today.toLocaleDateString(undefined, { weekday: "long", day: "numeric", month: "long" });
  $("cal-month").textContent = view.toLocaleDateString(undefined, { month: "long", year: "numeric" });

  let offset = (new Date(view.getFullYear(), view.getMonth(), 1).getDay() + 6) % 7;
  let cells = weekdays.map(d => h("span", { class: "cal-wd" }, d));

  for (let i = 0; i < 42; i++) {
    let date = new Date(view.getFullYear(), view.getMonth(), 1 - offset + i);
    let classes = ["cal-d"];
    if (date.getMonth() != view.getMonth()) classes.push("other");
    if (date.toDateString() == today.toDateString()) classes.push("today");
    if (date.getDay() == 0 || date.getDay() == 6) classes.push("weekend");
    cells.push(h("span", { class: classes.join(" ") }, date.getDate()));
  }

  calGrid.replaceChildren(...cells);
  calGrid.classList.remove("slide-left", "slide-right");
  if (animation) {
    calGrid.offsetWidth;
    calGrid.classList.add(animation);
  }
}

resetCalendar();

let notifList = $("notif-list");
let notifEmpty = $("notif-empty");
let clearBtn = $("clear");
let notifs = [];

clearBtn.addEventListener("click", () => {
  [...notifList.children].forEach((node, i) => setTimeout(() => leave(node), i * 40));
  for (let n of notifs) quiet(ww.notifications.close(n.id));
});

subscribe("notifications", list => {
  notifs = [...list].sort((a, b) => b.time - a.time);
  clearBtn.classList.toggle("hidden", notifs.length == 0);
  notifEmpty.classList.toggle("hidden", notifs.length > 0);
  notifEmpty.textContent = "You're all caught up";

  reconcile(
    notifList,
    notifs,
    n => n.id,
    n => {
      let node = h("div", { class: "notif" });
      node.addEventListener("click", e => {
        if (e.target.closest("button")) return;
        let cur = node.notification;
        if (cur.actions.some(a => a.key == "default")) {
          ww.notifications.invoke(cur.id, "default");
          closePanel();
        }
      });
      return node;
    },
    (node, n) => {
      let prev = node.notification;
      node.notification = n;
      node.classList.toggle("critical", n.urgency == 2);
      if (prev && prev.summary == n.summary && prev.body == n.body && prev.icon == n.icon && prev.actions.length == n.actions.length) {
        node.querySelector(".time").textContent = ago(n.time);
        return;
      }
      node.replaceChildren(...notificationContent(n));
    }
  );
});

function notificationContent(n) {
  let actions = n.actions.filter(a => a.key != "default");
  let close = h("button", { class: "round-btn close", title: "Dismiss" }, icon("x"));
  close.addEventListener("click", () => ww.notifications.close(n.id));
  return [
    appIcon(n.image || n.icon || n.app.toLowerCase(), n.app),
    h(
      "div",
      { class: "notif-body" },
      h("div", { class: "notif-top" }, h("span", { class: "app" }, n.app || "Notification"), h("span", { class: "time" }, ago(n.time))),
      h("div", { class: "notif-summary" }, n.summary),
      n.body ? h("div", { class: "notif-text" }, plainText(n.body)) : null,
      actions.length
        ? h(
            "div",
            { class: "notif-actions" },
            actions.map(a => h("button", { class: "pill-btn", onclick: () => ww.notifications.invoke(n.id, a.key) }, a.label))
          )
        : null
    ),
    close,
  ];
}

setInterval(() => {
  if (!isOpen) return;
  for (let node of notifList.children) {
    if (node.notification) node.querySelector(".time").textContent = ago(node.notification.time);
  }
}, 30000);
