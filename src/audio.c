#include <glib.h>
#include <pulse/def.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <stdint.h>

#include "audio.h"
#include "state.h"

typedef struct {
    guint32 index;
    gchar *name;
    gchar *description;
    gchar *title;
    gchar *icon;
    guint32 device;
    guint8 channels;
    gdouble volume;
    gboolean muted;
    gboolean corked;
} AudioNode;

struct Audio {
    pa_glib_mainloop *loop;
    pa_context *ctx;
    gchar *default_sink;
    gchar *default_source;
    GPtrArray *sinks;
    GPtrArray *sources;
    GPtrArray *playback;
    GPtrArray *record;
    guint refresh_id;
    guint reconnect_id;
    guint generation;
};

typedef struct {
    AppState *state;
    guint generation;
    gint pending;
    gchar *default_sink;
    gchar *default_source;
    GPtrArray *sinks;
    GPtrArray *sources;
    GPtrArray *playback;
    GPtrArray *record;
} Refresh;

static void op_unref(pa_operation *o) {
    if (o)
        pa_operation_unref(o);
}

static void node_free(AudioNode *n) {
    g_free(n->name);
    g_free(n->description);
    g_free(n->title);
    g_free(n->icon);
    g_free(n);
}

static GPtrArray *node_array(void) {
    return g_ptr_array_new_with_free_func((GDestroyNotify)node_free);
}

static AudioNode *node_new(guint32 index, const gchar *name, const gchar *description, const pa_cvolume *volume, int mute) {
    AudioNode *n = g_new0(AudioNode, 1);

    n->index = index;
    n->name = g_strdup(name);
    n->description = g_strdup(description ? description : name);
    n->channels = volume->channels;
    n->volume = (gdouble)pa_cvolume_max(volume) / PA_VOLUME_NORM;
    n->muted = mute;

    return n;
}

static const gchar *prop(pa_proplist *p, const gchar *key) {
    return p ? pa_proplist_gets(p, key) : NULL;
}

static AudioNode *stream_new(guint32 index, guint32 device, pa_proplist *p, const pa_cvolume *volume, int mute, int corked) {
    const gchar *app = prop(p, PA_PROP_APPLICATION_NAME);
    const gchar *binary = prop(p, PA_PROP_APPLICATION_PROCESS_BINARY);
    const gchar *icon = prop(p, PA_PROP_APPLICATION_ICON_NAME);
    AudioNode *n = node_new(index, NULL, app ? app : binary ? binary : "Unknown", volume, mute);

    n->title = g_strdup(prop(p, PA_PROP_MEDIA_NAME));
    n->icon = g_strdup(icon ? icon : binary);
    n->device = device;
    n->corked = corked;

    return n;
}

static gboolean stream_hidden(pa_proplist *p) {
    return g_strcmp0(prop(p, PA_PROP_APPLICATION_ID), "org.PulseAudio.pavucontrol") == 0
        || g_strcmp0(prop(p, PA_PROP_MEDIA_ROLE), "event") == 0
        || g_strcmp0(prop(p, "stream.monitor"), "true") == 0
        || g_strcmp0(prop(p, PA_PROP_APPLICATION_ID), "dev.svscagn.wwwidgets") == 0;
}

static AudioNode *find_index(GPtrArray *nodes, guint32 index) {
    for (guint i = 0; nodes && i < nodes->len; i++) {
        AudioNode *n = g_ptr_array_index(nodes, i);

        if (n->index == index)
            return n;
    }

    return NULL;
}

static AudioNode *find_name(GPtrArray *nodes, const gchar *name) {
    for (guint i = 0; nodes && name && i < nodes->len; i++) {
        AudioNode *n = g_ptr_array_index(nodes, i);

        if (g_strcmp0(n->name, name) == 0)
            return n;
    }

    return NULL;
}

static GVariant *device_variant(const AudioNode *n, const gchar *default_name) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "index", g_variant_new_uint32(n->index));
    g_variant_builder_add(&b, "{sv}", "name", g_variant_new_string(n->name));
    g_variant_builder_add(&b, "{sv}", "description", g_variant_new_string(n->description));
    g_variant_builder_add(&b, "{sv}", "volume", g_variant_new_double(n->volume));
    g_variant_builder_add(&b, "{sv}", "muted", g_variant_new_boolean(n->muted));
    g_variant_builder_add(&b, "{sv}", "default", g_variant_new_boolean(g_strcmp0(n->name, default_name) == 0));

    return g_variant_builder_end(&b);
}

static GVariant *stream_variant(const AudioNode *n, GPtrArray *devices) {
    AudioNode *device = find_index(devices, n->device);
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "index", g_variant_new_uint32(n->index));
    g_variant_builder_add(&b, "{sv}", "app", g_variant_new_string(n->description));
    g_variant_builder_add(&b, "{sv}", "title", g_variant_new_string(n->title ? n->title : ""));
    g_variant_builder_add(&b, "{sv}", "icon", g_variant_new_string(n->icon ? n->icon : ""));
    g_variant_builder_add(&b, "{sv}", "device", g_variant_new_string(device ? device->name : ""));
    g_variant_builder_add(&b, "{sv}", "volume", g_variant_new_double(n->volume));
    g_variant_builder_add(&b, "{sv}", "muted", g_variant_new_boolean(n->muted));
    g_variant_builder_add(&b, "{sv}", "corked", g_variant_new_boolean(n->corked));

    return g_variant_builder_end(&b);
}

static GVariant *list_variant(GPtrArray *nodes, gboolean streams, GPtrArray *devices, const gchar *default_name) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE("aa{sv}"));

    for (guint i = 0; nodes && i < nodes->len; i++) {
        AudioNode *n = g_ptr_array_index(nodes, i);
        g_variant_builder_add_value(&b, streams ? stream_variant(n, devices) : device_variant(n, default_name));
    }

    return g_variant_builder_end(&b);
}

GVariant *audio_snapshot(AppState *state) {
    Audio *a = state->audio;
    AudioNode *sink = find_name(a->sinks, a->default_sink);
    AudioNode *source = find_name(a->sources, a->default_source);
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);

    if (sink)
        g_variant_builder_add(&b, "{sv}", "sink", device_variant(sink, a->default_sink));

    if (source)
        g_variant_builder_add(&b, "{sv}", "source", device_variant(source, a->default_source));

    g_variant_builder_add(&b, "{sv}", "sinks", list_variant(a->sinks, FALSE, NULL, a->default_sink));
    g_variant_builder_add(&b, "{sv}", "sources", list_variant(a->sources, FALSE, NULL, a->default_source));
    g_variant_builder_add(&b, "{sv}", "playback", list_variant(a->playback, TRUE, a->sinks, NULL));
    g_variant_builder_add(&b, "{sv}", "record", list_variant(a->record, TRUE, a->sources, NULL));

    return g_variant_ref_sink(g_variant_builder_end(&b));
}

static void audio_push(AppState *state) {
    g_autoptr(GVariant) snap = audio_snapshot(state);

    for (guint i = 0; i < state->views->len; i++) {
        push_event(g_ptr_array_index(state->views, i), "audio", snap);
    }
}

static void lists_clear(Audio *a) {
    g_clear_pointer(&a->default_sink, g_free);
    g_clear_pointer(&a->default_source, g_free);
    g_clear_pointer(&a->sinks, g_ptr_array_unref);
    g_clear_pointer(&a->sources, g_ptr_array_unref);
    g_clear_pointer(&a->playback, g_ptr_array_unref);
    g_clear_pointer(&a->record, g_ptr_array_unref);
}

static void refresh_free(Refresh *r) {
    g_free(r->default_sink);
    g_free(r->default_source);
    g_clear_pointer(&r->sinks, g_ptr_array_unref);
    g_clear_pointer(&r->sources, g_ptr_array_unref);
    g_clear_pointer(&r->playback, g_ptr_array_unref);
    g_clear_pointer(&r->record, g_ptr_array_unref);
    g_free(r);
}

static void refresh_step(Refresh *r) {
    if (--r->pending > 0)
        return;

    Audio *a = r->state->audio;

    if (a && r->generation == a->generation) {
        lists_clear(a);
        a->default_sink = g_steal_pointer(&r->default_sink);
        a->default_source = g_steal_pointer(&r->default_source);
        a->sinks = g_steal_pointer(&r->sinks);
        a->sources = g_steal_pointer(&r->sources);
        a->playback = g_steal_pointer(&r->playback);
        a->record = g_steal_pointer(&r->record);
        audio_push(r->state);
    }

    refresh_free(r);
}

static void on_server_info(pa_context *c, const pa_server_info *i, void *user_data) {
    (void)c;

    Refresh *r = user_data;

    if (i) {
        r->default_sink = g_strdup(i->default_sink_name);
        r->default_source = g_strdup(i->default_source_name);
    }

    refresh_step(r);
}

static void on_sink_info(pa_context *c, const pa_sink_info *i, int eol, void *user_data) {
    (void)c;

    Refresh *r = user_data;

    if (eol) {
        refresh_step(r);
        return;
    }

    g_ptr_array_add(r->sinks, node_new(i->index, i->name, i->description, &i->volume, i->mute));
}

static void on_source_info(pa_context *c, const pa_source_info *i, int eol, void *user_data) {
    (void)c;

    Refresh *r = user_data;

    if (eol) {
        refresh_step(r);
        return;
    }

    if (i->monitor_of_sink == PA_INVALID_INDEX)
        g_ptr_array_add(r->sources, node_new(i->index, i->name, i->description, &i->volume, i->mute));
}

static void on_sink_input_info(pa_context *c, const pa_sink_input_info *i, int eol, void *user_data) {
    (void)c;

    Refresh *r = user_data;

    if (eol) {
        refresh_step(r);
        return;
    }

    if (i->has_volume && !stream_hidden(i->proplist))
        g_ptr_array_add(r->playback, stream_new(i->index, i->sink, i->proplist, &i->volume, i->mute, i->corked));
}

static void on_source_output_info(pa_context *c, const pa_source_output_info *i, int eol, void *user_data) {
    (void)c;

    Refresh *r = user_data;

    if (eol) {
        refresh_step(r);
        return;
    }

    if (i->has_volume && !stream_hidden(i->proplist))
        g_ptr_array_add(r->record, stream_new(i->index, i->source, i->proplist, &i->volume, i->mute, i->corked));
}

static gboolean refresh(gpointer user_data) {
    AppState *state = user_data;
    Audio *a = state->audio;
    Refresh *r = g_new0(Refresh, 1);

    a->refresh_id = 0;
    r->state = state;
    r->generation = ++a->generation;
    r->pending = 5;
    r->sinks = node_array();
    r->sources = node_array();
    r->playback = node_array();
    r->record = node_array();

    op_unref(pa_context_get_server_info(a->ctx, on_server_info, r));
    op_unref(pa_context_get_sink_info_list(a->ctx, on_sink_info, r));
    op_unref(pa_context_get_source_info_list(a->ctx, on_source_info, r));
    op_unref(pa_context_get_sink_input_info_list(a->ctx, on_sink_input_info, r));
    op_unref(pa_context_get_source_output_info_list(a->ctx, on_source_output_info, r));

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
            op_unref(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE
                                             | PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SOURCE_OUTPUT
                                             | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL));
            refresh(state);
            g_debug("audio context ready");
            break;
        case PA_CONTEXT_FAILED:
            g_warning("audio: %s", pa_strerror(pa_context_errno(c)));
            pa_context_set_state_callback(c, NULL, NULL);
            g_clear_handle_id(&a->refresh_id, g_source_remove);
            a->generation++;
            lists_clear(a);
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
    pa_proplist *props = pa_proplist_new();

    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "wwwidgets");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "dev.svscagn.wwwidgets");
    a->ctx = pa_context_new_with_proplist(pa_glib_mainloop_get_api(a->loop), "wwwidgets", props);
    pa_proplist_free(props);
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
    a->generation++;
    pa_context_set_state_callback(a->ctx, NULL, NULL);
    pa_context_disconnect(a->ctx);
    pa_context_unref(a->ctx);
    pa_glib_mainloop_free(a->loop);
    lists_clear(a);
    g_clear_pointer(&state->audio, g_free);
}

static gboolean ready(Audio *a) {
    return a != NULL && pa_context_get_state(a->ctx) == PA_CONTEXT_READY;
}

static gboolean is_sink(const gchar *kind) {
    return g_strcmp0(kind, "sink") == 0 || g_strcmp0(kind, "playback") == 0;
}

static gboolean valid_kind(const gchar *kind, gboolean stream) {
    return stream ? g_strcmp0(kind, "playback") == 0 || g_strcmp0(kind, "record") == 0
                  : g_strcmp0(kind, "sink") == 0 || g_strcmp0(kind, "source") == 0;
}

static AudioNode *device_for(Audio *a, const gchar *kind, const gchar *name) {
    if (!ready(a) || !valid_kind(kind, FALSE))
        return NULL;

    gboolean sink = is_sink(kind);

    return find_name(sink ? a->sinks : a->sources, name ? name : sink ? a->default_sink : a->default_source);
}

static AudioNode *stream_for(Audio *a, const gchar *kind, guint32 index) {
    if (!ready(a) || !valid_kind(kind, TRUE))
        return NULL;

    return find_index(is_sink(kind) ? a->playback : a->record, index);
}

static void volume_for(pa_cvolume *cv, const AudioNode *n, gdouble volume) {
    pa_cvolume_set(cv, n->channels, (pa_volume_t)(CLAMP(volume, 0.0, 1.5) * PA_VOLUME_NORM));
}

gboolean audio_set_device_volume(AppState *state, const gchar *kind, const gchar *name, gdouble volume) {
    AudioNode *d = device_for(state->audio, kind, name);
    pa_cvolume cv;

    if (d == NULL)
        return FALSE;

    volume_for(&cv, d, volume);

    if (is_sink(kind))
        op_unref(pa_context_set_sink_volume_by_name(state->audio->ctx, d->name, &cv, NULL, NULL));
    else
        op_unref(pa_context_set_source_volume_by_name(state->audio->ctx, d->name, &cv, NULL, NULL));

    return TRUE;
}

gboolean audio_set_device_mute(AppState *state, const gchar *kind, const gchar *name, gboolean muted) {
    AudioNode *d = device_for(state->audio, kind, name);

    if (d == NULL)
        return FALSE;

    if (is_sink(kind))
        op_unref(pa_context_set_sink_mute_by_name(state->audio->ctx, d->name, muted, NULL, NULL));
    else
        op_unref(pa_context_set_source_mute_by_name(state->audio->ctx, d->name, muted, NULL, NULL));

    return TRUE;
}

gboolean audio_set_default(AppState *state, const gchar *kind, const gchar *name) {
    AudioNode *d = name ? device_for(state->audio, kind, name) : NULL;

    if (d == NULL)
        return FALSE;

    if (is_sink(kind))
        op_unref(pa_context_set_default_sink(state->audio->ctx, d->name, NULL, NULL));
    else
        op_unref(pa_context_set_default_source(state->audio->ctx, d->name, NULL, NULL));

    return TRUE;
}

gboolean audio_set_volume(AppState *state, const gchar *device, gdouble volume) {
    return audio_set_device_volume(state, device, NULL, volume);
}

gboolean audio_set_mute(AppState *state, const gchar *device, gboolean muted) {
    return audio_set_device_mute(state, device, NULL, muted);
}

gboolean audio_set_stream_volume(AppState *state, const gchar *kind, guint32 index, gdouble volume) {
    AudioNode *s = stream_for(state->audio, kind, index);
    pa_cvolume cv;

    if (s == NULL)
        return FALSE;

    volume_for(&cv, s, volume);

    if (is_sink(kind))
        op_unref(pa_context_set_sink_input_volume(state->audio->ctx, s->index, &cv, NULL, NULL));
    else
        op_unref(pa_context_set_source_output_volume(state->audio->ctx, s->index, &cv, NULL, NULL));

    return TRUE;
}

gboolean audio_set_stream_mute(AppState *state, const gchar *kind, guint32 index, gboolean muted) {
    AudioNode *s = stream_for(state->audio, kind, index);

    if (s == NULL)
        return FALSE;

    if (is_sink(kind))
        op_unref(pa_context_set_sink_input_mute(state->audio->ctx, s->index, muted, NULL, NULL));
    else
        op_unref(pa_context_set_source_output_mute(state->audio->ctx, s->index, muted, NULL, NULL));

    return TRUE;
}

gboolean audio_move_stream(AppState *state, const gchar *kind, guint32 index, const gchar *device) {
    AudioNode *s = stream_for(state->audio, kind, index);
    AudioNode *d = s && device ? device_for(state->audio, is_sink(kind) ? "sink" : "source", device) : NULL;

    if (d == NULL)
        return FALSE;

    if (is_sink(kind))
        op_unref(pa_context_move_sink_input_by_name(state->audio->ctx, s->index, d->name, NULL, NULL));
    else
        op_unref(pa_context_move_source_output_by_name(state->audio->ctx, s->index, d->name, NULL, NULL));

    return TRUE;
}
