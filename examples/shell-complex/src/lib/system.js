import { reactive, ref, shallowRef } from "vue";
import { exec, sh } from "./ww";

export let user = reactive({ name: "", login: "", home: "", host: "", uptime: "" });

export async function loadUser() {
  let result = await sh('id -un; printf "%s\\n" "$HOME"; getent passwd "$(id -un)" | cut -d: -f5 | cut -d, -f1; uname -n; uptime -p');
  if (!result) return;
  let [login, home, gecos, host, uptime] = result.stdout.split("\n");
  user.login = login ?? "";
  user.home = home ?? "";
  user.name = gecos?.trim() || login || "";
  user.host = host ?? "";
  user.uptime = (uptime ?? "").replace(/^up\s+/, "");
}

let RESOURCES = `
head -1 /proc/stat
grep -E '^(MemTotal|MemAvailable|SwapTotal|SwapFree):' /proc/meminfo
echo "disk $(df -B1 --output=size,used / | tail -1)"
echo "temp $(cat /sys/class/thermal/thermal_zone*/temp 2>/dev/null | sort -n | tail -1)"
echo "cores $(nproc)"
echo "load $(cut -d' ' -f1-3 /proc/loadavg)"
`;

export let resources = reactive({
  cpu: 0,
  memory: 0,
  memoryUsed: 0,
  memoryTotal: 0,
  swap: 0,
  disk: 0,
  diskUsed: 0,
  diskTotal: 0,
  temp: null,
  cores: 0,
  load: "",
  history: [],
});

let previous = null;
let users = 0;
let timer = 0;

async function sample() {
  let result = await sh(RESOURCES);
  if (!result) return;
  let values = {};
  for (let line of result.stdout.split("\n")) {
    let parts = line.trim().split(/\s+/);
    values[parts[0].replace(/:$/, "")] = parts.slice(1);
  }

  if (values.cpu) {
    let nums = values.cpu.map(Number);
    let idle = nums[3] + (nums[4] ?? 0);
    let total = nums.reduce((a, b) => a + b, 0);
    if (previous) {
      let dt = total - previous.total;
      resources.cpu = dt > 0 ? 1 - (idle - previous.idle) / dt : 0;
    }
    previous = { idle, total };
  }

  let memTotal = Number(values.MemTotal?.[0] ?? 0) * 1024;
  let memAvail = Number(values.MemAvailable?.[0] ?? 0) * 1024;
  resources.memoryTotal = memTotal;
  resources.memoryUsed = memTotal - memAvail;
  resources.memory = memTotal ? resources.memoryUsed / memTotal : 0;

  let swapTotal = Number(values.SwapTotal?.[0] ?? 0);
  resources.swap = swapTotal ? 1 - Number(values.SwapFree?.[0] ?? 0) / swapTotal : 0;

  if (values.disk?.length == 2) {
    resources.diskTotal = Number(values.disk[0]);
    resources.diskUsed = Number(values.disk[1]);
    resources.disk = resources.diskTotal ? resources.diskUsed / resources.diskTotal : 0;
  }

  resources.temp = values.temp?.[0] ? Number(values.temp[0]) / 1000 : null;
  resources.cores = Number(values.cores?.[0] ?? 0);
  resources.load = (values.load ?? []).join("  ");
  resources.history = [...resources.history, resources.cpu].slice(-40);
}

export function watchResources() {
  users++;
  if (users == 1) {
    sample();
    timer = setInterval(sample, 2000);
  }
  return function stop() {
    users--;
    if (users == 0) clearInterval(timer);
  };
}

let WEATHER_ICONS = [
  [[113], "clear_day", "clear_night"],
  [[116], "partly_cloudy_day", "partly_cloudy_night"],
  [[119, 122], "cloud", "cloud"],
  [[143, 248, 260], "foggy", "foggy"],
  [[176, 263, 266, 293, 296, 299, 302, 305, 308, 311, 314, 353, 356, 359], "rainy", "rainy"],
  [[179, 182, 185, 227, 230, 281, 284, 317, 320, 323, 326, 329, 332, 335, 338, 350, 362, 365, 368, 371, 374, 377], "weather_snowy", "weather_snowy"],
  [[200, 386, 389, 392, 395], "thunderstorm", "thunderstorm"],
];

export let weather = shallowRef(null);
let weatherAt = 0;

export async function loadWeather() {
  if (Date.now() - weatherAt < 20 * 60 * 1000 && weather.value) return;
  let result = await exec("curl", ["-sf", "--max-time", "8", "https://wttr.in/?format=j1"]);
  if (!result || result.status != 0) return;
  try {
    let data = JSON.parse(result.stdout);
    let current = data.current_condition[0];
    let today = data.weather?.[0];
    let code = Number(current.weatherCode);
    let hour = new Date().getHours();
    let night = hour < 6 || hour >= 20;
    let match = WEATHER_ICONS.find(([codes]) => codes.includes(code));
    weather.value = {
      temp: Number(current.temp_C),
      feels: Number(current.FeelsLikeC),
      humidity: Number(current.humidity),
      description: current.weatherDesc?.[0]?.value ?? "",
      place: data.nearest_area?.[0]?.areaName?.[0]?.value ?? "",
      high: today ? Number(today.maxtempC) : null,
      low: today ? Number(today.mintempC) : null,
      icon: match ? (night ? match[2] : match[1]) : "thermostat",
    };
    weatherAt = Date.now();
  } catch {
    return;
  }
}

export let profiles = reactive({ available: false, current: "", list: [] });

export async function loadProfiles() {
  let result = await exec("powerprofilesctl", ["list"]);
  if (!result || result.status != 0) return;
  let list = [];
  let current = "";
  for (let line of result.stdout.split("\n")) {
    let match = line.match(/^(\*)?\s*([\w-]+):$/);
    if (!match) continue;
    list.push(match[2]);
    if (match[1]) current = match[2];
  }
  profiles.available = list.length > 0;
  profiles.list = list.reverse();
  profiles.current = current;
}

export async function setProfile(name) {
  profiles.current = name;
  await exec("powerprofilesctl", ["set", name]);
  loadProfiles();
}

export let SESSION = [
  { id: "lock", icon: "lock", label: "Lock", command: "loginctl lock-session" },
  { id: "logout", icon: "logout", label: "Log out", command: 'loginctl terminate-user "$(id -un)"' },
  { id: "suspend", icon: "bedtime", label: "Sleep", command: "systemctl suspend" },
  { id: "hibernate", icon: "downloading", label: "Hibernate", command: "systemctl hibernate" },
  { id: "reboot", icon: "restart_alt", label: "Restart", command: "systemctl reboot" },
  { id: "poweroff", icon: "power_settings_new", label: "Shut down", command: "systemctl poweroff" },
];

export function sessionAction(id) {
  let action = SESSION.find(a => a.id == id);
  if (action) sh(action.command);
}

export let now = ref(Date.now());
setInterval(() => (now.value = Date.now()), 1000);
