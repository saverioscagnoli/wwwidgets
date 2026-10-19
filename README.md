# wayland web widgets (wwwidgets)

Widget system that enables you to create widgets using any web technology!

## Install

```sh
git clone https://github.com/saverioscagnoli/wwwidgets
cd wwwidgets
make run
```

## Disclaimer

For now you shouldn't use it seriously, but just as a toy, since its untested and unfinished.

## Usage

It will look for `~/config/wwwidgets/config.json`, that should look like this:

```json
{
  "workspaces": true,
  "audio": true,
  "battery": true,
  "widgets": [
    {
      "path": "bar",
      "monitors": "all",
      "anchor": "top-left-right",
      "height": 32,
      "exclusive": true
    },
    {
      "path": "launcher",
      "anchor": "center",
      "width": "40%",
      "height": "50%",
      "layer": "overlay",
      "keyboard": "exclusive",
      "visible-when": "launcher.{monitor}",
      "hide": "unmap"
    }
  ]
}
```

### Schema:
| Key | Type | Default | Description |
|---|---|---|---|
| `devtools` | bool | `false` | WebKit inspector (shift + right click) |
| `hot_reload` | bool | `false` | Respawn widgets when files change |
| `workspaces` | bool | `false` | Workspace service |
| `notifications` | bool | `false` | Notification daemon |
| `tray` | bool | `false` | System tray |
| `audio` | bool | `false` | Audio service |
| `bluetooth` | bool | `false` | Bluetooth service |
| `network` | bool | `false` | Network service |
| `battery` | bool | `false` | Battery service |
| `exec` | bool | `false` | Allow widgets to run processes |
| `toplevels` | bool | `false` | Window list (foreign toplevel) |
| `widgets` | array | — | **Required**, list of widgets |

### Widget options

| Key | Type | Default | Description |
|---|---|---|---|
| `path` | string | — | Widget HTML file or directory [1] |
| `namespace` | string | `"wwwidgets"` | Layer shell namespace |
| `monitors` | string, array | `"all"` | Connector name(s), e.g. `"DP-1"` |
| `width` | number, `"N%"` | `"100%"` | Pixels or percent of monitor |
| `height` | number, `"N%"` | `"100%"` | Pixels or percent of monitor |
| `layer` | string | `"top"` | Layer shell layer [2] |
| `anchor` | string, array | `"center"` | Edges to anchor to [3] |
| `margin` | number, object | `0` | All edges, or per edge [4] |
| `exclusive` | bool, number | `false` | Exclusive zone [5] |
| `keyboard` | string | `"none"` | Keyboard mode [6] |
| `visible-when` | string | — | Store key controlling visibility [7] |
| `hide` | string | `"input"` | How to hide [8] |

1. Relative to the config dir, `~` expands, a directory loads its
   `index.html`. Without `path` the widget is an empty spacer (1x1).
2. `background`, `bottom`, `top`, `overlay`.
3. `top`, `bottom`, `left`, `right` joined with `-` (`"top-left"`),
   or `center`, `fill`.
4. `{ "top": 0, "bottom": 0, "left": 0, "right": 0 }`.
5. `true` sizes the zone automatically, a number sets it in pixels,
   `-1` ignores other zones.
6. `none`, `on-demand`, `exclusive`.
7. Shown while the value is truthy. `{monitor}` is replaced with
   the connector name.
8. `input` keeps the window mapped and drops input,
   `unmap` removes the window.

   ## License

   MIT License
