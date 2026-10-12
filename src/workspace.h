#pragma once

#include "glib.h"
#include "state.h"

void workspace_init(AppState *state);

GVariant *workspace_list(AppState *state);
GVariant *workspace_active(AppState *state);
gboolean workspace_activate(AppState *state, const gchar *id);
