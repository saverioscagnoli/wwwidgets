(() => {
  const handlers = new Map();
  let nextId = 0;
  const post = (msg) => window.webkit.messageHandlers.wwwidgets.postMessage(msg);

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
  };
})();
