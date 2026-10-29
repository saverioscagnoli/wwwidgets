# shell

A complete shell made with plain HTML, CSS and JS. No build step.

| Widget | What it does |
|---|---|
| `bar.html` | Launcher button, workspaces, taskbar, window title, clock, media, tray, network, bluetooth, volume, battery, notification bell |
| `launcher.html` | App search, open windows, `=` calculator, `>` run command, `:` clipboard history |
| `center.html` | Quick toggles, Wi-Fi and Bluetooth (with pairing), volume, mic, brightness, output devices, per-app mixer, media player, calendar, notifications, power menu |
| `toasts.html` | Notification popups |
| `osd.html` | Volume, mic and brightness popups |

## Run

Copy the folder to `~/.config/wwwidgets` (or point `-c` at `config.json` once that flag exists) and start `wwwidgets`.

## Keybinds

The launcher and the control center are toggled per monitor, so bind them in your compositor like this:

```sh
wwwidgets toggle shell.launcher.DP-1
wwwidgets toggle shell.center.DP-1
wwwidgets toggle shell.dnd
```

## Tips

- Scroll on the status icons to change the volume, middle click to mute.
- Scroll on the workspaces to switch, middle click a task to close it.
- Right click the media chip to skip the track.
- Power actions need a second click to confirm.
- Optional tools: `gtk-launch` for apps, `wl-copy` when the clipboard service is off, `systemctl` and `loginctl` for power.
