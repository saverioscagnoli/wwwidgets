#pragma once

#include "state.h"

void audio_init(AppState *state);
void audio_clear(AppState *state);

GVariant *audio_snapshot(AppState *state);

gboolean audio_set_volume(AppState *state, const gchar *device, gdouble volume);
gboolean audio_set_mute(AppState *state, const gchar *device, gboolean muted);
gboolean audio_set_default(AppState *state, const gchar *kind, const gchar *name);
gboolean audio_set_device_volume(AppState *state, const gchar *kind, const gchar *name, gdouble volume);
gboolean audio_set_device_mute(AppState *state, const gchar *kind, const gchar *name, gboolean muted);
gboolean audio_set_stream_volume(AppState *state, const gchar *kind, guint32 index, gdouble volume);
gboolean audio_set_stream_mute(AppState *state, const gchar *kind, guint32 index, gboolean muted);
gboolean audio_move_stream(AppState *state, const gchar *kind, guint32 index, const gchar *device);
