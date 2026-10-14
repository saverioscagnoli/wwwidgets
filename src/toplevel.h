#pragma once

#include "state.h"

void toplevels_init(AppState *state);
GVariant *toplevel_list(AppState *state);
gboolean toplevel_action(AppState *state, guint32 id, const gchar *action);
