#!/usr/bin/env python3
# Prints installed applications as JSON for the launcher.
# Icons are resolved through the GTK icon theme to file paths.

import json
import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")

from gi.repository import Gdk, Gio, Gtk  # noqa: E402

SIZE = 64


def icon_theme():
    try:
        Gtk.init()
        display = Gdk.Display.get_default()
        return Gtk.IconTheme.get_for_display(display) if display else None
    except Exception:
        return None


def resolve(theme, icon):
    if icon is None:
        return None

    if isinstance(icon, Gio.FileIcon):
        return icon.get_file().get_path()

    if theme is None or not isinstance(icon, Gio.ThemedIcon):
        return None

    for name in icon.get_names():
        if theme.has_icon(name):
            paintable = theme.lookup_icon(name, None, SIZE, 1, Gtk.TextDirection.NONE, 0)
            file = paintable.get_file()
            if file and file.get_path():
                return file.get_path()

    return None


def main():
    theme = icon_theme()
    apps = []

    for app in Gio.AppInfo.get_all():
        if not app.should_show():
            continue

        keywords = app.get_keywords() if isinstance(app, Gio.DesktopAppInfo) else []

        apps.append({
            "id": app.get_id(),
            "name": app.get_display_name() or app.get_name(),
            "comment": app.get_description() or "",
            "keywords": list(keywords or []),
            "icon": resolve(theme, app.get_icon()),
        })

    apps.sort(key=lambda a: a["name"].lower())
    json.dump(apps, sys.stdout)


if __name__ == "__main__":
    main()
