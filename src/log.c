#include "log.h"

#include <glib.h>
#include <stdio.h>

static GLogWriterOutput log_writer(GLogLevelFlags level, const GLogField *fields, gsize n_fields,
                                   gpointer user_data) {
    (void)user_data;

    const char *message = "", *domain = NULL;
    for (gsize i = 0; i < n_fields; i++) {
        if (g_str_equal(fields[i].key, "MESSAGE"))
            message = fields[i].value;
        else if (g_str_equal(fields[i].key, "GLIB_DOMAIN"))
            domain = fields[i].value;
    }

    /* Respect the debug domains set in log_init(). */
    if (g_log_writer_default_would_drop(level, domain))
        return G_LOG_WRITER_HANDLED;

    const char *name, *color;
    switch (level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:    name = "ERROR"; color = "\033[1;31m"; break;
    case G_LOG_LEVEL_CRITICAL: name = "CRIT "; color = "\033[1;35m"; break;
    case G_LOG_LEVEL_WARNING:  name = "WARN "; color = "\033[1;33m"; break;
    case G_LOG_LEVEL_MESSAGE:  name = "MSG  "; color = "\033[1;32m"; break;
    case G_LOG_LEVEL_INFO:     name = "INFO "; color = "\033[1;34m"; break;
    default:                   name = "DEBUG"; color = "\033[1;36m"; break;
    }

    const char *module = domain ? domain : "?";

    if (g_log_writer_supports_color(fileno(stderr)))
        fprintf(stderr, "%s%s\033[0m \033[90m[%s]\033[0m %s\n", color, name, module, message);
    else
        fprintf(stderr, "%s [%s] %s\n", name, module, message);
    fflush(stderr);

    return G_LOG_WRITER_HANDLED;
}

void log_init(void) {
    const char *debug_domains[] = { G_LOG_DOMAIN, NULL };
    g_log_writer_default_set_debug_domains(debug_domains);

    g_log_set_writer_func(log_writer, NULL, NULL);
}
