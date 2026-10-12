#include <gio/gio.h>

#include "image.h"
#include "gdk/gdk.h"
#include "glib.h"

void image_clear(Image *img) {
    g_clear_pointer(&img->bytes, g_bytes_unref);
    g_clear_pointer(&img->mime, g_free);
}

gboolean image_load_file(Image *img, GFile *file) {
    gchar *contents;
    gsize len;

    if (!g_file_load_contents(file, NULL,  &contents, &len, NULL, NULL))
        return FALSE;

    g_autofree gchar *name = g_file_get_basename(file);
    g_autofree gchar *type = g_content_type_guess(name, (const guchar *)contents, len, NULL);

    img->bytes = g_bytes_new_take(contents, len);
    img->mime = g_content_type_get_mime_type(type);

    return TRUE;
}

static gboolean image_load_themed(Image *img, GtkIconTheme *theme, const gchar *icon) {
    if (!gtk_icon_theme_has_icon(theme, icon))
        return FALSE;

    g_autoptr(GtkIconPaintable) paintable = gtk_icon_theme_lookup_icon(theme, icon, NULL, 64, 1, GTK_TEXT_DIR_NONE, 0);
    g_autoptr(GFile) file = gtk_icon_paintable_get_file(paintable);

    return file && image_load_file(img, file);
}

gboolean image_load_icon(Image *img, const gchar *icon, const gchar *search_path) {
    if (!icon || !*icon)
        return FALSE;

    if (g_str_has_prefix(icon, "file://")) {
        g_autoptr(GFile) file = g_file_new_for_uri(icon);
        return image_load_file(img, file);
    }

    if (g_path_is_absolute(icon)) {
        g_autoptr(GFile) file = g_file_new_for_path(icon);
        return image_load_file(img, file);
    }

    if (search_path && *search_path) {
        g_autoptr(GtkIconTheme) extra = gtk_icon_theme_new();
        const gchar *paths[] = { search_path, NULL };

        gtk_icon_theme_set_search_path(extra, paths);

        if (image_load_themed(img, extra, icon))
            return TRUE;
    }

    return image_load_themed(img, gtk_icon_theme_get_for_display(gdk_display_get_default()), icon);
}

gboolean image_load_pixels(Image *img, gint width, gint height, gsize stride, GdkMemoryFormat format, GBytes *pixels) {
    g_autoptr(GdkTexture) texture = gdk_memory_texture_new(width, height, format, pixels, stride);

    img->bytes = gdk_texture_save_to_png_bytes(texture);
    img->mime = g_strdup("image/png");

    return TRUE;
}

void image_serve(Image *img, WebKitURISchemeRequest *request) {
    g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_bytes(img->bytes);
    webkit_uri_scheme_request_finish(request, stream, g_bytes_get_size(img->bytes), img->mime);
}
