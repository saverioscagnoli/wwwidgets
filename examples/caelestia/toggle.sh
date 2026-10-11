#!/bin/sh
# Toggle a drawer from a keybind: toggle.sh launcher|dashboard|session
# The drawer opens on the monitor the pointer last visited.

name="$1"
current="$(wwwidgets get caelestia.open 2>/dev/null)"

if [ "$current" = "\"$name\"" ]; then
  wwwidgets set caelestia.open null
else
  wwwidgets set caelestia.open "\"$name\""
fi
