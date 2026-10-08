(() => {
  const handlers = new Map();
  let nextId = 0;
  const post = (msg) =>
    window.webkit.messageHandlers.wwwidgets.postMessage(msg);

  window.__wwwidgets_emit = (id, kind, data) => {
    const h = handlers.get(id);
    if (!h) return;
    if (kind === "stdout") h.onStdout?.(data);
    else if (kind === "stderr") h.onStderr?.(data);
    else if (kind === "exit") {
      handlers.delete(id);
      h.onExit?.(data);
    }
  };

  const state = new Map();
  const notify = (name, value) => state.get(name)?.forEach((fn) => fn(value));

  window.__wwwidgets_state = notify;

  window.wwwidgets = {
    exec: (argv) => post({ cmd: "exec", argv }),
    spawn: async (argv, callbacks = {}) => {
      const id = nextId++;
      handlers.set(id, callbacks);
      try {
        await post({ cmd: "spawn", id, argv });
      } catch (e) {
        handlers.delete(id);
        throw e;
      }
      return { id, kill: () => post({ cmd: "kill", id }) };
    },
    window: {
      isVisible: () => post({ cmd: "isvisible" }),
      hide: () => post({ cmd: "hide" }),
      show: () => post({ cmd: "show" }),
      size: () => post({ cmd: "size" }),
      setSize: (w, h) => post({ cmd: "setsize", argv: [w, h] }),
      setWidth: (w) => post({ cmd: "setwidth", argv: w }),
      setHeight: (h) => post({ cmd: "setheight", argv: h }),
      setKeyboard: (m) => post({ cmd: "setkeyboard", argv: m }),
      setClickThrough: (c) => post({ cmd: "setclickthrough", argv: c }),
      setMargin: (m) => post({ cmd: "setmargin", argv: m }),
      setAnchor: (a) => post({ cmd: "setanchor", argv: a }),
      setLayer: (l) => post({ cmd: "setlayer", argv: l }),
      setExclusive: (e) => post({ cmd: "setexclusive", argv: e }),
    },
    state: {
      get: (name) => post({ cmd: "getstate", name }),
      set: (name, value) => {
        const json = JSON.stringify(value);
        if (json === undefined)
          throw new TypeError("state.set: value is not JSON-serializable");
        notify(name, value);
        return post({ cmd: "setstate", name, value: json });
      },
      onChange: (name, fn) => {
        if (!state.has(name)) state.set(name, new Set());
        state.get(name).add(fn);
        post({ cmd: "getstate", name }).then((v) => v !== undefined && fn(v));
        return () => state.get(name)?.delete(fn);
      },
    },
    monitors: {
      get: name => post({ cmd: "getmonitor", name }),
      list: () => post({ cmd: "listmonitors" })
    }
  };
})();
