let queryEl = $("query");
let resultsEl = $("results");
let emptyEl = $("empty");
let modeEl = $("mode");

let USAGE_KEY = "shell.launcher.usage";
let SCAN = `
dirs="\${XDG_DATA_HOME:-$HOME/.local/share}:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
IFS=:
for d in $dirs; do
  [ -d "$d/applications" ] && find -L "$d/applications" -name '*.desktop' -print0
done | xargs -0 grep -H -E '^(\\[|Name=|GenericName=|Comment=|Keywords=|Icon=|Type=|NoDisplay=|Hidden=|Terminal=)' 2>/dev/null
`;

let apps = [];
let windows = [];
let clips = [];
let entries = [];
let selected = 0;
let isOpen = false;

setIcon(modeEl, "search");

function readUsage() {
  try {
    return JSON.parse(localStorage.getItem(USAGE_KEY) ?? "{}");
  } catch {
    return {};
  }
}

function bumpUsage(id) {
  try {
    let usage = readUsage();
    usage[id] = (usage[id] ?? 0) + 1;
    localStorage.setItem(USAGE_KEY, JSON.stringify(usage));
  } catch {
    return;
  }
}

function parseApps(output) {
  let files = new Map();
  let sections = new Map();

  for (let line of output.split("\n")) {
    let match = line.match(/^(.*?\.desktop):(.*)$/);
    if (!match) continue;
    let [, file, rest] = match;
    if (rest.startsWith("[")) {
      sections.set(file, rest);
      continue;
    }
    if (sections.get(file) != "[Desktop Entry]") continue;
    let i = rest.indexOf("=");
    let entry = files.get(file) ?? {};
    let key = rest.slice(0, i);
    if (!(key in entry)) entry[key] = rest.slice(i + 1);
    files.set(file, entry);
  }

  let seen = new Set();
  let list = [];

  for (let [file, e] of files) {
    let id = file.slice(file.indexOf("/applications/") + 14).replace(/\//g, "-");
    if (seen.has(id)) continue;
    seen.add(id);
    if (e.Type != "Application" || e.NoDisplay == "true" || e.Hidden == "true" || !e.Name) continue;
    list.push({
      id,
      name: e.Name,
      generic: e.GenericName ?? "",
      comment: e.Comment ?? "",
      keywords: (e.Keywords ?? "").replace(/;/g, " "),
      icon: e.Icon ?? "",
    });
  }

  return list.sort((a, b) => a.name.localeCompare(b.name));
}

async function loadApps() {
  let result = await quiet(ww.exec("sh", ["-c", SCAN]));
  if (!result) return;
  apps = parseApps(result.stdout);
  render();
}

async function loadWindows() {
  windows = (await quiet(ww.toplevels.list())) ?? [];
  render();
}

async function loadClips() {
  clips = (await quiet(ww.clipboard.history())) ?? [];
  render();
}

function score(app, q) {
  if (!q) return 0;
  let name = app.name.toLowerCase();
  if (name.startsWith(q)) return 100;
  if (name.split(/\s+/).some(word => word.startsWith(q))) return 80;
  if (name.includes(q)) return 60;
  let extra = `${app.generic} ${app.keywords} ${app.comment} ${app.id}`.toLowerCase();
  if (extra.includes(q)) return 40;
  let i = 0;
  for (let c of name) if (c == q[i]) i++;
  return i == q.length ? 20 : -1;
}

function calculate(input) {
  let tokens = input.match(/\d*\.?\d+(?:e[+-]?\d+)?|[-+*/%^()]|\S+/gi) ?? [];
  let pos = 0;

  function peek() {
    return tokens[pos];
  }

  function expr() {
    let value = term();
    while (peek() == "+" || peek() == "-") value = tokens[pos++] == "+" ? value + term() : value - term();
    return value;
  }

  function term() {
    let value = unary();
    while (peek() == "*" || peek() == "/" || peek() == "%") {
      let op = tokens[pos++];
      let right = unary();
      value = op == "*" ? value * right : op == "/" ? value / right : value % right;
    }
    return value;
  }

  function unary() {
    if (peek() == "-") {
      pos++;
      return -unary();
    }
    if (peek() == "+") {
      pos++;
      return unary();
    }
    return power();
  }

  function power() {
    let base = atom();
    if (peek() == "^") {
      pos++;
      return base ** unary();
    }
    return base;
  }

  function atom() {
    let token = tokens[pos++];
    if (token == "(") {
      let value = expr();
      if (tokens[pos++] != ")") throw new Error("paren");
      return value;
    }
    if (token == "pi") return Math.PI;
    if (token == "e") return Math.E;
    let value = Number(token);
    if (token == null || isNaN(value)) throw new Error("token");
    return value;
  }

  let value = expr();
  if (pos != tokens.length || !isFinite(value)) throw new Error("syntax");
  return String(Number(value.toPrecision(12)));
}

async function copyText(text) {
  let ok = await quiet(ww.clipboard.set(text));
  if (!ok) await quiet(ww.exec("wl-copy", [text]));
}

function close() {
  return openPanel(keys.launcher, false);
}

function launch(app) {
  bumpUsage(app.id);
  ww.exec("gtk-launch", [app.id]);
  close();
}

function focusWindow(t) {
  if (t.minimized) ww.toplevels.setMinimized(t.id, false);
  ww.toplevels.activate(t.id);
  close();
}

function buildEntries() {
  let raw = queryEl.value;
  let list = [];

  if (raw.startsWith("=")) {
    setIcon(modeEl, "calc");
    let expression = raw.slice(1).trim();
    let result = "";
    try {
      result = expression ? calculate(expression) : "";
    } catch {
      result = "";
    }
    list.push({
      key: "calc",
      big: true,
      glyph: "calc",
      title: result || "…",
      sub: result ? `${expression} · Enter to copy` : "Type an expression, e.g. = (2 + 3) * 4 ^ 2",
      action: () => result && copyText(result).then(close),
    });
    return list;
  }

  if (raw.startsWith(">")) {
    setIcon(modeEl, "terminal");
    let command = raw.slice(1).trim();
    list.push({
      key: "run",
      glyph: "terminal",
      title: command || "Run a command",
      sub: command ? "Enter to run in the background" : "e.g. > foot -e htop",
      action: () => {
        if (!command) return;
        run(command);
        close();
      },
    });
    return list;
  }

  if (raw.startsWith(":")) {
    setIcon(modeEl, "clipboard");
    let q = raw.slice(1).trim().toLowerCase();
    let matches = clips.filter(c => (c.image ? "image".includes(q) || !q : (c.text ?? "").toLowerCase().includes(q))).slice(0, 50);
    if (matches.length) list.push({ key: "s-clip", section: "Clipboard" });
    for (let c of matches) {
      list.push({
        key: `clip-${c.id}`,
        glyph: c.image ? null : "clipboard",
        icon: c.image ? c.url : null,
        title: c.image ? "Image" : (c.text ?? "").replace(/\s+/g, " ").slice(0, 120),
        sub: `${ago(c.time)} · ${c.size > 1024 ? `${Math.round(c.size / 1024)} KB` : `${c.size} B`}`,
        tag: "copy",
        action: () => ww.clipboard.copy(c.id).then(close),
      });
    }
    return list;
  }

  setIcon(modeEl, "search");
  let q = raw.trim().toLowerCase();

  let wins = q
    ? windows.filter(t => t.title.toLowerCase().includes(q) || t.appId.toLowerCase().includes(q)).slice(0, 5)
    : [];

  if (wins.length) list.push({ key: "s-win", section: "Windows" });
  for (let t of wins) {
    list.push({
      key: `win-${t.id}`,
      icon: t.appId,
      label: t.appId || t.title,
      title: t.title || t.appId,
      sub: t.appId,
      tag: "switch",
      action: () => focusWindow(t),
    });
  }

  let usage = readUsage();
  let ranked = apps
    .map(app => ({ app, score: score(app, q) }))
    .filter(r => r.score >= 0)
    .sort((a, b) => b.score - a.score || (usage[b.app.id] ?? 0) - (usage[a.app.id] ?? 0) || a.app.name.localeCompare(b.app.name))
    .slice(0, 60);

  if (ranked.length) list.push({ key: "s-app", section: q ? "Applications" : "All applications" });
  for (let { app } of ranked) {
    list.push({
      key: `app-${app.id}`,
      icon: app.icon,
      label: app.name,
      title: app.name,
      sub: app.comment || app.generic,
      tag: "open",
      action: () => launch(app),
    });
  }

  if (q && !wins.length && !ranked.length) {
    try {
      let result = calculate(q);
      list.push({ key: "calc", big: true, glyph: "calc", title: result, sub: `${q} · Enter to copy`, action: () => copyText(result).then(close) });
    } catch {
      list.push({ key: "run", glyph: "terminal", title: raw.trim(), sub: "Run as a command", action: () => (run(raw.trim()), close()) });
    }
  }

  return list;
}

function selectable() {
  return entries.filter(e => !e.section);
}

function render() {
  entries = buildEntries();
  let items = selectable();
  selected = clamp(selected, 0, Math.max(0, items.length - 1));
  let current = items[selected];

  reconcile(
    resultsEl,
    entries,
    e => e.key,
    e => {
      if (e.section) return h("div", { class: "section" });
      let node = h("button", { class: "row" });
      node.addEventListener("click", () => node.entry?.action());
      node.addEventListener("mousemove", () => {
        let index = selectable().indexOf(node.entry);
        if (index >= 0 && index != selected) {
          selected = index;
          paintSelection();
        }
      });
      return node;
    },
    (node, e) => {
      if (e.section) {
        node.textContent = e.section;
        return;
      }
      let changed = !node.entry || node.entry.title != e.title || node.entry.sub != e.sub || node.entry.icon != e.icon;
      node.entry = e;
      node.classList.toggle("big", !!e.big);
      node.classList.toggle("selected", e === current);
      if (!changed) return;
      let visual = e.glyph ? h("span", { class: "glyph" }, icon(e.glyph)) : appIcon(e.icon, e.label ?? e.title);
      node.replaceChildren(
        visual,
        h("span", { class: "row-text" }, h("span", { class: "row-title" }, e.title), e.sub ? h("span", { class: "row-sub" }, e.sub) : null),
        e.tag ? h("span", { class: "row-tag" }, e.tag) : null
      );
    }
  );

  emptyEl.classList.toggle("hidden", items.length > 0);
}

function paintSelection() {
  let current = selectable()[selected];
  for (let node of resultsEl.children) {
    if (node.entry) node.classList.toggle("selected", node.entry === current);
  }
  let node = [...resultsEl.children].find(n => n.entry === current);
  node?.scrollIntoView({ block: "nearest" });
}

function move(delta) {
  let count = selectable().length;
  if (!count) return;
  selected = (selected + delta + count) % count;
  paintSelection();
}

queryEl.addEventListener("input", () => {
  document.body.classList.add("typed");
  selected = 0;
  if (queryEl.value.startsWith(":") && !clips.length) loadClips();
  render();
  resultsEl.scrollTop = 0;
});

window.addEventListener("keydown", e => {
  if (!isOpen) return;
  if (e.key == "Escape") {
    e.preventDefault();
    if (queryEl.value) {
      queryEl.value = "";
      queryEl.dispatchEvent(new Event("input"));
    } else close();
  } else if (e.key == "ArrowDown" || (e.key == "Tab" && !e.shiftKey) || (e.ctrlKey && e.key == "n")) {
    e.preventDefault();
    move(1);
  } else if (e.key == "ArrowUp" || (e.key == "Tab" && e.shiftKey) || (e.ctrlKey && e.key == "p")) {
    e.preventDefault();
    move(-1);
  } else if (e.key == "Enter") {
    e.preventDefault();
    selectable()[selected]?.action();
  }
});

$("backdrop").addEventListener("click", close);

watch(keys.launcher, value => {
  isOpen = !!value;
  document.body.classList.toggle("open", isOpen);
  if (isOpen) {
    queryEl.value = "";
    selected = 0;
    clips = [];
    document.body.classList.remove("typed");
    render();
    resultsEl.scrollTop = 0;
    loadApps();
    loadWindows();
    setTimeout(() => queryEl.focus(), 30);
  } else {
    queryEl.blur();
  }
});

loadApps();
