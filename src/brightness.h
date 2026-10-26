#pragma once

#include "state.h"

void brightness_init(AppState *state);
void brightness_clear(AppState *state);

GVariant *brightness_snapshot(AppState *state);
gboolean brightness_set(AppState *state, gdouble value);
