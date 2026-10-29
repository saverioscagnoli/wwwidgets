let osd = $("osd");
let osdIcon = $("osd-icon");
let osdLabel = $("osd-label");
let osdValue = $("osd-value");
let hideTimer = 0;
let centerOpen = false;
let last = { sink: null, source: null, brightness: null };

function show(iconName, label, value, muted) {
  if (centerOpen) return;
  setIcon(osdIcon, iconName);
  osdIcon.classList.remove("bump");
  osdIcon.offsetWidth;
  osdIcon.classList.add("bump");
  osdLabel.textContent = label;
  osdValue.textContent = muted ? "Off" : pct(value);
  osd.style.setProperty("--v", clamp(muted ? 0 : value, 0, 1));
  osd.classList.toggle("muted", !!muted);
  osd.classList.toggle("over", value > 1);
  document.body.classList.add("show");
  clearTimeout(hideTimer);
  hideTimer = setTimeout(() => document.body.classList.remove("show"), 1600);
}

function fingerprint(device) {
  return device ? `${device.name}|${Math.round(device.volume * 100)}|${device.muted}` : null;
}

subscribe("audio", audio => {
  let sink = audio.sink;
  let source = audio.source;
  let sinkKey = fingerprint(sink);
  let sourceKey = fingerprint(source);

  if (last.sink != null && sinkKey != null && sinkKey != last.sink) {
    let switched = !last.sink.startsWith(`${sink.name}|`);
    show(switched ? "speaker" : volumeIconName(sink.volume, sink.muted), switched ? sink.description : "Volume", sink.volume, sink.muted);
  }

  if (last.source != null && sourceKey != null && sourceKey != last.source && last.source.startsWith(`${source.name}|`)) {
    show(source.muted ? "mic-off" : "mic", "Microphone", source.volume, source.muted);
  }

  last.sink = sinkKey ?? last.sink;
  last.source = sourceKey ?? last.source;
});

subscribe("brightness", b => {
  let value = Math.round(b.value * 100);
  if (last.brightness != null && value != last.brightness) show("sun", "Brightness", b.value, false);
  last.brightness = value;
});

watch(keys.center, value => {
  centerOpen = !!value;
  if (centerOpen) document.body.classList.remove("show");
});

ww.window.setInputRegion([]);
