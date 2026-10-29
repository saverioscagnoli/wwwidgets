import { ref, shallowRef } from "vue";

export let ww = window.wwwidgets;
export let monitor = ww.monitor;

export function quiet(promise) {
  try {
    return Promise.resolve(promise).catch(() => null);
  } catch {
    return Promise.resolve(null);
  }
}

export function service(name, initial) {
  let value = shallowRef(initial);
  let api = ww[name];
  let first = api.get ?? api.list ?? api.history;
  quiet(first()).then(v => {
    if (v != null) value.value = v;
  });
  api.onChange(v => {
    value.value = v;
  });
  return value;
}

export function storeRef(key, fallback) {
  let value = ref(fallback);
  quiet(ww.state.get(key)).then(v => {
    if (v != null) value.value = v;
  });
  ww.state.onChange(key, v => {
    value.value = v ?? fallback;
  });
  return value;
}

export function clamp(value, min, max) {
  return Math.max(min, Math.min(max, value));
}

export function throttle(fn, ms) {
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

export function iconSrc(name) {
  if (!name) return "";
  if (/^(https?|wwwidgets|data):/.test(name)) return name;
  return `wwwidgets://icon/${encodeURIComponent(name)}`;
}

export function artSrc(art) {
  if (!art) return "";
  if (art.startsWith("file://") || art.startsWith("/")) return iconSrc(art);
  return art;
}

export function plainText(html) {
  if (!html) return "";
  return new DOMParser().parseFromString(html, "text/html").body.textContent ?? "";
}

export function exec(command, args) {
  return quiet(ww.exec(command, args));
}

export function sh(script, ...args) {
  return exec("sh", ["-c", script, "shell-complex", ...args]);
}

export function run(command) {
  return sh('setsid -f sh -c "$1" >/dev/null 2>&1', command);
}

export async function copyText(text) {
  let ok = await quiet(ww.clipboard.set(text));
  if (!ok) await exec("wl-copy", [text]);
}
