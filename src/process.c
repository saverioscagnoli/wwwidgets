#include <glib.h>
#include <gio/gio.h>

#include "process.h"
#include "glib-object.h"
#include "state.h"

typedef struct {
    ProcessExecCallback callback;
    gpointer user_data;
} ExecRequest;

typedef struct {
    AppState *state;
    guint32 id;
    GSubprocess *proc;
    GDataInputStream *out;
    GDataInputStream *err;
    GCancellable *cancellable;
    gint pending;
} Process;

static gchar *bytes_str(GBytes *bytes) {
    gsize len = 0;
    const gchar *data = bytes ? g_bytes_get_data(bytes, &len) : NULL;

    return g_utf8_make_valid(data ? data : "", len);
}

static gint exit_status(GSubprocess *proc) {
    return g_subprocess_get_if_exited(proc) ? g_subprocess_get_exit_status(proc) : -1;
}

static void on_exec_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    ExecRequest *r = user_data;
    GSubprocess *proc = G_SUBPROCESS(source);
    g_autoptr(GBytes) out = NULL;
    g_autoptr(GBytes) err_out = NULL;
    g_autoptr(GError) err = NULL;

    if (!g_subprocess_communicate_finish(proc, res, &out, &err_out, &err)) {
        r->callback(NULL, err->message, r->user_data);
        g_free(r);
        return;
    }

    g_autofree gchar *out_str = bytes_str(out);
    g_autofree gchar *err_str = bytes_str(err_out);
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "stdout", g_variant_new_string(out_str));
    g_variant_builder_add(&b, "{sv}", "stderr", g_variant_new_string(err_str));
    g_variant_builder_add(&b, "{sv}", "status", g_variant_new_int32(exit_status(proc)));

    g_autoptr(GVariant) result = g_variant_ref_sink(g_variant_builder_end(&b));

    r->callback(result, NULL, r->user_data);
    g_free(r);
}

void process_exec(AppState *state, const gchar *const *argv, ProcessExecCallback callback, gpointer user_data) {
    (void)state;

    g_autoptr(GError) err = NULL;
    g_autoptr(GSubprocess) proc = g_subprocess_newv(argv, G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE, &err);

    if (!proc) {
        callback(NULL, err->message, user_data);
        return;
    }

    ExecRequest *r = g_new0(ExecRequest, 1);

    r->callback = callback;
    r->user_data = user_data;
    g_subprocess_communicate_async(proc, NULL, NULL, on_exec_done, r);
}

static void process_free(Process *p) {
    if (p->pending > 0)
        g_subprocess_send_signal(p->proc, 15);

    g_cancellable_cancel(p->cancellable);
    g_object_unref(p->cancellable);
    g_object_unref(p->out);
    g_object_unref(p->err);
    g_object_unref(p->proc);
    g_free(p);
}

static void process_push(Process *p, const gchar *type, GVariant *data) {
    GVariantBuilder b;

    g_variant_builder_init(&b, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&b, "{sv}", "id", g_variant_new_uint32(p->id));
    g_variant_builder_add(&b, "{sv}", "type", g_variant_new_string(type));
    g_variant_builder_add(&b, "{sv}", "data", data);

    g_autoptr(GVariant) detail = g_variant_ref_sink(g_variant_builder_end(&b));

    for (guint i = 0; i < p->state->views->len; i++) {
        push_event(g_ptr_array_index(p->state->views, i), "process", detail);
    }
}

static void process_done(Process *p) {
    if (--p->pending > 0)
        return;

    process_push(p, "exit", g_variant_new_int32(exit_status(p->proc)));
    g_hash_table_remove(p->state->processes, GUINT_TO_POINTER(p->id));
}

static void on_line(GObject *source, GAsyncResult *res, gpointer user_data);

static void read_next(Process *p, GDataInputStream *stream) {
    g_data_input_stream_read_line_async(stream, G_PRIORITY_DEFAULT, p->cancellable, on_line, p);
}

static void on_line(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;
    gsize len = 0;
    g_autofree gchar *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(source), res, &len, &err);

    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

    Process *p = user_data;

    if (line == NULL) {
        process_done(p);
        return;
    }

    g_autofree gchar *text = g_utf8_make_valid(line, len);

    process_push(p, source == G_OBJECT(p->out) ? "stdout" : "stderr", g_variant_new_string(text));
    read_next(p, G_DATA_INPUT_STREAM(source));
}

static void on_wait_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;

    if (!g_subprocess_wait_finish(G_SUBPROCESS(source), res, &err) && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

    process_done(user_data);
}

static GDataInputStream *line_stream(GInputStream *pipe) {
    GDataInputStream *stream = g_data_input_stream_new(pipe);

    g_data_input_stream_set_newline_type(stream, G_DATA_STREAM_NEWLINE_TYPE_ANY);
    return stream;
}

guint32 process_spawn(AppState *state, const gchar *const *argv, GError **error) {
    GSubprocessFlags flags = G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE;
    GSubprocess *proc = g_subprocess_newv(argv, flags, error);

    if (!proc)
        return 0;

    Process *p = g_new0(Process, 1);

    p->state = state;
    p->id = ++state->process_next_id;
    p->proc = proc;
    p->out = line_stream(g_subprocess_get_stdout_pipe(proc));
    p->err = line_stream(g_subprocess_get_stderr_pipe(proc));
    p->cancellable = g_cancellable_new();
    p->pending = 3;

    g_hash_table_insert(state->processes, GUINT_TO_POINTER(p->id), p);
    read_next(p, p->out);
    read_next(p, p->err);
    g_subprocess_wait_async(proc, p->cancellable, on_wait_done, p);
    g_debug("process %u: spawned %s", p->id, argv[0]);

    return p->id;
}

static void on_written(GObject *source, GAsyncResult *res, gpointer user_data) {
    g_autoptr(GError) err = NULL;

    g_bytes_unref(user_data);

    if (!g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), res, NULL, &err) && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning("process: write: %s", err->message);
}

gboolean process_write(AppState *state, guint32 id, const gchar *data) {
    Process *p = g_hash_table_lookup(state->processes, GUINT_TO_POINTER(id));

    if (!p)
        return FALSE;

    GBytes *bytes = g_bytes_new(data, strlen(data));
    gsize len = 0;
    gconstpointer buf = g_bytes_get_data(bytes, &len);

    g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(p->proc), buf, len, G_PRIORITY_DEFAULT, p->cancellable, on_written, bytes);
    return TRUE;
}

gboolean process_kill(AppState *state, guint32 id, gint signal) {
    Process *p = g_hash_table_lookup(state->processes, GUINT_TO_POINTER(id));

    if (!p)
        return FALSE;

    g_subprocess_send_signal(p->proc, signal);
    return TRUE;
}

void process_init(AppState *state) {
    state->processes = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)process_free);
}

void process_clear(AppState *state) {
    g_clear_pointer(&state->processes, g_hash_table_unref);
}
