#include "app.h"

#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static GtkWidget *section_label(const char *text) { GtkWidget *label = gtk_label_new(text); gtk_widget_set_halign(label, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(label), "section"); return label; }

static gboolean pulse_glow(GtkWidget *widget) {
    if (gtk_widget_get_realized(widget)) gtk_widget_queue_draw(widget);
    return G_SOURCE_CONTINUE;
}

static void stop_glow(GtkWidget *widget, gpointer data) {
    (void)data;
    guint source = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "glow-source"));
    if (source) g_source_remove(source);
}

static gboolean draw_glow(GtkWidget *widget, cairo_t *cr, gpointer data) {
    (void)data;
    double width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget);
    double pulse = 0.5 + 0.5 * sin((double)g_get_monotonic_time() / 1000000.0 * 2.4);
    double alpha = 0.58 + pulse * 0.28;
    double radius = MIN(width, height) * 0.48;
    cairo_pattern_t *gradient = cairo_pattern_create_radial(width / 2.0, height / 2.0, 2.0, width / 2.0, height / 2.0, radius);
    cairo_pattern_add_color_stop_rgba(gradient, 0.0, 1.0, 1.0, 1.0, alpha);
    cairo_pattern_add_color_stop_rgba(gradient, 0.24, 0.55, 0.86, 1.0, alpha * 0.72);
    cairo_pattern_add_color_stop_rgba(gradient, 0.68, 0.12, 0.43, 1.0, alpha * 0.25);
    cairo_pattern_add_color_stop_rgba(gradient, 1.0, 0.05, 0.25, 0.9, 0.0);
    cairo_set_source(cr, gradient); cairo_arc(cr, width / 2.0, height / 2.0, radius, 0.0, 2.0 * G_PI); cairo_fill(cr); cairo_pattern_destroy(gradient);
    return FALSE;
}

static GtkWidget *brand_header(const char *text) {
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    char *path = logo_path(); GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, 42, 42, TRUE, NULL);
    if (pixbuf) {
        GtkWidget *overlay = gtk_overlay_new(); GtkWidget *glow = gtk_drawing_area_new();
        gtk_widget_set_size_request(glow, 88, 88); gtk_widget_set_halign(glow, GTK_ALIGN_CENTER); gtk_widget_set_valign(glow, GTK_ALIGN_CENTER); gtk_container_add(GTK_CONTAINER(overlay), glow);
        g_signal_connect(glow, "draw", G_CALLBACK(draw_glow), NULL); g_signal_connect(glow, "destroy", G_CALLBACK(stop_glow), NULL);
        guint source = g_timeout_add(33, (GSourceFunc)pulse_glow, glow); g_object_set_data(G_OBJECT(glow), "glow-source", GUINT_TO_POINTER(source));
        GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf); gtk_widget_set_halign(image, GTK_ALIGN_CENTER); gtk_widget_set_valign(image, GTK_ALIGN_CENTER); gtk_overlay_add_overlay(GTK_OVERLAY(overlay), image); gtk_box_pack_start(GTK_BOX(header), overlay, FALSE, FALSE, 0); g_object_unref(pixbuf);
    }
    g_free(path); GtkWidget *label = gtk_label_new(text); gtk_widget_set_halign(label, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(label), "brand"); gtk_box_pack_start(GTK_BOX(header), label, FALSE, FALSE, 0); return header;
}

static int widget_int(GtkWidget *widget, const char *key, int fallback) {
    gpointer value = g_object_get_data(G_OBJECT(widget), key);
    return value ? GPOINTER_TO_INT(value) : fallback;
}

static float *widget_rgba(GtkWidget *widget, const char *key) {
    return g_object_get_data(G_OBJECT(widget), key);
}

static void widget_text(GtkWidget *widget, const struct tm *local_time, char *text, gsize length) {
    const char *kind = g_object_get_data(G_OBJECT(widget), "widget-kind");
    const char *format = g_object_get_data(G_OBJECT(widget), "widget-format");
    if (!g_strcmp0(kind, "time")) strftime(text, length, g_strcmp0(format, "12") ? "%H:%M" : "%I:%M %p", local_time);
    else strftime(text, length, !g_strcmp0(format, "short") ? "%Y-%m-%d" : "%A, %B %d", local_time);
}

static int widget_character_count(const char *text) { return MAX(1, (int)g_utf8_strlen(text ? text : "", -1)); }

static double widget_text_advance(cairo_t *cr, const char *text, int spacing) {
    cairo_text_extents_t ext; cairo_text_extents(cr, text, &ext); return ext.x_advance + MAX(0, widget_character_count(text) - 1) * spacing;
}

static void widget_text_path(cairo_t *cr, const char *text, int spacing) {
    double x = 0; const char *cursor = text;
    while (cursor && *cursor) {
        gunichar character = g_utf8_get_char(cursor); char part[8]; int length = g_unichar_to_utf8(character, part); part[length] = '\0'; cairo_move_to(cr, x, 0); cairo_text_path(cr, part);
        cairo_text_extents_t ext; cairo_text_extents(cr, part, &ext); x += ext.x_advance + spacing; cursor = g_utf8_next_char(cursor);
    }
}

static gboolean draw_wallpaper_widget(GtkWidget *area, cairo_t *cr, GtkWidget *widget) {
    int area_width = gtk_widget_get_allocated_width(area), area_height = gtk_widget_get_allocated_height(area);
    const char *text = g_object_get_data(G_OBJECT(widget), "widget-text"); const char *font = g_object_get_data(G_OBJECT(widget), "widget-font");
    int font_size = widget_int(widget, "widget-size", 32); gboolean bold = widget_int(widget, "widget-bold", TRUE), italic = widget_int(widget, "widget-italic", FALSE);
    int padding = 8, spacing = widget_int(widget, "widget-spacing", 0); double stretch_x = widget_int(widget, "widget-stretch-x", 100) / 100.0, stretch_y = widget_int(widget, "widget-stretch-y", 100) / 100.0; double *preview_scale_x = g_object_get_data(G_OBJECT(widget), "widget-preview-scale-x"), *preview_scale_y = g_object_get_data(G_OBJECT(widget), "widget-preview-scale-y"); double render_scale_x = (preview_scale_x ? *preview_scale_x : 1.0) * stretch_x, render_scale_y = (preview_scale_y ? *preview_scale_y : 1.0) * stretch_y;
    float *text_color = widget_rgba(widget, "widget-color"), *gradient_color = widget_rgba(widget, "widget-gradient-color");
    if (!text || !font || !text_color || !gradient_color) return FALSE;
    int width = MAX(1, (int)ceil(area_width / render_scale_x)), height = MAX(1, (int)ceil(area_height / render_scale_y)); cairo_save(cr); cairo_scale(cr, render_scale_x, render_scale_y);
    cairo_select_font_face(cr, font, italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(cr, font_size);
    cairo_text_extents_t ext; cairo_font_extents_t metrics; cairo_text_extents(cr, text, &ext); cairo_font_extents(cr, &metrics); double x = padding, y = padding + (height - padding * 2 - metrics.height) / 2.0 + metrics.height;
    cairo_save(cr); cairo_translate(cr, x, y);
    if (widget_int(widget, "widget-gradient", FALSE)) {
        double angle = widget_int(widget, "widget-gradient-angle", 0) * G_PI / 180.0, cx = width / 2.0 - x, cy = height / 2.0 - y, dx = cos(angle) * width / 2.0, dy = sin(angle) * height / 2.0;
        widget_text_path(cr, text, spacing); cairo_pattern_t *gradient = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy); cairo_pattern_add_color_stop_rgba(gradient, 0, text_color[0], text_color[1], text_color[2], text_color[3]); cairo_pattern_add_color_stop_rgba(gradient, 1, gradient_color[0], gradient_color[1], gradient_color[2], gradient_color[3]); cairo_set_source(cr, gradient); cairo_fill(cr); cairo_pattern_destroy(gradient);
    } else { widget_text_path(cr, text, spacing); cairo_set_source_rgba(cr, text_color[0], text_color[1], text_color[2], text_color[3]); cairo_fill(cr); }
    cairo_restore(cr); cairo_restore(cr);
    return FALSE;
}

static void update_widget_label(GtkWidget *widget, const struct tm *local_time) {
    GtkWidget *area = g_object_get_data(G_OBJECT(widget), "widget-label"); char text[96]; widget_text(widget, local_time, text, sizeof(text));
    g_object_set_data_full(G_OBJECT(widget), "widget-text", g_strdup(text), g_free);
    const char *font = g_object_get_data(G_OBJECT(widget), "widget-font"); int font_size = widget_int(widget, "widget-size", 32), padding = 8, spacing = widget_int(widget, "widget-spacing", 0); double stretch_x = widget_int(widget, "widget-stretch-x", 100) / 100.0, stretch_y = widget_int(widget, "widget-stretch-y", 100) / 100.0; double *preview_scale_x = g_object_get_data(G_OBJECT(widget), "widget-preview-scale-x"), *preview_scale_y = g_object_get_data(G_OBJECT(widget), "widget-preview-scale-y"); double scale_x = preview_scale_x ? *preview_scale_x : 1.0, scale_y = preview_scale_y ? *preview_scale_y : 1.0;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *cr = cairo_create(surface); cairo_select_font_face(cr, font ? font : "Sans", widget_int(widget, "widget-italic", FALSE) ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, widget_int(widget, "widget-bold", TRUE) ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(cr, font_size); cairo_text_extents_t ext; cairo_font_extents_t metrics; cairo_text_extents(cr, text, &ext); cairo_font_extents(cr, &metrics); double text_width = widget_text_advance(cr, text, spacing); cairo_destroy(cr); cairo_surface_destroy(surface);
    int base_width = MAX(widget_int(widget, "widget-width", 0), MAX(1, (int)ceil(text_width + padding * 2))); int base_height = MAX(1, (int)ceil(metrics.height + padding * 2)); gtk_widget_set_size_request(area, MAX(1, (int)ceil(base_width * scale_x * stretch_x)), MAX(1, (int)ceil(base_height * scale_y * stretch_y))); gtk_widget_queue_draw(area);
}

static gboolean update_widget_labels(gpointer data) {
    App *app = data; time_t now = time(NULL); struct tm local_time;
    localtime_r(&now, &local_time);
    if (app->wallpaper_widgets) for (guint i = 0; i < app->wallpaper_widgets->len; i++) { GtkWidget *widget = g_ptr_array_index(app->wallpaper_widgets, i); double *scale_x = g_new(double, 1), *scale_y = g_new(double, 1); int canvas_width = gtk_widget_get_allocated_width(app->widget_canvas), canvas_height = gtk_widget_get_allocated_height(app->widget_canvas); *scale_x = canvas_width > 0 ? (double)canvas_width / MAX(1, app->widget_screen_width) : 1.0; *scale_y = canvas_height > 0 ? (double)canvas_height / MAX(1, app->widget_screen_height) : 1.0; g_object_set_data_full(G_OBJECT(widget), "widget-preview-scale-x", scale_x, g_free); g_object_set_data_full(G_OBJECT(widget), "widget-preview-scale-y", scale_y, g_free); update_widget_label(widget, &local_time); }
    return G_SOURCE_CONTINUE;
}

static void set_widget_rgba(GtkWidget *widget, const char *key, float r, float g, float b, float a) {
    float *color = g_new(float, 4); color[0] = r; color[1] = g; color[2] = b; color[3] = a; g_object_set_data_full(G_OBJECT(widget), key, color, g_free);
}

static void set_widget_style_defaults(GtkWidget *widget, const char *kind) {
    g_object_set_data_full(G_OBJECT(widget), "widget-font", g_strdup("Sans"), g_free); g_object_set_data(G_OBJECT(widget), "widget-size", GINT_TO_POINTER(!g_strcmp0(kind, "time") ? 48 : 26));
    g_object_set_data(G_OBJECT(widget), "widget-bold", GINT_TO_POINTER(TRUE)); g_object_set_data(G_OBJECT(widget), "widget-italic", GINT_TO_POINTER(FALSE));
    set_widget_rgba(widget, "widget-color", 0.90f, 0.96f, 0.98f, 1.0f); set_widget_rgba(widget, "widget-gradient-color", 0.45f, 0.80f, 1.0f, 1.0f);
    g_object_set_data(G_OBJECT(widget), "widget-gradient", GINT_TO_POINTER(FALSE)); g_object_set_data(G_OBJECT(widget), "widget-box", GINT_TO_POINTER(FALSE)); set_widget_rgba(widget, "widget-box-color", 0.01f, 0.03f, 0.05f, 0.0f); set_widget_rgba(widget, "widget-glow-color", 0.45f, 0.80f, 1.0f, 1.0f);
    g_object_set_data(G_OBJECT(widget), "widget-border", GINT_TO_POINTER(0)); g_object_set_data(G_OBJECT(widget), "widget-radius", GINT_TO_POINTER(0)); g_object_set_data(G_OBJECT(widget), "widget-glow", GINT_TO_POINTER(FALSE)); g_object_set_data(G_OBJECT(widget), "widget-glow-blur", GINT_TO_POINTER(12)); g_object_set_data(G_OBJECT(widget), "widget-glow-opacity", GINT_TO_POINTER(55)); g_object_set_data(G_OBJECT(widget), "widget-width", GINT_TO_POINTER(0)); g_object_set_data(G_OBJECT(widget), "widget-spacing", GINT_TO_POINTER(0)); g_object_set_data(G_OBJECT(widget), "widget-stretch-x", GINT_TO_POINTER(100)); g_object_set_data(G_OBJECT(widget), "widget-stretch-y", GINT_TO_POINTER(100)); g_object_set_data(G_OBJECT(widget), "widget-gradient-angle", GINT_TO_POINTER(0));
}

static void apply_widget_style_line(GtkWidget *widget, const char *line) {
    char kind[16], format[16], font[32] = "Sans"; double x, y; int size = widget_int(widget, "widget-size", 32), bold = TRUE, italic = FALSE, gradient = FALSE, box = FALSE, border = 0, radius = 0, glow = FALSE, glow_blur = 12, widget_width = 0, gradient_angle = 0, glow_opacity = 55, spacing = 0, stretch_x = 100, stretch_y = 100;
    float text_r = 0.90f, text_g = 0.96f, text_b = 0.98f, text_a = 1.0f, grad_r = 0.45f, grad_g = 0.80f, grad_b = 1.0f, grad_a = 1.0f, box_r = 0.01f, box_g = 0.03f, box_b = 0.05f, box_a = 0.0f, glow_r = 0.45f, glow_g = 0.80f, glow_b = 1.0f, glow_a = 1.0f;
    int fields = sscanf(line, "%15s %lf %lf %15s %31s %d %d %d %f %f %f %f %d %f %f %f %f %d %f %f %f %f %d %d %d %d %d %d %d %d %d %d %f %f %f %f", kind, &x, &y, format, font, &size, &bold, &italic, &text_r, &text_g, &text_b, &text_a, &gradient, &grad_r, &grad_g, &grad_b, &grad_a, &box, &box_r, &box_g, &box_b, &box_a, &border, &radius, &glow, &glow_blur, &widget_width, &gradient_angle, &glow_opacity, &spacing, &stretch_x, &stretch_y, &glow_r, &glow_g, &glow_b, &glow_a);
    if (fields < 5) return;
    for (char *p = font; *p; p++) if (*p == '_') *p = ' ';
    g_object_set_data_full(G_OBJECT(widget), "widget-font", g_strdup(font), g_free); g_object_set_data(G_OBJECT(widget), "widget-size", GINT_TO_POINTER(CLAMP(size, 8, 160))); g_object_set_data(G_OBJECT(widget), "widget-bold", GINT_TO_POINTER(!!bold)); g_object_set_data(G_OBJECT(widget), "widget-italic", GINT_TO_POINTER(!!italic));
    if (fields >= 12) set_widget_rgba(widget, "widget-color", text_r, text_g, text_b, text_a);
    if (fields >= 17) { g_object_set_data(G_OBJECT(widget), "widget-gradient", GINT_TO_POINTER(!!gradient)); set_widget_rgba(widget, "widget-gradient-color", grad_r, grad_g, grad_b, grad_a); }
    if (fields >= 22) { g_object_set_data(G_OBJECT(widget), "widget-box", GINT_TO_POINTER(FALSE)); set_widget_rgba(widget, "widget-box-color", box_r, box_g, box_b, 0.0f); }
    if (fields >= 23) g_object_set_data(G_OBJECT(widget), "widget-border", GINT_TO_POINTER(0));
    if (fields >= 24) g_object_set_data(G_OBJECT(widget), "widget-radius", GINT_TO_POINTER(0));
    if (fields >= 26) { g_object_set_data(G_OBJECT(widget), "widget-glow", GINT_TO_POINTER(!!glow)); g_object_set_data(G_OBJECT(widget), "widget-glow-blur", GINT_TO_POINTER(CLAMP(glow_blur, 1, 80))); }
    if (fields >= 27) g_object_set_data(G_OBJECT(widget), "widget-width", GINT_TO_POINTER(CLAMP(widget_width, 0, 1600)));
    if (fields >= 28) g_object_set_data(G_OBJECT(widget), "widget-gradient-angle", GINT_TO_POINTER(fields == 28 && gradient_angle <= 4 ? 0 : ((gradient_angle % 360) + 360) % 360));
    if (fields >= 29) g_object_set_data(G_OBJECT(widget), "widget-glow-opacity", GINT_TO_POINTER(CLAMP(glow_opacity, 0, 100)));
    if (fields >= 30) g_object_set_data(G_OBJECT(widget), "widget-spacing", GINT_TO_POINTER(CLAMP(spacing, 0, 40)));
    if (fields >= 31) g_object_set_data(G_OBJECT(widget), "widget-stretch-x", GINT_TO_POINTER(CLAMP(stretch_x, 50, 200)));
    if (fields >= 32) g_object_set_data(G_OBJECT(widget), "widget-stretch-y", GINT_TO_POINTER(CLAMP(stretch_y, 50, 200)));
    if (fields >= 36) set_widget_rgba(widget, "widget-glow-color", glow_r, glow_g, glow_b, glow_a);
}

static void save_wallpaper_widgets(App *app) {
    if (!app->wallpaper_widgets) return;
    GString *contents = g_string_new(NULL);
    for (guint i = 0; i < app->wallpaper_widgets->len; i++) {
        GtkWidget *widget = g_ptr_array_index(app->wallpaper_widgets, i); const char *kind = g_object_get_data(G_OBJECT(widget), "widget-kind"), *format = g_object_get_data(G_OBJECT(widget), "widget-format"), *font = g_object_get_data(G_OBJECT(widget), "widget-font"); char font_token[64] = "Sans"; if (font) g_strlcpy(font_token, font, sizeof(font_token)); for (char *p = font_token; *p; p++) if (*p == ' ') *p = '_'; double *x = g_object_get_data(G_OBJECT(widget), "widget-x"), *y = g_object_get_data(G_OBJECT(widget), "widget-y"); float *text_color = widget_rgba(widget, "widget-color"), *gradient_color = widget_rgba(widget, "widget-gradient-color"), *box_color = widget_rgba(widget, "widget-box-color"), *glow_color = widget_rgba(widget, "widget-glow-color");
        g_string_append_printf(contents, "%s %.6f %.6f %s %s %d %d %d %.3f %.3f %.3f %.3f %d %.3f %.3f %.3f %.3f %d %.3f %.3f %.3f %.3f %d %d %d %d %d %d %d %d %d %d %.3f %.3f %.3f %.3f\n", kind, x ? *x : 0.05, y ? *y : 0.05, format ? format : (!g_strcmp0(kind, "time") ? "24" : "long"), font_token, widget_int(widget, "widget-size", 32), widget_int(widget, "widget-bold", TRUE), widget_int(widget, "widget-italic", FALSE), text_color[0], text_color[1], text_color[2], text_color[3], widget_int(widget, "widget-gradient", FALSE), gradient_color[0], gradient_color[1], gradient_color[2], gradient_color[3], 0, box_color[0], box_color[1], box_color[2], 0.0, 0, 0, widget_int(widget, "widget-glow", FALSE), widget_int(widget, "widget-glow-blur", 12), widget_int(widget, "widget-width", 0), widget_int(widget, "widget-gradient-angle", 0), widget_int(widget, "widget-glow-opacity", 55), widget_int(widget, "widget-spacing", 0), widget_int(widget, "widget-stretch-x", 100), widget_int(widget, "widget-stretch-y", 100), glow_color[0], glow_color[1], glow_color[2], glow_color[3]);
    }
    char *path = data_path("widgets.conf"); g_file_set_contents(path, contents->str, -1, NULL); g_free(path); g_string_free(contents, TRUE);
}

static void set_widget_normalized_position(GtkWidget *widget, App *app, double x, double y) {
    double *old_x = g_object_get_data(G_OBJECT(widget), "widget-x"), *old_y = g_object_get_data(G_OBJECT(widget), "widget-y");
    if (!old_x) { old_x = g_new(double, 1); g_object_set_data_full(G_OBJECT(widget), "widget-x", old_x, g_free); }
    if (!old_y) { old_y = g_new(double, 1); g_object_set_data_full(G_OBJECT(widget), "widget-y", old_y, g_free); }
    *old_x = CLAMP(x, 0.0, 1.0); *old_y = CLAMP(y, 0.0, 1.0);
    int width = gtk_widget_get_allocated_width(app->widget_canvas), height = gtk_widget_get_allocated_height(app->widget_canvas);
    if (width > 0) gtk_widget_set_margin_start(widget, (int)(*old_x * width));
    if (height > 0) gtk_widget_set_margin_top(widget, (int)(*old_y * height));
}

static void snap_widget_position(App *app, GtkWidget *widget, int left, int top) {
    int canvas_width = gtk_widget_get_allocated_width(app->widget_canvas), canvas_height = gtk_widget_get_allocated_height(app->widget_canvas);
    double scale_x = (double)canvas_width / MAX(1, app->widget_screen_width), scale_y = (double)canvas_height / MAX(1, app->widget_screen_height);
    double step_x = 64.0 * scale_x, step_y = 64.0 * scale_y; if (step_x < 4.0) step_x = 4.0; if (step_y < 4.0) step_y = 4.0;
    double center_x = canvas_width / 2.0, center_y = canvas_height / 2.0;
    left = (int)round(center_x + round((left + gtk_widget_get_allocated_width(widget) / 2.0 - center_x) / step_x) * step_x - gtk_widget_get_allocated_width(widget) / 2.0);
    top = (int)round(center_y + round((top + gtk_widget_get_allocated_height(widget) / 2.0 - center_y) / step_y) * step_y - gtk_widget_get_allocated_height(widget) / 2.0);
    left = CLAMP(left, 0, MAX(0, canvas_width - gtk_widget_get_allocated_width(widget))); top = CLAMP(top, 0, MAX(0, canvas_height - gtk_widget_get_allocated_height(widget)));
    set_widget_normalized_position(widget, app, (double)left / MAX(1, canvas_width), (double)top / MAX(1, canvas_height));
}

static gboolean draw_widget_grid(GtkWidget *grid, cairo_t *cr, App *app) {
    double width = gtk_widget_get_allocated_width(grid), height = gtk_widget_get_allocated_height(grid); if (width <= 0 || height <= 0) return FALSE;
    double step_x = 64.0 * width / MAX(1, app->widget_screen_width), step_y = 64.0 * height / MAX(1, app->widget_screen_height); if (step_x < 4.0) step_x = 4.0; if (step_y < 4.0) step_y = 4.0;
    cairo_set_line_width(cr, 1.0); cairo_set_source_rgba(cr, 0.66, 0.88, 0.94, 0.18);
    for (double x = width / 2.0; x >= 0; x -= step_x) { cairo_move_to(cr, x, 0); cairo_line_to(cr, x, height); }
    for (double x = width / 2.0 + step_x; x <= width; x += step_x) { cairo_move_to(cr, x, 0); cairo_line_to(cr, x, height); }
    for (double y = height / 2.0; y >= 0; y -= step_y) { cairo_move_to(cr, 0, y); cairo_line_to(cr, width, y); }
    for (double y = height / 2.0 + step_y; y <= height; y += step_y) { cairo_move_to(cr, 0, y); cairo_line_to(cr, width, y); }
    cairo_stroke(cr); cairo_set_source_rgba(cr, 0.66, 0.88, 0.94, 0.55); cairo_move_to(cr, width / 2.0, 0); cairo_line_to(cr, width / 2.0, height); cairo_move_to(cr, 0, height / 2.0); cairo_line_to(cr, width, height / 2.0); cairo_stroke(cr); return FALSE;
}

static void position_wallpaper_widgets(GtkWidget *canvas, GtkAllocation *allocation, App *app) {
    (void)canvas; (void)allocation;
    for (guint i = 0; app->wallpaper_widgets && i < app->wallpaper_widgets->len; i++) {
        GtkWidget *widget = g_ptr_array_index(app->wallpaper_widgets, i); double *x = g_object_get_data(G_OBJECT(widget), "widget-x"), *y = g_object_get_data(G_OBJECT(widget), "widget-y");
        if (x && y) set_widget_normalized_position(widget, app, *x, *y);
    }
}

static void wallpaper_widget_map(GtkWidget *widget, App *app) {
    double *x = g_object_get_data(G_OBJECT(widget), "widget-x"), *y = g_object_get_data(G_OBJECT(widget), "widget-y");
    if (x && y) { snap_widget_position(app, widget, (int)(*x * gtk_widget_get_allocated_width(app->widget_canvas)), (int)(*y * gtk_widget_get_allocated_height(app->widget_canvas))); save_wallpaper_widgets(app); }
}

static gboolean draw_widget_color_preview(GtkWidget *area, cairo_t *cr, App *app) {
    GtkWidget *button = gtk_widget_get_parent(area); const char *key = g_object_get_data(G_OBJECT(button), "widget-color-key"); GtkWidget *widget = app->selected_widget; int width = gtk_widget_get_allocated_width(area), height = gtk_widget_get_allocated_height(area); if (!widget || !key) { cairo_set_source_rgb(cr, 0.12, 0.15, 0.16); cairo_paint(cr); return FALSE; }
    const char *color_key = !strcmp(key, "widget-box-color") ? "widget-box-color" : !strcmp(key, "widget-glow-color") ? "widget-glow-color" : "widget-color"; float *start = widget_rgba(widget, color_key), *end = widget_rgba(widget, "widget-gradient-color"); cairo_pattern_t *pattern = NULL;
    if (!strcmp(key, "widget-color") && widget_int(widget, "widget-gradient", FALSE)) { double angle = widget_int(widget, "widget-gradient-angle", 0) * G_PI / 180.0, cx = width / 2.0, cy = height / 2.0, dx = cos(angle) * width / 2.0, dy = sin(angle) * height / 2.0; pattern = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy); cairo_pattern_add_color_stop_rgba(pattern, 0, start[0], start[1], start[2], start[3]); cairo_pattern_add_color_stop_rgba(pattern, 1, end[0], end[1], end[2], end[3]); cairo_set_source(cr, pattern); } else cairo_set_source_rgba(cr, start[0], start[1], start[2], start[3]); cairo_rectangle(cr, 1, 1, MAX(1, width - 2), MAX(1, height - 2)); cairo_fill(cr); if (pattern) cairo_pattern_destroy(pattern); cairo_set_source_rgba(cr, 1, 1, 1, 0.55); cairo_set_line_width(cr, 1); cairo_rectangle(cr, 1, 1, MAX(1, width - 2), MAX(1, height - 2)); cairo_stroke(cr); return FALSE;
}

static GtkWidget *widget_color_button(App *app, const char *key) {
    GtkWidget *button = gtk_button_new(); GtkWidget *preview = gtk_drawing_area_new(); gtk_widget_set_size_request(preview, 88, 26); g_object_set_data(G_OBJECT(button), "widget-color-key", (gpointer)key); g_signal_connect(preview, "draw", G_CALLBACK(draw_widget_color_preview), app); gtk_container_add(GTK_CONTAINER(button), preview); return button;
}

static void refresh_widget_color_previews(App *app) {
    GtkWidget *buttons[] = { app->widget_settings_color }; for (guint i = 0; i < G_N_ELEMENTS(buttons); i++) if (buttons[i]) gtk_widget_queue_draw(gtk_bin_get_child(GTK_BIN(buttons[i])));
}

typedef struct { App *app; GtkWidget *window, *list, *search, *target, *button; } FontPicker;

static gboolean font_row_filter(GtkListBoxRow *row, gpointer data) {
    FontPicker *picker = data; const char *term = gtk_entry_get_text(GTK_ENTRY(picker->search)); GtkWidget *label = gtk_bin_get_child(GTK_BIN(row)); const char *name = gtk_label_get_text(GTK_LABEL(label)); char *lower_name = g_ascii_strdown(name, -1); char *lower_term = g_ascii_strdown(term, -1); gboolean visible = !lower_term[0] || strstr(lower_name, lower_term) != NULL; g_free(lower_name); g_free(lower_term); return visible;
}

static void font_row_activated(GtkListBox *list, GtkListBoxRow *row, FontPicker *picker) {
    (void)list; GtkWidget *label = gtk_bin_get_child(GTK_BIN(row)); const char *font = gtk_label_get_text(GTK_LABEL(label));
    if (picker->target && picker->app->selected_widget) { g_object_set_data_full(G_OBJECT(picker->target), "widget-font", g_strdup(font), g_free); gtk_button_set_label(GTK_BUTTON(picker->button), font); update_widget_labels(picker->app); save_wallpaper_widgets(picker->app); }
    gtk_widget_destroy(picker->window);
}

static void font_search_changed(GtkEditable *editable, FontPicker *picker) { (void)editable; gtk_list_box_invalidate_filter(GTK_LIST_BOX(picker->list)); }
static void font_picker_destroy(GtkWidget *window, FontPicker *picker) { (void)window; g_free(picker); }

static void font_button_clicked(GtkButton *button, App *app) {
    if (!app->selected_widget) return;
    FontPicker *picker = g_new0(FontPicker, 1); picker->app = app; picker->target = app->selected_widget; picker->button = GTK_WIDGET(button); picker->window = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(picker->window), "Choose widget font"); gtk_window_set_transient_for(GTK_WINDOW(picker->window), GTK_WINDOW(app->window)); gtk_window_set_default_size(GTK_WINDOW(picker->window), 440, 560);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); gtk_container_set_border_width(GTK_CONTAINER(outer), 12); gtk_container_add(GTK_CONTAINER(picker->window), outer); picker->search = gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(picker->search), "Search installed fonts"); gtk_box_pack_start(GTK_BOX(outer), picker->search, FALSE, FALSE, 0); GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC); picker->list = gtk_list_box_new(); gtk_list_box_set_filter_func(GTK_LIST_BOX(picker->list), font_row_filter, picker, NULL); gtk_container_add(GTK_CONTAINER(scroll), picker->list); gtk_box_pack_start(GTK_BOX(outer), scroll, TRUE, TRUE, 0);
    PangoFontMap *map = pango_cairo_font_map_get_default(); int count = 0; PangoFontFamily **families = NULL; pango_font_map_list_families(map, &families, &count); for (int i = 0; i < count; i++) { const char *name = pango_font_family_get_name(families[i]); GtkWidget *row_label = gtk_label_new(name); PangoAttrList *attributes = pango_attr_list_new(); pango_attr_list_insert(attributes, pango_attr_family_new(name)); pango_attr_list_insert(attributes, pango_attr_size_new(15 * PANGO_SCALE)); gtk_label_set_attributes(GTK_LABEL(row_label), attributes); pango_attr_list_unref(attributes); gtk_widget_set_halign(row_label, GTK_ALIGN_START); gtk_widget_set_margin_top(row_label, 6); gtk_widget_set_margin_bottom(row_label, 6); gtk_widget_set_margin_start(row_label, 8); gtk_widget_set_margin_end(row_label, 8); gtk_list_box_insert(GTK_LIST_BOX(picker->list), row_label, -1); } g_free(families);
    g_signal_connect(picker->search, "changed", G_CALLBACK(font_search_changed), picker); g_signal_connect(picker->list, "row-activated", G_CALLBACK(font_row_activated), picker); g_signal_connect(picker->window, "destroy", G_CALLBACK(font_picker_destroy), picker); gtk_widget_show_all(picker->window); gtk_widget_grab_focus(picker->search);
}

typedef struct { App *app; GtkWidget *window, *sv_area, *hue_area, *alpha_area, *gradient_box, *gradient_sv_area, *gradient_hue_area, *gradient_alpha_area, *angle, *gradient_toggle, *target; char key[32]; float color[4], gradient_color[4]; gboolean dragging; } ColorPicker;

static void widget_color_button_clicked(GtkButton *button, App *app);

static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b) {
    float i = floorf(h * 6.0f), f = h * 6.0f - i, p = v * (1.0f - s), q = v * (1.0f - f * s), t = v * (1.0f - (1.0f - f) * s); switch ((int)i % 6) { case 0: *r = v; *g = t; *b = p; break; case 1: *r = q; *g = v; *b = p; break; case 2: *r = p; *g = v; *b = t; break; case 3: *r = p; *g = q; *b = v; break; case 4: *r = t; *g = p; *b = v; break; default: *r = v; *g = p; *b = q; break; }
}

static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v) {
    float max = MAX(r, MAX(g, b)), min = MIN(r, MIN(g, b)), d = max - min; *v = max; *s = max == 0 ? 0 : d / max; if (d == 0) *h = 0; else if (max == r) *h = fmodf((g - b) / d, 6.0f) / 6.0f; else if (max == g) *h = ((b - r) / d + 2.0f) / 6.0f; else *h = ((r - g) / d + 4.0f) / 6.0f; if (*h < 0) *h += 1.0f;
}

static void picker_apply(ColorPicker *picker) {
    if (!picker->target || !picker->app->selected_widget) return;
    if (!strcmp(picker->key, "widget-color")) { set_widget_rgba(picker->target, "widget-color", picker->color[0], picker->color[1], picker->color[2], picker->color[3]); set_widget_rgba(picker->target, "widget-gradient-color", picker->gradient_color[0], picker->gradient_color[1], picker->gradient_color[2], picker->gradient_color[3]); }
    else set_widget_rgba(picker->target, picker->key, picker->color[0], picker->color[1], picker->color[2], picker->color[3]);
    update_widget_labels(picker->app); refresh_widget_color_previews(picker->app); save_wallpaper_widgets(picker->app);
}

static float *picker_area_color(ColorPicker *picker, GtkWidget *area) {
    return area == picker->gradient_sv_area || area == picker->gradient_hue_area || area == picker->gradient_alpha_area ? picker->gradient_color : picker->color;
}

static gboolean picker_draw(GtkWidget *area, cairo_t *cr, ColorPicker *picker) {
    int width = gtk_widget_get_allocated_width(area), height = gtk_widget_get_allocated_height(area); if (width < 1 || height < 1) return FALSE; float *color = picker_area_color(picker, area);
    if (area == picker->sv_area || area == picker->gradient_sv_area) { float h, s, v, r, g, b; rgb_to_hsv(color[0], color[1], color[2], &h, &s, &v); hsv_to_rgb(h, 1, 1, &r, &g, &b); cairo_set_source_rgb(cr, r, g, b); cairo_paint(cr); cairo_pattern_t *white = cairo_pattern_create_linear(0, 0, width, 0); cairo_pattern_add_color_stop_rgba(white, 0, 1, 1, 1, 1); cairo_pattern_add_color_stop_rgba(white, 1, 1, 1, 1, 0); cairo_set_source(cr, white); cairo_paint(cr); cairo_pattern_destroy(white); cairo_pattern_t *black = cairo_pattern_create_linear(0, 0, 0, height); cairo_pattern_add_color_stop_rgba(black, 0, 0, 0, 0, 0); cairo_pattern_add_color_stop_rgba(black, 1, 0, 0, 0, 1); cairo_set_source(cr, black); cairo_paint(cr); cairo_pattern_destroy(black); cairo_set_source_rgb(cr, 1, 1, 1); cairo_arc(cr, s * width, (1 - v) * height, 5, 0, 2 * G_PI); cairo_stroke(cr); }
    else if (area == picker->hue_area || area == picker->gradient_hue_area) { cairo_pattern_t *hue = cairo_pattern_create_linear(0, 0, width, 0); for (int i = 0; i <= 6; i++) { float r, g, b; hsv_to_rgb((float)i / 6.0f, 1, 1, &r, &g, &b); cairo_pattern_add_color_stop_rgb(hue, (double)i / 6.0, r, g, b); } cairo_set_source(cr, hue); cairo_paint(cr); cairo_pattern_destroy(hue); float h, s, v; rgb_to_hsv(color[0], color[1], color[2], &h, &s, &v); cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, h * width - 2, 0, 4, height); cairo_stroke(cr); }
    else { cairo_pattern_t *alpha = cairo_pattern_create_linear(0, 0, width, 0); cairo_pattern_add_color_stop_rgba(alpha, 0, color[0], color[1], color[2], 0); cairo_pattern_add_color_stop_rgba(alpha, 1, color[0], color[1], color[2], 1); cairo_set_source(cr, alpha); cairo_paint(cr); cairo_pattern_destroy(alpha); cairo_set_source_rgb(cr, 1, 1, 1); cairo_rectangle(cr, color[3] * width - 2, 0, 4, height); cairo_stroke(cr); }
    return FALSE;
}

static gboolean picker_event(GtkWidget *area, GdkEvent *event, ColorPicker *picker) {
    if (event->type == GDK_MOTION_NOTIFY && !picker->dragging) return FALSE;
    if (event->type == GDK_BUTTON_PRESS) picker->dragging = TRUE;
    if (event->type == GDK_BUTTON_RELEASE) picker->dragging = FALSE;
    double event_x = event->type == GDK_BUTTON_PRESS ? event->button.x : event->motion.x, event_y = event->type == GDK_BUTTON_PRESS ? event->button.y : event->motion.y; double x = CLAMP(event_x / MAX(1, gtk_widget_get_allocated_width(area)), 0.0, 1.0), y = CLAMP(event_y / MAX(1, gtk_widget_get_allocated_height(area)), 0.0, 1.0); float h, s, v;
    float *color = picker_area_color(picker, area); if (area == picker->sv_area || area == picker->gradient_sv_area) { rgb_to_hsv(color[0], color[1], color[2], &h, &s, &v); hsv_to_rgb(h, x, 1.0 - y, &color[0], &color[1], &color[2]); } else if (area == picker->hue_area || area == picker->gradient_hue_area) { rgb_to_hsv(color[0], color[1], color[2], &h, &s, &v); hsv_to_rgb(x, s, v, &color[0], &color[1], &color[2]); } else color[3] = x;
    picker_apply(picker); gtk_widget_queue_draw(picker->sv_area); gtk_widget_queue_draw(picker->hue_area); gtk_widget_queue_draw(picker->alpha_area); if (picker->gradient_sv_area) { gtk_widget_queue_draw(picker->gradient_sv_area); gtk_widget_queue_draw(picker->gradient_hue_area); gtk_widget_queue_draw(picker->gradient_alpha_area); } return TRUE;
}

static void picker_angle_changed(GtkSpinButton *spin, ColorPicker *picker) {
    int angle = CLAMP(gtk_spin_button_get_value_as_int(spin), 0, 360); angle = (angle / 15) * 15;
    if (angle > 360) angle = 360;
    if (angle != gtk_spin_button_get_value_as_int(spin)) gtk_spin_button_set_value(spin, angle);
    g_object_set_data(G_OBJECT(picker->target), "widget-gradient-angle", GINT_TO_POINTER(angle % 360)); refresh_widget_color_previews(picker->app); save_wallpaper_widgets(picker->app);
}
static void picker_gradient_toggled(GtkToggleButton *toggle, ColorPicker *picker) { gboolean active = gtk_toggle_button_get_active(toggle); g_object_set_data(G_OBJECT(picker->target), "widget-gradient", GINT_TO_POINTER(active)); if (picker->gradient_box) gtk_widget_set_visible(picker->gradient_box, active); update_widget_labels(picker->app); refresh_widget_color_previews(picker->app); save_wallpaper_widgets(picker->app); }
static void picker_destroy(GtkWidget *window, ColorPicker *picker) { (void)window; g_free(picker); }

static void widget_color_button_clicked(GtkButton *button, App *app) {
    if (!app->selected_widget) return;
    ColorPicker *picker = g_new0(ColorPicker, 1); picker->app = app; picker->target = app->selected_widget; g_strlcpy(picker->key, g_object_get_data(G_OBJECT(button), "widget-color-key"), sizeof(picker->key)); float *color = widget_rgba(picker->target, picker->key); memcpy(picker->color, color, sizeof(picker->color)); if (!strcmp(picker->key, "widget-color")) memcpy(picker->gradient_color, widget_rgba(picker->target, "widget-gradient-color"), sizeof(picker->gradient_color)); picker->window = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(picker->window), !strcmp(picker->key, "widget-glow-color") ? "Glow color" : "Widget text color"); gtk_window_set_transient_for(GTK_WINDOW(picker->window), GTK_WINDOW(app->window)); gtk_window_set_default_size(GTK_WINDOW(picker->window), 360, 560);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); gtk_container_set_border_width(GTK_CONTAINER(outer), 12); gtk_container_add(GTK_CONTAINER(picker->window), outer); picker->sv_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->sv_area, -1, 230); gtk_widget_add_events(picker->sv_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(outer), picker->sv_area, TRUE, TRUE, 0); picker->hue_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->hue_area, -1, 24); gtk_widget_add_events(picker->hue_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(outer), picker->hue_area, FALSE, FALSE, 0); picker->alpha_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->alpha_area, -1, 20); gtk_widget_add_events(picker->alpha_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(outer), picker->alpha_area, FALSE, FALSE, 0);
    if (!strcmp(picker->key, "widget-color")) { picker->gradient_toggle = gtk_check_button_new_with_label("Use gradient"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(picker->gradient_toggle), widget_int(picker->target, "widget-gradient", FALSE)); gtk_box_pack_start(GTK_BOX(outer), picker->gradient_toggle, FALSE, FALSE, 0); g_signal_connect(picker->gradient_toggle, "toggled", G_CALLBACK(picker_gradient_toggled), picker); picker->gradient_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); gtk_box_pack_start(GTK_BOX(picker->gradient_box), gtk_label_new("Gradient color"), FALSE, FALSE, 0); picker->gradient_sv_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->gradient_sv_area, -1, 150); gtk_widget_add_events(picker->gradient_sv_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->gradient_sv_area, FALSE, FALSE, 0); picker->gradient_hue_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->gradient_hue_area, -1, 18); gtk_widget_add_events(picker->gradient_hue_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->gradient_hue_area, FALSE, FALSE, 0); picker->gradient_alpha_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->gradient_alpha_area, -1, 16); gtk_widget_add_events(picker->gradient_alpha_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->gradient_alpha_area, FALSE, FALSE, 0); GtkWidget *angle_label = gtk_label_new("Gradient angle (degrees)"); gtk_widget_set_halign(angle_label, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(picker->gradient_box), angle_label, FALSE, FALSE, 0); picker->angle = gtk_spin_button_new_with_range(0, 360, 15); gtk_spin_button_set_digits(GTK_SPIN_BUTTON(picker->angle), 0); gtk_spin_button_set_value(GTK_SPIN_BUTTON(picker->angle), widget_int(picker->target, "widget-gradient-angle", 0)); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->angle, FALSE, FALSE, 0); g_signal_connect(picker->angle, "value-changed", G_CALLBACK(picker_angle_changed), picker); gtk_box_pack_start(GTK_BOX(outer), picker->gradient_box, FALSE, FALSE, 0); }
    g_signal_connect(picker->sv_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->hue_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->alpha_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->sv_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->hue_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->alpha_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->sv_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->hue_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->alpha_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->sv_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->hue_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->alpha_area, "button-release-event", G_CALLBACK(picker_event), picker); if (picker->gradient_box) { g_signal_connect(picker->gradient_sv_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->gradient_hue_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->gradient_alpha_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->gradient_sv_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_hue_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_alpha_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_sv_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_hue_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_alpha_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_sv_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_hue_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_alpha_area, "button-release-event", G_CALLBACK(picker_event), picker); }
    g_signal_connect(picker->window, "destroy", G_CALLBACK(picker_destroy), picker); gtk_widget_show_all(picker->window); if (picker->gradient_box) gtk_widget_set_visible(picker->gradient_box, widget_int(picker->target, "widget-gradient", FALSE));
}

static void update_widget_inspector(App *app) {
    GtkWidget *widget = app->selected_widget;
    app->widget_settings_updating = TRUE;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->widget_settings_format));
    if (!widget) {
        gtk_label_set_text(GTK_LABEL(app->widget_settings_name), "Select a widget");
        gtk_widget_set_sensitive(app->widget_settings_panel, FALSE);
        app->widget_settings_updating = FALSE;
        return;
    }
    const char *kind = g_object_get_data(G_OBJECT(widget), "widget-kind");
    const char *format = g_object_get_data(G_OBJECT(widget), "widget-format");
    gtk_label_set_text(GTK_LABEL(app->widget_settings_name), !g_strcmp0(kind, "time") ? "Time widget" : "Date widget");
    if (!g_strcmp0(kind, "time")) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->widget_settings_format), "24", "24-hour time");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->widget_settings_format), "12", "12-hour time");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->widget_settings_format), !g_strcmp0(format, "12") ? "12" : "24");
    } else {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->widget_settings_format), "long", "Long date");
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->widget_settings_format), "short", "Short date");
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->widget_settings_format), !g_strcmp0(format, "short") ? "short" : "long");
    }
    gtk_widget_set_sensitive(app->widget_settings_format, TRUE);
    const char *font = g_object_get_data(G_OBJECT(widget), "widget-font");
    gtk_button_set_label(GTK_BUTTON(app->widget_settings_font), font ? font : "Sans");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_size), widget_int(widget, "widget-size", 32)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_width), widget_int(widget, "widget-width", 0)); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->widget_settings_bold), widget_int(widget, "widget-bold", TRUE)); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->widget_settings_italic), widget_int(widget, "widget-italic", FALSE)); gtk_range_set_value(GTK_RANGE(app->widget_settings_spacing), widget_int(widget, "widget-spacing", 0)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_stretch_x), widget_int(widget, "widget-stretch-x", 100)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_stretch_y), widget_int(widget, "widget-stretch-y", 100));
    refresh_widget_color_previews(app);
    gtk_widget_set_sensitive(app->widget_settings_panel, TRUE);
    app->widget_settings_updating = FALSE;
}

static void widget_format_changed(GtkComboBox *combo, App *app) {
    if (app->widget_settings_updating || !app->selected_widget) return;
    const char *format = gtk_combo_box_get_active_id(combo);
    if (!format) return;
    g_object_set_data_full(G_OBJECT(app->selected_widget), "widget-format", g_strdup(format), g_free);
    update_widget_labels(app);
    save_wallpaper_widgets(app);
}

static void widget_style_changed(GtkWidget *control, App *app) {
    if (app->widget_settings_updating || !app->selected_widget) return;
    GtkWidget *widget = app->selected_widget;
    if (GTK_IS_RANGE(control)) {
        const char *key = control == app->widget_settings_size ? "widget-size" : control == app->widget_settings_width ? "widget-width" : control == app->widget_settings_spacing ? "widget-spacing" : control == app->widget_settings_stretch_x ? "widget-stretch-x" : "widget-stretch-y";
        int maximum = control == app->widget_settings_size ? 160 : control == app->widget_settings_width ? 1600 : control == app->widget_settings_spacing ? 40 : 200; int minimum = control == app->widget_settings_stretch_x || control == app->widget_settings_stretch_y ? 50 : 0; g_object_set_data(G_OBJECT(widget), key, GINT_TO_POINTER(CLAMP((int)round(gtk_range_get_value(GTK_RANGE(control))), minimum, maximum)));
    } else if (control == app->widget_settings_bold) g_object_set_data(G_OBJECT(widget), "widget-bold", GINT_TO_POINTER(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control))));
    else if (control == app->widget_settings_italic) g_object_set_data(G_OBJECT(widget), "widget-italic", GINT_TO_POINTER(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control))));
    update_widget_labels(app); save_wallpaper_widgets(app);
}

static void delete_selected_widget(GtkButton *button, App *app) {
    (void)button;
    if (!app->selected_widget) return;
    GtkWidget *widget = app->selected_widget;
    app->selected_widget = NULL;
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget), "wallpaper-widget-selected");
    g_ptr_array_remove(app->wallpaper_widgets, widget);
    gtk_widget_destroy(widget);
    save_wallpaper_widgets(app);
    update_widget_inspector(app);
}

static gboolean wallpaper_widget_press(GtkWidget *widget, GdkEventButton *event, App *app) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    if (app->selected_widget != widget) {
        if (app->selected_widget) gtk_style_context_remove_class(gtk_widget_get_style_context(app->selected_widget), "wallpaper-widget-selected");
        app->selected_widget = widget;
        gtk_style_context_add_class(gtk_widget_get_style_context(widget), "wallpaper-widget-selected");
        update_widget_inspector(app);
    }
    g_object_set_data(G_OBJECT(widget), "dragging", GINT_TO_POINTER(TRUE));
    g_object_set_data(G_OBJECT(widget), "drag-x", GINT_TO_POINTER((int)event->x_root));
    g_object_set_data(G_OBJECT(widget), "drag-y", GINT_TO_POINTER((int)event->y_root));
    g_object_set_data(G_OBJECT(widget), "drag-left", GINT_TO_POINTER(gtk_widget_get_margin_start(widget)));
    g_object_set_data(G_OBJECT(widget), "drag-top", GINT_TO_POINTER(gtk_widget_get_margin_top(widget)));
    gtk_widget_grab_focus(widget); gtk_widget_show(app->widget_grid); gtk_widget_queue_draw(app->widget_grid); return TRUE;
}

static gboolean wallpaper_widget_motion(GtkWidget *widget, GdkEventMotion *event, App *app) {
    if (!GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "dragging"))) return FALSE;
    int left = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "drag-left")) + (int)event->x_root - GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "drag-x"));
    int top = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "drag-top")) + (int)event->y_root - GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "drag-y"));
    int max_left = MAX(0, gtk_widget_get_allocated_width(app->widget_canvas) - gtk_widget_get_allocated_width(widget));
    int max_top = MAX(0, gtk_widget_get_allocated_height(app->widget_canvas) - gtk_widget_get_allocated_height(widget));
    left = CLAMP(left, 0, max_left); top = CLAMP(top, 0, max_top); snap_widget_position(app, widget, left, top); save_wallpaper_widgets(app); return TRUE;
}

static gboolean wallpaper_widget_release(GtkWidget *widget, GdkEventButton *event, App *app) {
    (void)event; g_object_set_data(G_OBJECT(widget), "dragging", GINT_TO_POINTER(FALSE)); gtk_widget_hide(app->widget_grid); return TRUE;
}

static GtkWidget *create_wallpaper_widget(App *app, const char *kind, const char *format, double x, double y) {
    GtkWidget *event_box = gtk_event_box_new(); GtkWidget *label = gtk_drawing_area_new();
    gtk_widget_set_can_focus(event_box, TRUE); gtk_widget_add_events(event_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    gtk_style_context_add_class(gtk_widget_get_style_context(event_box), "wallpaper-widget"); gtk_container_add(GTK_CONTAINER(event_box), label); set_widget_style_defaults(event_box, kind);
    g_object_set_data_full(G_OBJECT(event_box), "widget-kind", g_strdup(kind), g_free); g_object_set_data_full(G_OBJECT(event_box), "widget-format", g_strdup(format ? format : (!g_strcmp0(kind, "time") ? "24" : "long")), g_free); g_object_set_data(G_OBJECT(event_box), "widget-label", label);
    g_object_set_data(G_OBJECT(label), "widget-owner", event_box); g_signal_connect(label, "draw", G_CALLBACK(draw_wallpaper_widget), event_box);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(wallpaper_widget_press), app); g_signal_connect(event_box, "motion-notify-event", G_CALLBACK(wallpaper_widget_motion), app); g_signal_connect(event_box, "button-release-event", G_CALLBACK(wallpaper_widget_release), app); g_signal_connect(event_box, "map", G_CALLBACK(wallpaper_widget_map), app);
    gtk_widget_set_halign(event_box, GTK_ALIGN_START); gtk_widget_set_valign(event_box, GTK_ALIGN_START); gtk_widget_set_tooltip_text(event_box, "Drag to move"); set_widget_normalized_position(event_box, app, x, y);
    gtk_overlay_add_overlay(GTK_OVERLAY(app->widget_canvas), event_box); g_ptr_array_add(app->wallpaper_widgets, event_box); update_widget_labels(app); gtk_widget_show_all(event_box); return event_box;
}

static void add_wallpaper_widget(GtkMenuItem *item, App *app) {
    const char *kind = g_object_get_data(G_OBJECT(item), "widget-kind"); guint index = app->wallpaper_widgets->len;
    create_wallpaper_widget(app, kind, NULL, 0.04 + (index % 3) * 0.18, 0.05 + (index / 3) * 0.10); save_wallpaper_widgets(app);
}

static void load_saved_wallpaper_widgets(App *app) {
    char *path = data_path("widgets.conf"); char *contents = read_file(path); g_free(path); if (!contents) return;
    char **lines = g_strsplit(contents, "\n", -1);
    for (guint i = 0; lines[i]; i++) { char kind[16], format[16] = ""; double x, y; int fields = sscanf(lines[i], "%15s %lf %lf %15s", kind, &x, &y, format); if (fields >= 3 && (!strcmp(kind, "time") || !strcmp(kind, "date"))) { GtkWidget *widget = create_wallpaper_widget(app, kind, fields == 4 ? format : NULL, x, y); apply_widget_style_line(widget, lines[i]); } }
    update_widget_labels(app); g_strfreev(lines); g_free(contents);
}

static void widget_page_destroy(GtkWidget *page, App *app) {
    (void)page;
    if (app->widget_clock_id) { g_source_remove(app->widget_clock_id); app->widget_clock_id = 0; }
}

void add_css(void) {
    GtkCssProvider *widget_css = gtk_css_provider_new(); gtk_css_provider_load_from_data(widget_css, ".wallpaper-widget { border: 0; background: transparent; } .wallpaper-widget-selected { border: 0; box-shadow: 0 0 0 1px #ffd84a; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(widget_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1); g_object_unref(widget_css);
    GtkCssProvider *css = gtk_css_provider_new(); gtk_css_provider_load_from_data(css,
        "* { border-radius: 0; } window, notebook, notebook stack { background: #020304; color: #dce5e8; } notebook header { background: #010203; border-bottom: 1px solid #182126; } notebook tab { padding: 11px 22px; color: #59666d; border: 0; } notebook tab:checked { color: #a9e0ef; border-bottom: 2px solid #a9e0ef; } .brand { font-size: 21px; font-weight: 600; color: #e5f0f2; } .muted, .row-subtitle { color: #596970; } .section { margin-top: 12px; margin-bottom: 5px; font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #8fcbdc; } entry, combobox, button { min-height: 32px; border: 1px solid #202b30; background: #111518; color: #dce5e8; box-shadow: none; } entry:focus, textview:focus { border: 1px solid #a9e0ef; } button:hover { background: #1a2226; border-color: #7899a3; } button.suggested-action { background: #a9e0ef; color: #081013; border-color: #a9e0ef; } button.suggested-action:hover { background: #c6edf5; } .preview, .thumb { border: 1px solid #202b30; } .wallpaper-widget { padding: 0; border: 1px solid #a9e0ef; background: transparent; color: #e5f0f2; } .wallpaper-widget-selected { border-color: #ffd84a; box-shadow: 0 0 0 1px #ffd84a; } .row-title { font-weight: 600; color: #dce8eb; } flowbox { padding: 2px; } flowboxchild { padding: 4px; border: 1px solid #1b2428; background: #0d1113; } flowboxchild:hover { border-color: #334950; } flowboxchild:selected { border-color: #a9e0ef; background: #111b1f; } textview { background: #050607; color: #d1e1e6; border: 1px solid #202b30; } scrolledwindow { border: 1px solid #172024; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); GtkCssProvider *compact_css = gtk_css_provider_new(); gtk_css_provider_load_from_data(compact_css, ".selected-control { min-height: 0; padding: 0 4px; font-size: 9px; } .selected-scroll { border: 1px solid #172024; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(compact_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); g_object_unref(compact_css); g_object_unref(css);
}

GtkWidget *build_ui(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); app->notebook = gtk_notebook_new(); gtk_notebook_set_tab_pos(GTK_NOTEBOOK(app->notebook), GTK_POS_TOP); gtk_box_pack_start(GTK_BOX(root), app->notebook, TRUE, TRUE, 0);
    GtkWidget *local_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(local_page), 24);
    GtkWidget *brand = brand_header("My wallpapers"); gtk_box_pack_start(GTK_BOX(local_page), brand, FALSE, FALSE, 0);
    GtkWidget *hint = gtk_label_new("Choose a shader from the grid. Your selected shader previews below."); gtk_widget_set_halign(hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(hint), "muted"); gtk_box_pack_start(GTK_BOX(local_page), hint, FALSE, FALSE, 3);
    GtkWidget *local_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(local_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC); gtk_widget_set_vexpand(local_scroll, TRUE); app->local_list = gtk_flow_box_new(); gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(app->local_list), GTK_SELECTION_BROWSE); gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(app->local_list), TRUE); gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(app->local_list), FALSE); gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(app->local_list), 8); gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(app->local_list), 8); gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(app->local_list), 5); gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(app->local_list), 5); gtk_container_add(GTK_CONTAINER(local_scroll), app->local_list); gtk_box_pack_start(GTK_BOX(local_page), local_scroll, TRUE, TRUE, 12); g_signal_connect(app->local_list, "child-activated", G_CALLBACK(local_row_activated), app);
    GtkWidget *preview_label = gtk_label_new("SELECTED"); gtk_widget_set_halign(preview_label, GTK_ALIGN_START); gtk_widget_set_margin_top(preview_label, 0); gtk_widget_set_margin_bottom(preview_label, 0); gtk_style_context_add_class(gtk_widget_get_style_context(preview_label), "section"); gtk_box_pack_start(GTK_BOX(local_page), preview_label, FALSE, FALSE, 0);
    GtkWidget *selected_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); gtk_widget_set_size_request(selected_strip, -1, 160); gtk_widget_set_vexpand(selected_strip, FALSE); app->preview = gtk_gl_area_new(); gtk_widget_set_size_request(app->preview, 260, 160); gtk_style_context_add_class(gtk_widget_get_style_context(app->preview), "preview"); gtk_box_pack_start(GTK_BOX(selected_strip), app->preview, FALSE, FALSE, 0); g_signal_connect(app->preview, "realize", G_CALLBACK(preview_realize), app); g_signal_connect(app->preview, "render", G_CALLBACK(preview_render), app);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_set_size_request(controls, 160, 160); GtkWidget *apply = gtk_button_new_with_label("Set as current wallpaper"); gtk_style_context_add_class(gtk_widget_get_style_context(apply), "suggested-action"); gtk_style_context_add_class(gtk_widget_get_style_context(apply), "selected-control"); gtk_widget_set_hexpand(apply, TRUE); gtk_widget_set_vexpand(apply, TRUE); gtk_box_pack_start(GTK_BOX(controls), apply, TRUE, TRUE, 0); g_signal_connect(apply, "clicked", G_CALLBACK(apply_clicked), app); GtkWidget *stop = gtk_button_new_with_label("Stop wallpaper"); gtk_style_context_add_class(gtk_widget_get_style_context(stop), "selected-control"); gtk_widget_set_hexpand(stop, TRUE); gtk_widget_set_vexpand(stop, TRUE); gtk_box_pack_start(GTK_BOX(controls), stop, TRUE, TRUE, 0); g_signal_connect(stop, "clicked", G_CALLBACK(stop_clicked), app); GtkWidget *delete_button = gtk_button_new_with_label("Delete selected"); gtk_style_context_add_class(gtk_widget_get_style_context(delete_button), "selected-control"); gtk_widget_set_hexpand(delete_button, TRUE); gtk_widget_set_vexpand(delete_button, TRUE); gtk_box_pack_start(GTK_BOX(controls), delete_button, TRUE, TRUE, 0); g_signal_connect(delete_button, "clicked", G_CALLBACK(delete_selected_clicked), app); GtkWidget *open = gtk_button_new_with_label("Open on Shadertoy"); gtk_style_context_add_class(gtk_widget_get_style_context(open), "selected-control"); gtk_widget_set_hexpand(open, TRUE); gtk_widget_set_vexpand(open, TRUE); gtk_box_pack_start(GTK_BOX(controls), open, TRUE, TRUE, 0); g_signal_connect(open, "clicked", G_CALLBACK(open_clicked), app); gtk_box_pack_start(GTK_BOX(selected_strip), controls, FALSE, FALSE, 0);
    GtkWidget *constants_panel = gtk_frame_new("Shader constants"); gtk_widget_set_size_request(constants_panel, 300, 160); gtk_widget_set_hexpand(constants_panel, TRUE); GtkWidget *constants_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(constants_outer), 0); gtk_container_add(GTK_CONTAINER(constants_panel), constants_outer); GtkWidget *constants_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_style_context_add_class(gtk_widget_get_style_context(constants_scroll), "selected-scroll"); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(constants_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC); gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(constants_scroll), 0); gtk_widget_set_vexpand(constants_scroll, TRUE); app->constants_box = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(app->constants_box), 0); gtk_grid_set_column_spacing(GTK_GRID(app->constants_box), 0); gtk_container_add(GTK_CONTAINER(constants_scroll), app->constants_box); gtk_box_pack_start(GTK_BOX(constants_outer), constants_scroll, TRUE, TRUE, 0); GtkWidget *constants_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); GtkWidget *constants_apply = gtk_button_new_with_label("Apply"); gtk_box_pack_end(GTK_BOX(constants_buttons), constants_apply, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(constants_outer), constants_buttons, FALSE, FALSE, 0); g_signal_connect(constants_apply, "clicked", G_CALLBACK(apply_constants_clicked), app); gtk_box_pack_start(GTK_BOX(selected_strip), constants_panel, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(local_page), selected_strip, FALSE, FALSE, 0);
    app->status = gtk_label_new("Choose a shader to begin"); gtk_widget_set_halign(app->status, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(app->status), "muted"); gtk_box_pack_start(GTK_BOX(local_page), app->status, FALSE, FALSE, 0); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), local_page, gtk_label_new("Wallpapers"));

    app->wallpaper_widgets = g_ptr_array_new(); GdkDisplay *display = gdk_display_get_default(); app->widget_screen_width = 0; app->widget_screen_height = 0;
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_DISPLAY(display)) { Display *xdisplay = gdk_x11_display_get_xdisplay(display); if (xdisplay) { int xscreen = DefaultScreen(xdisplay); app->widget_screen_width = DisplayWidth(xdisplay, xscreen); app->widget_screen_height = DisplayHeight(xdisplay, xscreen); } }
#endif
    if (app->widget_screen_width < 320 || app->widget_screen_height < 180) { GdkMonitor *monitor = gdk_display_get_primary_monitor(display); GdkRectangle geometry = {0}; if (monitor) gdk_monitor_get_geometry(monitor, &geometry); app->widget_screen_width = geometry.width; app->widget_screen_height = geometry.height; }
    if (app->widget_screen_width < 320 || app->widget_screen_height < 180) { app->widget_screen_width = 1920; app->widget_screen_height = 1080; }
    double widget_ratio = (double)app->widget_screen_width / (double)app->widget_screen_height;
    GtkWidget *widgets_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_widget_set_hexpand(widgets_page, TRUE); gtk_widget_set_vexpand(widgets_page, TRUE); GtkWidget *widgets_frame = gtk_aspect_frame_new(NULL, 0.5, 0.5, widget_ratio, FALSE); gtk_frame_set_shadow_type(GTK_FRAME(widgets_frame), GTK_SHADOW_NONE); gtk_widget_set_size_request(widgets_frame, 640, MAX(360, (int)(640.0 / widget_ratio))); gtk_widget_set_halign(widgets_frame, GTK_ALIGN_FILL); gtk_widget_set_valign(widgets_frame, GTK_ALIGN_FILL); gtk_widget_set_hexpand(widgets_frame, TRUE); gtk_widget_set_vexpand(widgets_frame, TRUE); app->widget_canvas = gtk_overlay_new(); gtk_widget_set_size_request(app->widget_canvas, 640, MAX(360, (int)(640.0 / widget_ratio))); gtk_widget_set_hexpand(app->widget_canvas, TRUE); gtk_widget_set_vexpand(app->widget_canvas, TRUE); g_signal_connect(app->widget_canvas, "size-allocate", G_CALLBACK(position_wallpaper_widgets), app); gtk_container_add(GTK_CONTAINER(widgets_frame), app->widget_canvas); gtk_box_pack_start(GTK_BOX(widgets_page), widgets_frame, TRUE, TRUE, 0);
    app->widget_preview = gtk_gl_area_new(); gtk_widget_set_size_request(app->widget_preview, 640, MAX(360, (int)(640.0 / widget_ratio))); gtk_widget_set_hexpand(app->widget_preview, TRUE); gtk_widget_set_vexpand(app->widget_preview, TRUE); gtk_container_add(GTK_CONTAINER(app->widget_canvas), app->widget_preview); g_signal_connect(app->widget_preview, "realize", G_CALLBACK(widget_preview_realize), app); g_signal_connect(app->widget_preview, "unrealize", G_CALLBACK(widget_preview_unrealize), app); g_signal_connect(app->widget_preview, "render", G_CALLBACK(widget_preview_render), app);
    app->widget_grid = gtk_drawing_area_new(); gtk_widget_set_halign(app->widget_grid, GTK_ALIGN_FILL); gtk_widget_set_valign(app->widget_grid, GTK_ALIGN_FILL); gtk_widget_set_hexpand(app->widget_grid, TRUE); gtk_widget_set_vexpand(app->widget_grid, TRUE); gtk_widget_set_sensitive(app->widget_grid, FALSE); gtk_widget_set_no_show_all(app->widget_grid, TRUE); g_signal_connect(app->widget_grid, "draw", G_CALLBACK(draw_widget_grid), app); gtk_overlay_add_overlay(GTK_OVERLAY(app->widget_canvas), app->widget_grid);
    GtkWidget *widget_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); GtkWidget *add_widget = gtk_menu_button_new(); gtk_button_set_label(GTK_BUTTON(add_widget), "+"); gtk_widget_set_tooltip_text(add_widget, "Add wallpaper widget"); gtk_box_pack_start(GTK_BOX(widget_toolbar), add_widget, FALSE, FALSE, 0); gtk_widget_set_halign(widget_toolbar, GTK_ALIGN_END); gtk_widget_set_valign(widget_toolbar, GTK_ALIGN_START); gtk_widget_set_margin_top(widget_toolbar, 14); gtk_widget_set_margin_end(widget_toolbar, 14); gtk_overlay_add_overlay(GTK_OVERLAY(app->widget_canvas), widget_toolbar);
    GtkWidget *widget_menu = gtk_menu_new(); GtkWidget *time_item = gtk_menu_item_new_with_label("Time"); GtkWidget *date_item = gtk_menu_item_new_with_label("Date"); gtk_widget_set_size_request(time_item, 120, -1); gtk_widget_set_size_request(date_item, 120, -1); g_object_set_data(G_OBJECT(time_item), "widget-kind", (gpointer)"time"); g_object_set_data(G_OBJECT(date_item), "widget-kind", (gpointer)"date"); gtk_menu_shell_append(GTK_MENU_SHELL(widget_menu), time_item); gtk_menu_shell_append(GTK_MENU_SHELL(widget_menu), date_item); g_signal_connect(time_item, "activate", G_CALLBACK(add_wallpaper_widget), app); g_signal_connect(date_item, "activate", G_CALLBACK(add_wallpaper_widget), app); gtk_widget_show_all(widget_menu); gtk_menu_button_set_popup(GTK_MENU_BUTTON(add_widget), widget_menu);
    GtkWidget *inspector = gtk_frame_new("Selected widget"); app->widget_settings_panel = inspector; gtk_widget_set_size_request(inspector, -1, 190); gtk_widget_set_hexpand(inspector, TRUE);
    GtkWidget *inspector_grid = gtk_grid_new(); gtk_container_set_border_width(GTK_CONTAINER(inspector_grid), 10); gtk_grid_set_row_spacing(GTK_GRID(inspector_grid), 6); gtk_grid_set_column_spacing(GTK_GRID(inspector_grid), 8); gtk_container_add(GTK_CONTAINER(inspector), inspector_grid);
    app->widget_settings_name = gtk_label_new("Select a widget"); gtk_widget_set_halign(app->widget_settings_name, GTK_ALIGN_START); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Format"), 1, 0, 1, 1); app->widget_settings_format = gtk_combo_box_text_new(); gtk_widget_set_size_request(app->widget_settings_format, 150, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_format, 2, 0, 1, 1); g_signal_connect(app->widget_settings_format, "changed", G_CALLBACK(widget_format_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Font"), 3, 0, 1, 1); app->widget_settings_font = gtk_button_new_with_label("Sans"); gtk_widget_set_size_request(app->widget_settings_font, 130, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_font, 4, 0, 1, 1); g_signal_connect(app->widget_settings_font, "clicked", G_CALLBACK(font_button_clicked), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Size"), 5, 0, 1, 1); app->widget_settings_size = gtk_spin_button_new_with_range(8, 160, 1); gtk_widget_set_size_request(app->widget_settings_size, 68, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_size, 6, 0, 1, 1); g_signal_connect(app->widget_settings_size, "value-changed", G_CALLBACK(widget_style_changed), app);
    app->widget_settings_bold = gtk_check_button_new_with_label("Bold"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_bold, 7, 0, 1, 1); g_signal_connect(app->widget_settings_bold, "toggled", G_CALLBACK(widget_style_changed), app);
    app->widget_settings_italic = gtk_check_button_new_with_label("Italic"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_italic, 8, 0, 1, 1); g_signal_connect(app->widget_settings_italic, "toggled", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Text"), 0, 1, 1, 1); app->widget_settings_color = widget_color_button(app, "widget-color"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_color, 1, 1, 2, 1); g_signal_connect(app->widget_settings_color, "clicked", G_CALLBACK(widget_color_button_clicked), app);

    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Spacing"), 0, 2, 1, 1); app->widget_settings_spacing = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 40, 1); gtk_scale_set_draw_value(GTK_SCALE(app->widget_settings_spacing), TRUE); gtk_widget_set_size_request(app->widget_settings_spacing, 120, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_spacing, 1, 2, 1, 1); g_signal_connect(app->widget_settings_spacing, "value-changed", G_CALLBACK(widget_style_changed), app); gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Stretch X"), 2, 2, 1, 1); app->widget_settings_stretch_x = gtk_spin_button_new_with_range(50, 200, 1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_stretch_x, 3, 2, 1, 1); g_signal_connect(app->widget_settings_stretch_x, "value-changed", G_CALLBACK(widget_style_changed), app); gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Stretch Y"), 4, 2, 1, 1); app->widget_settings_stretch_y = gtk_spin_button_new_with_range(50, 200, 1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_stretch_y, 5, 2, 1, 1); g_signal_connect(app->widget_settings_stretch_y, "value-changed", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Width"), 0, 3, 1, 1); app->widget_settings_width = gtk_spin_button_new_with_range(0, 1600, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_width), 0); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_width, 1, 3, 1, 1); g_signal_connect(app->widget_settings_width, "value-changed", G_CALLBACK(widget_style_changed), app);
    GtkWidget *delete_widget = gtk_button_new_with_label("Delete"); gtk_widget_set_halign(delete_widget, GTK_ALIGN_END); gtk_grid_attach(GTK_GRID(inspector_grid), delete_widget, 8, 3, 1, 1); g_signal_connect(delete_widget, "clicked", G_CALLBACK(delete_selected_widget), app);
    gtk_box_pack_start(GTK_BOX(widgets_page), inspector, FALSE, FALSE, 0); update_widget_inspector(app); load_saved_wallpaper_widgets(app);
    g_signal_connect(widgets_page, "destroy", G_CALLBACK(widget_page_destroy), app); app->widget_clock_id = g_timeout_add(1000, update_widget_labels, app); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), widgets_page, gtk_label_new("Widgets"));

    GtkWidget *toy_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(toy_page), 24); GtkWidget *toy_title = brand_header("Shadertoy"); gtk_box_pack_start(GTK_BOX(toy_page), toy_title, FALSE, FALSE, 0); GtkWidget *toy_hint = gtk_label_new("Browse Shadertoy directly. Use the injected import button on a shader page."); gtk_widget_set_halign(toy_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(toy_hint), "muted"); gtk_box_pack_start(GTK_BOX(toy_page), toy_hint, FALSE, FALSE, 3);
#ifdef HAVE_WEBKIT
    GtkWidget *browser_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); GtkWidget *back = gtk_button_new_with_label("‹"); GtkWidget *forward = gtk_button_new_with_label("›"); app->browser_url = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->browser_url), "https://www.shadertoy.com"); GtkWidget *go = gtk_button_new_with_label("Go"); gtk_box_pack_start(GTK_BOX(browser_bar), back, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), forward, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), app->browser_url, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), go, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(toy_page), browser_bar, FALSE, FALSE, 8);
    WebKitUserContentManager *content_manager = webkit_user_content_manager_new(); g_signal_connect(content_manager, "script-message-received::glacier", G_CALLBACK(browser_message), app); webkit_user_content_manager_register_script_message_handler(content_manager, "glacier"); WebKitUserScript *script = webkit_user_script_new(browser_import_script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL); webkit_user_content_manager_add_script(content_manager, script); webkit_user_script_unref(script); app->browser = webkit_web_view_new_with_user_content_manager(content_manager); webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->browser), "https://www.shadertoy.com"); gtk_widget_set_vexpand(app->browser, TRUE); gtk_box_pack_start(GTK_BOX(toy_page), app->browser, TRUE, TRUE, 8); g_signal_connect(back, "clicked", G_CALLBACK(browser_back), app); g_signal_connect(forward, "clicked", G_CALLBACK(browser_forward), app); g_signal_connect(go, "clicked", G_CALLBACK(browser_load), app); g_signal_connect(app->browser_url, "activate", G_CALLBACK(browser_load), app); g_signal_connect(app->browser, "load-changed", G_CALLBACK(browser_load_changed), app);
#else
    GtkWidget *browser_missing = gtk_label_new("Embedded browser unavailable in this build. Install libwebkit2gtk-4.0-dev, rebuild, and this tab will include an Import shader button on Shadertoy pages."); gtk_label_set_line_wrap(GTK_LABEL(browser_missing), TRUE); gtk_widget_set_halign(browser_missing, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), browser_missing, FALSE, FALSE, 10); GtkWidget *external = gtk_button_new_with_label("Open Shadertoy in browser"); gtk_widget_set_halign(external, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), external, FALSE, FALSE, 5); g_signal_connect(external, "clicked", G_CALLBACK(browser_external_clicked), app);
#endif
    gtk_box_pack_start(GTK_BOX(toy_page), section_label("PASTE SHADER CODE"), FALSE, FALSE, 0); app->code_view = gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->code_view), GTK_WRAP_NONE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->code_view), TRUE); GtkWidget *code_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(code_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); gtk_widget_set_size_request(code_scroll, -1, 130); gtk_container_add(GTK_CONTAINER(code_scroll), app->code_view); gtk_box_pack_start(GTK_BOX(toy_page), code_scroll, FALSE, FALSE, 5); GtkWidget *paste_button = gtk_button_new_with_label("Preview pasted shader"); gtk_widget_set_halign(paste_button, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), paste_button, FALSE, FALSE, 0); g_signal_connect(paste_button, "clicked", G_CALLBACK(load_pasted_clicked), app); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), toy_page, gtk_label_new("Shadertoy"));

    GtkWidget *settings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(settings_page), 36); GtkWidget *settings_title = brand_header("Settings"); gtk_box_pack_start(GTK_BOX(settings_page), settings_title, FALSE, FALSE, 0); GtkWidget *settings_hint = gtk_label_new("Connection and wallpaper behavior"); gtk_widget_set_halign(settings_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(settings_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), settings_hint, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(settings_page), section_label("OPTIONAL SHADERTOY API KEY"), FALSE, FALSE, 14); app->api_key = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(app->api_key), FALSE); gtk_entry_set_placeholder_text(GTK_ENTRY(app->api_key), "Paste your Shadertoy app key"); const char *env = g_getenv("SHADERTOY_API_KEY"); if (env) gtk_entry_set_text(GTK_ENTRY(app->api_key), env); gtk_box_pack_start(GTK_BOX(settings_page), app->api_key, FALSE, FALSE, 0); GtkWidget *key_hint = gtk_label_new("The embedded browser imports shaders without this key. The key is only for the legacy REST search."); gtk_widget_set_halign(key_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(key_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), key_hint, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(settings_page), section_label("WALLPAPER FPS"), FALSE, FALSE, 18); app->fps = gtk_combo_box_text_new(); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "24"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "30"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "45"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "60"); gtk_combo_box_set_active(GTK_COMBO_BOX(app->fps), 1); gtk_widget_set_halign(app->fps, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(settings_page), app->fps, FALSE, FALSE, 0); GtkWidget *mint_hint = gtk_label_new("X11 mode · wallpaper is shared across all workspaces"); gtk_widget_set_halign(mint_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(mint_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), mint_hint, FALSE, FALSE, 22); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), settings_page, gtk_label_new("Settings"));
    gtk_box_set_spacing(GTK_BOX(selected_strip), 0); gtk_box_set_child_packing(GTK_BOX(selected_strip), app->preview, FALSE, FALSE, 0, GTK_PACK_START); gtk_box_set_child_packing(GTK_BOX(selected_strip), controls, FALSE, FALSE, 0, GTK_PACK_START); gtk_box_set_child_packing(GTK_BOX(selected_strip), constants_panel, TRUE, TRUE, 0, GTK_PACK_START); GtkWidget *section_widgets[] = { app->preview, controls, constants_panel }; for (guint i = 0; i < G_N_ELEMENTS(section_widgets); i++) { gtk_widget_set_margin_start(section_widgets[i], 0); gtk_widget_set_margin_end(section_widgets[i], 0); }
    GtkCssProvider *selected_css = gtk_css_provider_new(); gtk_css_provider_load_from_data(selected_css, ".selected-control { font-size: 13px; padding-left: 2px; padding-right: 2px; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(selected_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1); g_object_unref(selected_css);
    return root;
}
