#pragma once

#include "glib.h"
#include "state.h"

void notifications_init(AppState *state);
void notifications_clear(AppState *state);

GVariant *notifications_list(AppState *state);
gboolean notifications_close(AppState *state, guint32 id);
gboolean notifications_invoke(AppState *state, guint32 id, const gchar *key);
gboolean notifications_serve(AppState *state, WebKitURISchemeRequest *request, const gchar *path);
