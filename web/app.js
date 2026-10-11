const native = (cmd, args = {}) =>
  window.webkit.messageHandlers.native.postMessage({ cmd, ...args });

const $ = (id) => document.getElementById(id);

$("greet").addEventListener("submit", async (e) => {
  e.preventDefault();
  try {
    $("reply").textContent = await native("greet", { name: $("name").value });
    $("reply").classList.remove("muted");
  } catch (err) {
    $("reply").textContent = `Error: ${err.message}`;
  }
});

let count = 0;
const render = () => ($("count").textContent = count);
$("inc").addEventListener("click", () => { count++; render(); });
$("dec").addEventListener("click", () => { count--; render(); });

native("info").then((info) => {
  $("info").textContent =
    `${info.user}@${info.host} · GTK ${info.gtk} · WebKitGTK ${info.webkit}`;
});
