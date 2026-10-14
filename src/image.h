#pragma once

#include <gtk/gtk.h>
#include <webkit/webkit.h>

typedef struct {
    GBytes *bytes;
    gchar *mime;
} Image;

void image_clear(Image *img);
gboolean image_load_file(Image *img, GFile *file);
gboolean image_load_icon(Image *img, const gchar *icon, const gchar *search_path);
gboolean image_load_pixels(Image *img, gint width, gint height, gsize stride, GdkMemoryFormat format, GBytes *pixels);
void image_serve(Image *img, WebKitURISchemeRequest *request);
