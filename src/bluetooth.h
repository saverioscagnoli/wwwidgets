#pragma once

#include "state.h"

typedef void (*BluetoothCallback)(const gchar *error, gpointer user_data);

void bluetooth_init(AppState *state);
void bluetooth_clear(AppState *state);

GVariant *bluetooth_snapshot(AppState *state);
gboolean bluetooth_set_powered(AppState *state, gboolean powered);
gboolean bluetooth_scan(AppState *state, gboolean on);
gboolean bluetooth_connect(AppState *state, const gchar *path, gboolean connect, BluetoothCallback callback, gpointer user_data);
gboolean bluetooth_pair(AppState *state, const gchar *path, BluetoothCallback callback, gpointer user_data);
gboolean bluetooth_reply(AppState *state, gboolean accept, const gchar *value);
