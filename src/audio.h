#pragma once

#include "state.h"

void audio_init(AppState *state);
void audio_clear(AppState *state);

GVariant *audio_snapshot(AppState *state);

gboolean audio_set_volume(AppState *state, const gchar *device, gdouble volume);
gboolean audio_set_mute(AppState *state, const gchar *device, gboolean muted);
