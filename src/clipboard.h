#pragma once

#include "state.h"

void clipboard_init(AppState *state);
void clipboard_clear(AppState *state);
GVariant *clipboard_history(AppState *state);
gboolean clipboard_set(AppState *state, const gchar *text);
gboolean clipboard_copy(AppState *state, guint32 id);
gboolean clipboard_remove(AppState *state, guint32 id);
gboolean clipboard_wipe(AppState *state);
gboolean clipboard_serve(AppState *state, WebKitURISchemeRequest *request, const gchar *path);
