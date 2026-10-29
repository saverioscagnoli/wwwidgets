export function pct(value) {
  return `${Math.round(value * 100)}%`;
}

export function duration(seconds) {
  if (!isFinite(seconds) || seconds < 0) seconds = 0;
  let h = Math.floor(seconds / 3600);
  let m = Math.floor((seconds % 3600) / 60);
  let s = Math.floor(seconds % 60);
  let tail = `${String(m).padStart(h ? 2 : 1, "0")}:${String(s).padStart(2, "0")}`;
  return h ? `${h}:${tail}` : tail;
}

export function ago(ms, now = Date.now()) {
  let s = Math.max(0, (now - ms) / 1000);
  if (s < 45) return "now";
  if (s < 3600) return `${Math.round(s / 60)}m`;
  if (s < 86400) return `${Math.round(s / 3600)}h`;
  return `${Math.round(s / 86400)}d`;
}

export function hoursMinutes(seconds) {
  let h = Math.floor(seconds / 3600);
  let m = Math.round((seconds % 3600) / 60);
  return h ? `${h}h ${m}m` : `${m}m`;
}

export function bytes(value) {
  let units = ["B", "KB", "MB", "GB", "TB"];
  let i = 0;
  while (value >= 1024 && i < units.length - 1) {
    value /= 1024;
    i++;
  }
  return `${value.toFixed(value >= 10 || i == 0 ? 0 : 1)} ${units[i]}`;
}

export function volumeIcon(volume, muted) {
  if (muted || volume <= 0.001) return "volume_off";
  if (volume < 0.34) return "volume_mute";
  if (volume < 0.67) return "volume_down";
  return "volume_up";
}

export function brightnessIcon(value) {
  if (value < 0.34) return "brightness_low";
  if (value < 0.67) return "brightness_medium";
  return "brightness_high";
}

export function wifiIcon(strength) {
  if (strength >= 80) return "signal_wifi_4_bar";
  if (strength >= 60) return "network_wifi_3_bar";
  if (strength >= 40) return "network_wifi_2_bar";
  if (strength >= 20) return "network_wifi_1_bar";
  return "signal_wifi_0_bar";
}

export function networkIcon(net, ap, wired) {
  if (!net || !net.available) return "signal_wifi_off";
  if (wired) return "lan";
  if (ap) return wifiIcon(ap.strength);
  if (!net.wifiEnabled) return "wifi_off";
  return "signal_wifi_statusbar_not_connected";
}

export function batteryIcon(percent, charging) {
  if (charging) return percent >= 95 ? "battery_charging_full" : "battery_charging_80";
  if (percent >= 95) return "battery_full";
  return `battery_${Math.max(0, Math.min(6, Math.round((percent / 100) * 6)))}_bar`;
}

export function isCharging(bat) {
  return bat?.state == "charging" || bat?.state == "full" || bat?.state == "pending charge";
}
