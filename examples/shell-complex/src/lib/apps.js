import { shallowRef } from "vue";
import { exec, sh } from "./ww";

let USAGE_KEY = "complex.launcher.usage";

let SCAN = `
dirs="\${XDG_DATA_HOME:-$HOME/.local/share}:\${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
IFS=:
for d in $dirs; do
  [ -d "$d/applications" ] && find -L "$d/applications" -name '*.desktop' -print0
done | xargs -0 grep -H -E '^(\\[|Name=|GenericName=|Comment=|Keywords=|Icon=|Type=|NoDisplay=|Hidden=)' 2>/dev/null
`;

export let apps = shallowRef([]);

function parse(output) {
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

export async function loadApps() {
  let result = await sh(SCAN);
  if (result) apps.value = parse(result.stdout);
}

export function readUsage() {
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

export function score(text, q, extra = "") {
  if (!q) return 0;
  let name = text.toLowerCase();
  if (name.startsWith(q)) return 100;
  if (name.split(/\s+/).some(word => word.startsWith(q))) return 80;
  if (name.includes(q)) return 60;
  if (extra.toLowerCase().includes(q)) return 40;
  let i = 0;
  for (let c of name) if (c == q[i]) i++;
  return i == q.length ? 20 : -1;
}

export function launch(app) {
  bumpUsage(app.id);
  return exec("gtk-launch", [app.id]);
}
