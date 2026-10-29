let stack = $("stack");
let notifications = [];
let dismissed = new Set();
let dnd = false;
let centerOpen = false;
let MAX = 4;

function render() {
  let visible = centerOpen || dnd ? [] : notifications.filter(n => !n.expired && !dismissed.has(n.id)).sort((a, b) => b.time - a.time).slice(0, MAX);

  reconcile(
    stack,
    visible,
    n => n.id,
    n => {
      let node = h("div", { class: "toast shine" });
      node.addEventListener("click", e => {
        if (e.target.closest("button")) return;
        let cur = node.notification;
        if (cur.actions.some(a => a.key == "default")) ww.notifications.invoke(cur.id, "default");
        hide(cur.id);
      });
      return node;
    },
    (node, n) => {
      let prev = node.notification;
      node.notification = n;
      node.classList.toggle("critical", n.urgency == 2);
      if (prev && prev.summary == n.summary && prev.body == n.body && prev.icon == n.icon && prev.image == n.image) return;
      if (prev) {
        node.classList.remove("shine");
        node.offsetWidth;
        node.classList.add("shine");
      }
      node.replaceChildren(...content(n));
    }
  );

  requestAnimationFrame(updateRegion);
}

function content(n) {
  let actions = n.actions.filter(a => a.key != "default");
  let close = h("button", { class: "round-btn close", title: "Dismiss" }, icon("x"));
  close.addEventListener("click", () => hide(n.id));

  return [
    appIcon(n.image || n.icon || n.app.toLowerCase(), n.app),
    h(
      "div",
      { class: "toast-body" },
      h("div", { class: "toast-top" }, h("span", {}, n.app || "Notification"), h("span", {}, "now")),
      h("div", { class: "toast-summary" }, n.summary),
      n.body ? h("div", { class: "toast-text" }, plainText(n.body)) : null,
      actions.length
        ? h(
            "div",
            { class: "toast-actions" },
            actions.map(a =>
              h(
                "button",
                {
                  class: "pill-btn",
                  onclick: () => {
                    ww.notifications.invoke(n.id, a.key);
                    hide(n.id);
                  },
                },
                a.label
              )
            )
          )
        : null
    ),
    close,
  ];
}

function hide(id) {
  dismissed.add(id);
  render();
}

function updateRegion() {
  let rects = [...stack.children]
    .filter(node => !node.classList.contains("leaving"))
    .map(node => node.getBoundingClientRect())
    .map(r => ({ x: Math.floor(r.x), y: Math.floor(r.y), width: Math.ceil(r.width), height: Math.ceil(r.height) }));
  ww.window.setInputRegion(rects);
}

new ResizeObserver(updateRegion).observe(stack);

subscribe("notifications", list => {
  let times = new Map(notifications.map(n => [n.id, n.time]));
  notifications = list;
  let ids = new Set(list.map(n => n.id));
  for (let id of dismissed) if (!ids.has(id)) dismissed.delete(id);
  for (let n of list) if (times.has(n.id) && times.get(n.id) != n.time && !centerOpen) dismissed.delete(n.id);
  render();
});

watch(keys.dnd, value => {
  dnd = !!value;
  render();
});

watch(keys.center, value => {
  centerOpen = !!value;
  if (centerOpen) for (let n of notifications) dismissed.add(n.id);
  render();
});

ww.window.setInputRegion([]);
