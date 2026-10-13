#include "state.h"
#include <glib.h>
#include <iso646.h>
#include <pulse/def.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <stdint.h>

typedef struct {
    gchar *name;
    gchar *description;
    guint8 channels;
    gdouble volume;
    gboolean muted;
} AudioDevice;

struct Audio {
    pa_glib_mainloop *loop;
    pa_context *ctx;
    AudioDevice sink;
    AudioDevice source;
    guint refresh_id;
    guint reconnect_id;
};

static void op_unref(pa_operation *o) {
    if (o)
        pa_operation_unref(o);
}

static void device_clear(AudioDevice *d) {
    g_clear_pointer(&d->name, g_free);
    g_clear_pointer(&d->description, g_free);
}

static void device_set(AudioDevice *d, const char *name, const char *description, const pa_cvolume *volume, int mute) {
    device_clear(d);

    d->name = g_strdup(name);
    d->description = g_strdup(description);
    d->channels = volume->channels;
    d->volume = (gdouble)pa_cvolume_max(volume) / PA_VOLUME_NORM;
    d->muted = mute;
}

static GVariant *device_variant(const AudioDevice *d) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(d->name));
    g_variant_builder_add(&b, "{sv}", "description", g_variant_new_string(d->description ? d->description : d->name));
    g_variant_builder_add(&b, "{sv}", "volume", g_variant_new_double(d->volume));
    g_variant_builder_add(&b, "{sv}", "muted", g_variant_new_boolean(d->muted));

    return g_variant_builder_end(&b);
}

GVariant *audio_snapshot(AppState *state) {
    Audio *a = state->audio;
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);

    if (a->sink.name)
        g_variant_builder_add(&b, "{sv}", "sink", device_variant(&a->sink));

    if (a->source.name)
        g_variant_builder_add(&b, "{sv}", "source", device_variant(&a->source));

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void audio_push(AppState *state) {
    g_autoptr(GVariant) snap = audio_snapshot(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "audio", snap);
    }
}

static void on_sink_info(pa_context *c, const pa_sink_info *i, int eol, void *user_data) {
    (void)c;

    AppState *state = user_data;

    if (eol || i == NULL)
        return;

    device_set(&state->audio->sink, i->name, i->description, &i->volume, i->mute);
    audio_push(state);
}

static void on_source_info(pa_context *c, const pa_source_info *i, int eol, void *user_data) {
    (void)c;

    AppState *state = user_data;

    if (eol || i == NULL)
        return;

    device_set(&state->audio->source, i->name, i->description, &i->volume, i->mute);
    audio_push(state);
}

static void on_server_info(pa_context *c, const pa_server_info *i, void *user_data) {
    if (i == NULL)
        return;

    if (i->default_sink_name)
        op_unref(pa_context_get_sink_info_by_name(c, i->default_sink_name, on_sink_info, user_data));

    if (i->default_source_name)
        op_unref(pa_context_get_source_info_by_name(c, i->default_source_name, on_source_info, user_data));
}

static gboolean refresh(gpointer user_data) {
    AppState *state = user_data;

    state->audio->refresh_id = 0;
    op_unref(pa_context_get_server_info(state->audio->ctx, on_server_info, state));

    return G_SOURCE_REMOVE;
}

static void on_subscribe(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *user_data) {
    (void)c;
    (void)t;
    (void)idx;
    AppState *state = user_data;

    if (state->audio->refresh_id == 0)
        state->audio->refresh_id = g_idle_add(refresh, state);
}

static void audio_connect(AppState *state);

static gboolean reconnect(gpointer user_data) {
    AppState *state = user_data;

    state->audio->reconnect_id = 0;
    pa_context_unref(state->audio->ctx);
    audio_connect(state);
    g_debug("audio is reconnecting");

    return G_SOURCE_REMOVE;
}

static void on_context_state(pa_context *c, void *user_data) {
    AppState *state = user_data;
    Audio *a = state->audio;

    switch (pa_context_get_state(c)) {
        case PA_CONTEXT_READY:
            pa_context_set_subscribe_callback(c, on_subscribe, state);
            op_unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL));
            refresh(state);
            g_debug("audio context ready");
            break;
        case PA_CONTEXT_FAILED:
            g_warning("audio: %s", pa_strerror(pa_context_errno(c)));
            pa_context_set_state_callback(c, NULL, NULL);
            g_clear_handle_id(&a->refresh_id, g_source_remove);
            device_clear(&a->sink);
            device_clear(&a->source);
            audio_push(state);

            if (a->reconnect_id == 0)
                a->reconnect_id = g_timeout_add_seconds(1, reconnect, state);
            break;
        default:
            break;
    }
}

static void audio_connect(AppState *state) {
    Audio *a = state->audio;

    a->ctx = pa_context_new(pa_glib_mainloop_get_api(a->loop), "wwwidgets");
    pa_context_set_state_callback(a->ctx, on_context_state, state);

    if (pa_context_connect(a->ctx, NULL, PA_CONTEXT_NOFAIL, NULL) < 0)
        g_warning("audio: %s", pa_strerror(pa_context_errno(a->ctx)));
}

void audio_init(AppState *state) {
    state->audio = g_new0(Audio, 1);
    state->audio->loop = pa_glib_mainloop_new(NULL);
    audio_connect(state);
}

void audio_clear(AppState *state) {
    Audio *a = state->audio;

    if (a == NULL)
        return;

    g_clear_handle_id(&a->refresh_id, g_source_remove);
    g_clear_handle_id(&a->reconnect_id, g_source_remove);
    pa_context_set_state_callback(a->ctx, NULL, NULL);
    pa_context_disconnect(a->ctx);
    pa_context_unref(a->ctx);
    pa_glib_mainloop_free(a->loop);
    device_clear(&a->sink);
    device_clear(&a->source);
    g_clear_pointer(&state->audio, g_free);
}

static AudioDevice *device_for(Audio *a, const gchar *device) {
    if (a == NULL || pa_context_get_state(a->ctx) != PA_CONTEXT_READY)
        return NULL;

    AudioDevice *d = g_str_equal(device, "sink") ? &a->sink : g_str_equal(device, "source") ? &a->source : NULL;
    return d && d->name ? d : NULL;
}

gboolean audio_set_volume(AppState *state, const gchar *device, gdouble volume) {
    AudioDevice *d = device_for(state->audio, device);
    pa_cvolume cv;

    if (d == NULL)
        return FALSE;

    pa_cvolume_set(&cv, d->channels, (pa_volume_t)(CLAMP(volume, 0.0, 1.5) * PA_VOLUME_NORM));

    if (d == &state->audio->sink)
        op_unref(pa_context_set_sink_volume_by_name(state->audio->ctx, d->name, &cv, NULL, NULL));
    else
        op_unref(pa_context_set_source_volume_by_name(state->audio->ctx, d->name, &cv, NULL, NULL));

    return TRUE;
}

gboolean audio_set_mute(AppState *state, const gchar *device, gboolean muted) {
    AudioDevice *d = device_for(state->audio, device);

    if (d == NULL)
        return FALSE;

    if (d == &state->audio->sink)
        op_unref(pa_context_set_sink_mute_by_name(state->audio->ctx, d->name, muted, NULL, NULL));
    else
        op_unref(pa_context_set_source_mute_by_name(state->audio->ctx, d->name, muted, NULL, NULL));

    return TRUE;
}
