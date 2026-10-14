#pragma once

#include "state.h"

typedef void (*NetworkConnectCallback)(const gchar *error, gpointer user_data);

void network_init(AppState *state);
void network_clear(AppState *state);

GVariant *network_snapshot(AppState *state);
gboolean network_set_wifi(AppState *state, gboolean enabled);
gboolean network_scan(AppState *state);
gboolean network_disconnect(AppState *state);
gboolean network_connect(AppState *state, const gchar *ap, const gchar *password, NetworkConnectCallback callback, gpointer user_data);
