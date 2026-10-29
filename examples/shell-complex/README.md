# shell-complex

A Caelestia-style shell built with Vue 3 and Vite. Everything lives in one full-screen widget per monitor: a frame around the screen with a vertical bar on the left, and panels that grow out of the frame edges.

| Part | Where | Opens with |
|---|---|---|
| Bar | Left edge | Always visible: launcher button, workspaces, focused window, tray, clock, status icons, power |
| Popouts | Out of the bar | Hover a status icon or the window title, right click a tray icon |
| Dashboard | Top edge | Hover the top edge, click the clock, or the keybind. Tabs: dashboard, media, performance |
| Launcher | Bottom edge | Logo button or keybind. Type `>` for actions (`>calc`, `>run`, `>clip`, session actions) |
| Notifications | Top right corner | New notifications |
| OSD | Right edge | Volume, mic or brightness changes, or hover the right edge |
| Session | Right edge | Power button or keybind, arrow keys to pick |

## Build

```sh
npm install
npm run build
```

`npm run watch` rebuilds on save, and with `"hot-reload": true` wwwidgets reloads the widget after every rebuild.

## Run

Copy this folder (with `dist/`) to `~/.config/wwwidgets`, or point wwwidgets at `config.json`, and start it.

The four path-less widgets in `config.json` only reserve screen space for the frame (48px on the left, 10px elsewhere). The shell itself ignores those zones and covers the whole output, so change `geometry` in `src/lib/ui.js` together with those numbers.

## Keybinds

```sh
wwwidgets toggle complex.launcher.DP-1
wwwidgets toggle complex.dashboard.DP-1
wwwidgets toggle complex.session.DP-1
wwwidgets toggle complex.dnd
```

## Optional tools

- `gtk-launch` to start apps
- `curl` for the weather card (wttr.in)
- `powerprofilesctl` for power profiles in the battery popout
- `systemctl` and `loginctl` for the session menu
- `~/.face` is used as the avatar
