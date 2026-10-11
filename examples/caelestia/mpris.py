#!/usr/bin/env python3
# Follows MPRIS players on the session bus and prints the most relevant one as
# a JSON line whenever it changes ("null" when there is none).

import json
import sys

from gi.repository import Gio, GLib

PREFIX = "org.mpris.MediaPlayer2."
PATH = "/org/mpris/MediaPlayer2"
PLAYER = "org.mpris.MediaPlayer2.Player"

bus = Gio.bus_get_sync(Gio.BusType.SESSION)
players = {}  # bus name -> props dict
last = None


def call(name, iface, method, args=None, reply=None):
    return bus.call_sync(name, PATH, iface, method, args, reply, Gio.DBusCallFlags.NONE, 500)


def load(name):
    try:
        props = call(name, "org.freedesktop.DBus.Properties", "GetAll",
                     GLib.Variant("(s)", (PLAYER,))).unpack()[0]
    except GLib.Error:
        return

    try:
        identity = call(name, "org.freedesktop.DBus.Properties", "Get",
                        GLib.Variant("(ss)", ("org.mpris.MediaPlayer2", "Identity"))).unpack()[0]
    except GLib.Error:
        identity = name[len(PREFIX):]

    props["Identity"] = identity
    players[name] = props


def position(name):
    try:
        return call(name, "org.freedesktop.DBus.Properties", "Get",
                    GLib.Variant("(ss)", (PLAYER, "Position"))).unpack()[0]
    except GLib.Error:
        return 0


def pick():
    order = {"Playing": 0, "Paused": 1}
    names = sorted(players, key=lambda n: order.get(players[n].get("PlaybackStatus"), 2))
    return names[0] if names else None


def emit():
    global last
    name = pick()

    if name is None:
        out = None
    else:
        p = players[name]
        meta = p.get("Metadata", {})
        artist = meta.get("xesam:artist", [])
        out = {
            "bus": name,
            "identity": p.get("Identity", ""),
            "status": p.get("PlaybackStatus", "Stopped"),
            "title": meta.get("xesam:title", ""),
            "artist": ", ".join(artist) if isinstance(artist, list) else str(artist),
            "album": meta.get("xesam:album", ""),
            "art": meta.get("mpris:artUrl", ""),
            "length": meta.get("mpris:length", 0),
            "position": position(name),
            "canNext": p.get("CanGoNext", False),
            "canPrevious": p.get("CanGoPrevious", False),
        }

    line = json.dumps(out)
    if line != last:
        last = line
        print(line, flush=True)


def on_props(_conn, sender, _path, _iface, _signal, params):
    iface, changed, _ = params.unpack()
    if iface != PLAYER:
        return

    # Signals carry the unique name; map it back to the well-known one.
    for name in list(players):
        if owners.get(name) == sender:
            players[name].update(changed)
    emit()


def on_seeked(_conn, sender, *_):
    emit_force()


def emit_force():
    global last
    last = None
    emit()


owners = {}


def on_owner(_conn, _sender, _path, _iface, _signal, params):
    name, _old, new = params.unpack()
    if not name.startswith(PREFIX):
        return

    if new:
        owners[name] = new
        load(name)
    else:
        owners.pop(name, None)
        players.pop(name, None)
    emit()


def main():
    names = call("org.freedesktop.DBus", "org.freedesktop.DBus", "ListNames").unpack()[0]

    for name in names:
        if name.startswith(PREFIX):
            try:
                owners[name] = call("org.freedesktop.DBus", "org.freedesktop.DBus", "GetNameOwner",
                                    GLib.Variant("(s)", (name,))).unpack()[0]
            except GLib.Error:
                continue
            load(name)

    bus.signal_subscribe(None, "org.freedesktop.DBus.Properties", "PropertiesChanged", PATH,
                         None, Gio.DBusSignalFlags.NONE, on_props)
    bus.signal_subscribe(None, PLAYER, "Seeked", PATH, None, Gio.DBusSignalFlags.NONE, on_seeked)
    bus.signal_subscribe("org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                         "/org/freedesktop/DBus", None, Gio.DBusSignalFlags.NONE, on_owner)

    emit_force()
    GLib.MainLoop().run()


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, BrokenPipeError):
        sys.exit(0)
