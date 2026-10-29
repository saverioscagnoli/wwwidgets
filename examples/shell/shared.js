let ww = window.wwwidgets;
let monitor = ww.monitor;

let keys = {
  center: `shell.center.${monitor}`,
  launcher: `shell.launcher.${monitor}`,
  dnd: "shell.dnd",
};

let SPEAKER = `<path d="M11 5 6 9H3v6h3l5 4z"/>`;
let MIC = `<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M19 10v1a7 7 0 0 1-14 0v-1"/><path d="M12 18v4"/>`;
let BELL = `<path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9"/><path d="M10.3 21a1.94 1.94 0 0 0 3.4 0"/>`;
let SLASH = `<path d="M3 3l18 18"/>`;
let ARCS = [
  `<path d="M8.5 15.5a5 5 0 0 1 7 0"/>`,
  `<path d="M5 12a10 10 0 0 1 14 0"/>`,
  `<path d="M1.5 8.5a15 15 0 0 1 21 0"/>`,
];
let DOT = `<circle cx="12" cy="19.5" r="1.2" fill="currentColor" stroke="none"/>`;

function wifiArcs(level) {
  return ARCS.map((arc, i) => (i < level ? arc : arc.replace("<path", `<path opacity="0.25"`))).join("") + DOT;
}

let PATHS = {
  grid: `<rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/>`,
  "volume-x": SPEAKER + `<path d="M22 9l-6 6M16 9l6 6"/>`,
  "volume-0": SPEAKER,
  "volume-1": SPEAKER + `<path d="M15.5 8.5a5 5 0 0 1 0 7"/>`,
  "volume-2": SPEAKER + `<path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M19 5a10 10 0 0 1 0 14"/>`,
  mic: MIC,
  "mic-off": MIC + SLASH,
  sun: `<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>`,
  "wifi-0": wifiArcs(0),
  "wifi-1": wifiArcs(1),
  "wifi-2": wifiArcs(2),
  "wifi-3": wifiArcs(3),
  "wifi-off": wifiArcs(0) + SLASH,
  ethernet: `<rect x="3" y="8" width="18" height="11" rx="2"/><path d="M7 19v-4M12 19v-4M17 19v-4M8 8V5h8v3"/>`,
  bluetooth: `<path d="m7 7 10 10-5 5V2l5 5L7 17"/>`,
  "bluetooth-off": `<path d="m17 17-5 5V12l-5 5M12 7V2l5 5-2.5 2.5"/>` + SLASH,
  bell: BELL,
  "bell-off": BELL + SLASH,
  bolt: `<path d="M13 2 4 14h7l-1 8 9-12h-7z"/>`,
  power: `<path d="M12 2v10"/><path d="M18.4 6.6a9 9 0 1 1-12.8 0"/>`,
  restart: `<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/>`,
  moon: `<path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9z"/>`,
  logout: `<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><path d="m16 17 5-5-5-5"/><path d="M21 12H9"/>`,
  lock: `<rect x="4" y="11" width="16" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>`,
  play: `<path d="M7 4.5v15a1 1 0 0 0 1.5.86l12-7.5a1 1 0 0 0 0-1.72l-12-7.5A1 1 0 0 0 7 4.5z" fill="currentColor" stroke="none"/>`,
  pause: `<rect x="6" y="4" width="4" height="16" rx="1.2" fill="currentColor" stroke="none"/><rect x="14" y="4" width="4" height="16" rx="1.2" fill="currentColor" stroke="none"/>`,
  next: `<path d="M5 5.5v13a1 1 0 0 0 1.6.8l8.7-6.5a1 1 0 0 0 0-1.6L6.6 4.7A1 1 0 0 0 5 5.5z" fill="currentColor" stroke="none"/><path d="M19 5v14"/>`,
  prev: `<path d="M19 5.5v13a1 1 0 0 1-1.6.8l-8.7-6.5a1 1 0 0 1 0-1.6l8.7-6.5A1 1 0 0 1 19 5.5z" fill="currentColor" stroke="none"/><path d="M5 5v14"/>`,
  x: `<path d="M18 6 6 18M6 6l12 12"/>`,
  chevron: `<path d="m9 18 6-6-6-6"/>`,
  "chevron-left": `<path d="m15 18-6-6 6-6"/>`,
  search: `<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>`,
  check: `<path d="M20 6 9 17l-5-5"/>`,
  music: `<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>`,
  calc: `<rect x="4" y="2" width="16" height="20" rx="2"/><path d="M8 6h8M8 11h.01M12 11h.01M16 11h.01M8 15h.01M12 15h.01M16 15h.01M8 19h.01M12 19h.01M16 19h.01"/>`,
  terminal: `<path d="m4 17 6-6-6-6"/><path d="M12 19h8"/>`,
  window: `<rect x="2" y="4" width="20" height="16" rx="2"/><path d="M2 9h20"/>`,
  clipboard: `<rect x="8" y="2" width="8" height="4" rx="1"/><path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/>`,
  sliders: `<path d="M4 21v-7M4 10V3M12 21v-9M12 8V3M20 21v-5M20 12V3M1 14h6M9 8h6M17 16h6"/>`,
  speaker: `<rect x="5" y="2" width="14" height="20" rx="2"/><circle cx="12" cy="14" r="4"/><path d="M12 6h.01"/>`,
  trash: `<path d="M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6"/>`,
  shield: `<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/>`,
};

function svg(inner) {
  return `<svg viewBox="0 0 24 24" aria-hidden="true">${inner}</svg>`;
}

function icon(name, cls) {
  let span = document.createElement("span");
  span.className = cls ? `icon ${cls}` : "icon";
  setIcon(span, name);
  return span;
}

function setIcon(node, name) {
  if (node.dataset.icon == name) return;
  node.dataset.icon = name;
  node.innerHTML = svg(PATHS[name] ?? "");
}

function setBatteryIcon(node, percent, charging) {
  let level = Math.round(Math.max(0, Math.min(100, percent)) / 5) * 5;
  let name = `battery:${level}:${charging}`;
  if (node.dataset.icon == name) return;
  node.dataset.icon = name;
  let width = Math.max(1, (level / 100) * 13);
  let bolt = charging ? `<path d="M11.5 8.5 9 12.5h3l-1 3.5 3-4.5h-3z" fill="var(--bg-solid)" stroke="none"/>` : "";
  node.innerHTML = svg(
    `<rect x="2" y="7" width="17" height="10" rx="2.5"/><path d="M22 11v2"/><rect x="4" y="9" width="${width}" height="6" rx="1" fill="currentColor" stroke="none"/>${bolt}`
  );
}

function $(id) {
  return document.getElementById(id);
}

function h(tag, props, ...children) {
  let node = document.createElement(tag);
  for (let [key, value] of Object.entries(props ?? {})) {
    if (value == null || value === false) continue;
    if (key == "class") node.className = value;
    else if (key.startsWith("on") && typeof value == "function") node.addEventListener(key.slice(2), value);
    else if (key in node) node[key] = value;
    else node.setAttribute(key, value);
  }
  for (let child of children.flat()) {
    if (child == null || child === false) continue;
    node.append(child instanceof Node ? child : String(child));
  }
  return node;
}

function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

function pct(value) {
  return `${Math.round(value * 100)}%`;
}

function throttle(fn, ms) {
  let last = 0;
  let timer = 0;
  let pending = [];
  return function (...args) {
    pending = args;
    let wait = ms - (performance.now() - last);
    if (wait <= 0) {
      clearTimeout(timer);
      timer = 0;
      last = performance.now();
      fn(...pending);
    } else if (!timer) {
      timer = setTimeout(() => {
        timer = 0;
        last = performance.now();
        fn(...pending);
      }, wait);
    }
  };
}

function quiet(promise) {
  return Promise.resolve(promise).catch(() => null);
}

function watch(key, cb) {
  quiet(ww.state.get(key)).then(cb);
  return ww.state.onChange(key, cb);
}

function subscribe(name, cb) {
  let api = ww[name];
  let first = api.get ?? api.list;
  quiet(first()).then(value => value != null && cb(value));
  return api.onChange(cb);
}

function openPanel(key, open) {
  let other = key == keys.launcher ? keys.center : keys.launcher;
  if (open) ww.state.set(other, false);
  return ww.state.set(key, open);
}

async function togglePanel(key) {
  let open = !(await quiet(ww.state.get(key)));
  return openPanel(key, open);
}

function iconSrc(name) {
  if (!name) return "";
  if (/^(https?|wwwidgets|data):/.test(name)) return name;
  return `wwwidgets://icon/${encodeURIComponent(name)}`;
}

function appIcon(name, label, cls) {
  let box = h("span", { class: cls ? `app-icon ${cls}` : "app-icon" });
  setAppIcon(box, name, label);
  return box;
}

function setAppIcon(box, name, label) {
  let src = iconSrc(name);
  if (box.dataset.src == src && box.firstChild) return;
  box.dataset.src = src;

  function fallback() {
    let letter = (label || name || "?").replace(/^.*[./]/, "").trim().charAt(0).toUpperCase() || "?";
    let hue = [...(label || name || "")].reduce((a, c) => a + c.charCodeAt(0), 0) % 360;
    box.classList.add("letter");
    box.style.setProperty("--hue", hue);
    box.replaceChildren(h("span", {}, letter));
  }

  if (!src) return fallback();
  let img = h("img", { src, alt: "", draggable: false });
  img.addEventListener("error", fallback, { once: true });
  img.addEventListener("load", () => box.classList.remove("letter"), { once: true });
  box.replaceChildren(img);
}

function plainText(html) {
  if (!html) return "";
  return new DOMParser().parseFromString(html, "text/html").body.textContent ?? "";
}

function reconcile(container, items, keyOf, create, update) {
  let existing = new Map();
  for (let child of [...container.children]) {
    if (child.dataset.key != null && !child.classList.contains("leaving")) existing.set(child.dataset.key, child);
  }

  let seen = new Set();
  let cursor = container.firstChild;

  items.forEach((item, index) => {
    let key = String(keyOf(item));
    seen.add(key);
    let node = existing.get(key);

    if (!node) {
      node = create(item);
      node.dataset.key = key;
      node.classList.add("entering");
      requestAnimationFrame(() => requestAnimationFrame(() => node.classList.remove("entering")));
    }

    node.style.setProperty("--i", index);
    if (update) update(node, item, index);

    while (cursor && cursor.classList?.contains("leaving")) cursor = cursor.nextSibling;
    if (node !== cursor) container.insertBefore(node, cursor);
    else cursor = cursor.nextSibling;
  });

  for (let [key, node] of existing) {
    if (!seen.has(key)) leave(node);
  }
}

function leave(node) {
  if (node.classList.contains("leaving")) return;
  node.style.maxHeight = `${node.offsetHeight}px`;
  node.style.maxWidth = `${node.offsetWidth}px`;
  node.offsetHeight;
  node.classList.add("leaving");
  let done = false;

  function remove() {
    if (done) return;
    done = true;
    node.remove();
  }

  node.addEventListener("transitionend", e => e.target == node && e.propertyName.startsWith("max-") && remove());
  setTimeout(remove, 700);
}

function slider(options) {
  let value = options.value ?? 0;
  let dragging = false;
  let lockedUntil = 0;
  let send = throttle(v => options.onInput?.(v), 40);

  let fill = h("div", { class: "slider-fill" });
  let thumb = h("div", { class: "slider-thumb" });
  let track = h("div", { class: "slider-track" }, fill, thumb);
  let root = h("div", { class: options.class ? `slider ${options.class}` : "slider" }, track);

  function paint() {
    root.style.setProperty("--v", clamp(value, 0, 1));
  }

  function fromEvent(e) {
    let rect = track.getBoundingClientRect();
    return clamp((e.clientX - rect.left) / rect.width, 0, 1);
  }

  function commit(v) {
    value = v;
    paint();
    send(value);
    options.onPreview?.(value);
  }

  track.addEventListener("pointerdown", e => {
    dragging = true;
    root.classList.add("dragging");
    track.setPointerCapture(e.pointerId);
    commit(fromEvent(e));
  });

  track.addEventListener("pointermove", e => dragging && commit(fromEvent(e)));

  function release() {
    if (!dragging) return;
    dragging = false;
    lockedUntil = performance.now() + 400;
    root.classList.remove("dragging");
  }

  track.addEventListener("pointerup", release);
  track.addEventListener("pointercancel", release);

  root.addEventListener(
    "wheel",
    e => {
      e.preventDefault();
      lockedUntil = performance.now() + 400;
      commit(clamp(value - Math.sign(e.deltaY) * 0.05, 0, 1));
    },
    { passive: false }
  );

  paint();

  return {
    el: root,
    set(v) {
      if (dragging || performance.now() < lockedUntil) return;
      value = v;
      paint();
    },
    get value() {
      return value;
    },
  };
}

function volumeIconName(volume, muted) {
  if (muted || volume <= 0.001) return "volume-x";
  if (volume < 0.34) return "volume-0";
  if (volume < 0.67) return "volume-1";
  return "volume-2";
}

function wifiLevel(strength) {
  if (strength >= 70) return 3;
  if (strength >= 40) return 2;
  return 1;
}

function networkInfo(net) {
  if (!net || !net.available) return { icon: "wifi-off", label: "Offline", busy: false };
  let busy = net.state == "connecting" || net.state == "disconnecting";
  let type = net.primary?.type ?? "";
  if (type.includes("ethernet") || type.includes("802-3")) return { icon: "ethernet", label: net.primary.name, busy };
  let active = net.accessPoints.find(ap => ap.active);
  if (active) return { icon: `wifi-${wifiLevel(active.strength)}`, label: active.ssid, busy };
  if (busy) return { icon: "wifi-1", label: "Connecting…", busy };
  if (!net.wifiEnabled) return { icon: "wifi-off", label: "Off", busy };
  return { icon: "wifi-0", label: net.primary?.name ?? "Disconnected", busy };
}

function duration(seconds) {
  if (!isFinite(seconds) || seconds < 0) seconds = 0;
  let m = Math.floor(seconds / 60);
  let s = Math.floor(seconds % 60);
  return `${m}:${String(s).padStart(2, "0")}`;
}

function ago(ms) {
  let s = Math.max(0, (Date.now() - ms) / 1000);
  if (s < 45) return "now";
  if (s < 3600) return `${Math.round(s / 60)}m`;
  if (s < 86400) return `${Math.round(s / 3600)}h`;
  return `${Math.round(s / 86400)}d`;
}

function swapText(node, text) {
  if (node.textContent == text) return;
  node.textContent = text;
  node.classList.remove("swap");
  node.offsetWidth;
  node.classList.add("swap");
}

function pickPlayer(players) {
  return players.find(p => p.playing) ?? players.find(p => p.status == "Paused") ?? players[0];
}

function artSrc(art) {
  if (!art) return "";
  if (art.startsWith("file://") || art.startsWith("/")) return iconSrc(art);
  return art;
}

function run(command) {
  return ww.exec("sh", ["-c", 'setsid -f sh -c "$1" >/dev/null 2>&1', "shell", command]);
}
