#pragma once

#include "state.h"

void media_init(AppState *state);
void media_clear(AppState *state);

GVariant *media_list(AppState *state);
gboolean media_action(AppState *state, guint32 id, const gchar *action);
gboolean media_set_position(AppState *state, guint32 id, gdouble seconds);
