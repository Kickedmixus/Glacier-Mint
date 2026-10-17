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

static DeskWidget *desk_of(GtkWidget *box) { return box ? g_object_get_data(G_OBJECT(box), "desk-widget") : NULL; }

static gboolean is_live_widget(App *app, GtkWidget *box) { guint index; return box && app->wallpaper_widgets && g_ptr_array_find(app->wallpaper_widgets, box, &index); }

/* The canvas shows the whole screen shrunk down, so widgets are drawn at canvas size / screen size. */
static double preview_scale(App *app, gboolean vertical) {
    int canvas = vertical ? gtk_widget_get_allocated_height(app->widget_canvas) : gtk_widget_get_allocated_width(app->widget_canvas), screen = vertical ? app->widget_screen_height : app->widget_screen_width;
    return canvas > 1 ? (double)canvas / MAX(1, screen) : 1.0;
}

static gboolean draw_wallpaper_widget(GtkWidget *area, cairo_t *cr, GtkWidget *box) {
    (void)area; App *app = g_object_get_data(G_OBJECT(box), "widget-app"); DeskWidget *widget = desk_of(box); if (!app || !widget) return FALSE;
    cairo_save(cr); cairo_scale(cr, preview_scale(app, FALSE), preview_scale(app, TRUE)); desk_widget_draw(cr, widget, &app->widget_snapshot); cairo_restore(cr); return FALSE;
}

static void refresh_widget_size(App *app, GtkWidget *box) {
    GtkWidget *area = g_object_get_data(G_OBJECT(box), "widget-label"); int width, height; desk_widget_measure(desk_of(box), &app->widget_snapshot, &width, &height);
    gtk_widget_set_size_request(area, MAX(1, (int)ceil(width * preview_scale(app, FALSE))), MAX(1, (int)ceil(height * preview_scale(app, TRUE)))); gtk_widget_queue_draw(area);
}

/* Runs every 33 ms; each widget redraws only when its own refresh interval has passed. */
static gboolean update_widget_labels(gpointer data) {
    App *app = data; gint64 now = g_get_real_time() / 1000; gboolean fresh = FALSE;
    for (guint i = 0; app->wallpaper_widgets && i < app->wallpaper_widgets->len; i++) {
        GtkWidget *box = g_ptr_array_index(app->wallpaper_widgets, i); gint64 *last = g_object_get_data(G_OBJECT(box), "widget-bucket"), bucket = now / desk_widget_refresh_ms(desk_of(box));
        if (*last == bucket) continue;
        if (!fresh) { widget_snapshot_clear(&app->widget_snapshot); widget_feed_snapshot(app->widget_feed, &app->widget_snapshot); fresh = TRUE; }
        *last = bucket; refresh_widget_size(app, box);
    }
    return G_SOURCE_CONTINUE;
}

static void invalidate_wallpaper_widgets(App *app) {
    for (guint i = 0; app->wallpaper_widgets && i < app->wallpaper_widgets->len; i++) { gint64 *bucket = g_object_get_data(G_OBJECT(g_ptr_array_index(app->wallpaper_widgets, i)), "widget-bucket"); *bucket = -1; }
}

static GPtrArray *desk_widget_list(App *app) {
    GPtrArray *list = g_ptr_array_new(); for (guint i = 0; app->wallpaper_widgets && i < app->wallpaper_widgets->len; i++) g_ptr_array_add(list, desk_of(g_ptr_array_index(app->wallpaper_widgets, i)));
    return list;
}

static void save_wallpaper_widgets(App *app) {
    if (!app->wallpaper_widgets) return;
    GPtrArray *list = desk_widget_list(app); desk_widgets_save(list); g_ptr_array_free(list, TRUE);
}

static void sync_widget_feed(App *app) { GPtrArray *list = desk_widget_list(app); widget_feed_sync(app->widget_feed, list); g_ptr_array_free(list, TRUE); }

static void wallpaper_widgets_changed(App *app) { sync_widget_feed(app); invalidate_wallpaper_widgets(app); update_widget_labels(app); save_wallpaper_widgets(app); }

static void set_widget_normalized_position(GtkWidget *box, App *app, double x, double y) {
    DeskWidget *widget = desk_of(box); widget->x = CLAMP(x, 0.0, 1.0); widget->y = CLAMP(y, 0.0, 1.0);
    int width = gtk_widget_get_allocated_width(app->widget_canvas), height = gtk_widget_get_allocated_height(app->widget_canvas);
    if (width > 0) gtk_widget_set_margin_start(box, (int)(widget->x * width));
    if (height > 0) gtk_widget_set_margin_top(box, (int)(widget->y * height));
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

static gboolean reposition_wallpaper_widgets(gpointer data) {
    App *app = data; g_object_set_data(G_OBJECT(app->widget_canvas), "reposition-pending", NULL);
    for (guint i = 0; app->wallpaper_widgets && i < app->wallpaper_widgets->len; i++) { GtkWidget *box = g_ptr_array_index(app->wallpaper_widgets, i); DeskWidget *widget = desk_of(box); set_widget_normalized_position(box, app, widget->x, widget->y); }
    invalidate_wallpaper_widgets(app); update_widget_labels(app); return G_SOURCE_REMOVE;
}

/* Margins changed during an allocation are not applied until the next one, so move widgets from an idle callback. */
static void position_wallpaper_widgets(GtkWidget *canvas, GtkAllocation *allocation, App *app) {
    int size = allocation->width * 65536 + allocation->height;
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(canvas), "canvas-size")) == size) return;
    g_object_set_data(G_OBJECT(canvas), "canvas-size", GINT_TO_POINTER(size));
    if (!g_object_get_data(G_OBJECT(canvas), "reposition-pending")) { g_object_set_data(G_OBJECT(canvas), "reposition-pending", GINT_TO_POINTER(TRUE)); g_idle_add(reposition_wallpaper_widgets, app); }
}

/* Only place the widget; snapping here would move saved widgets every time the editor opens. Dragging still snaps. */
static void wallpaper_widget_map(GtkWidget *box, App *app) {
    DeskWidget *widget = desk_of(box); set_widget_normalized_position(box, app, widget->x, widget->y);
}

static gboolean draw_widget_color_preview(GtkWidget *area, cairo_t *cr, App *app) {
    GtkWidget *button = gtk_widget_get_parent(area); const char *key = g_object_get_data(G_OBJECT(button), "widget-color-key"); DeskWidget *widget = desk_of(app->selected_widget); int width = gtk_widget_get_allocated_width(area), height = gtk_widget_get_allocated_height(area); if (!widget || !key) { cairo_set_source_rgb(cr, 0.12, 0.15, 0.16); cairo_paint(cr); return FALSE; }
    float *start = desk_widget_color(widget, key), *end = widget->gradient_color; cairo_pattern_t *pattern = NULL;
    if (!strcmp(key, "text") && widget->gradient) { double angle = widget->gradient_angle * G_PI / 180.0, cx = width / 2.0, cy = height / 2.0, dx = cos(angle) * width / 2.0, dy = sin(angle) * height / 2.0; pattern = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy); cairo_pattern_add_color_stop_rgba(pattern, 0, start[0], start[1], start[2], start[3]); cairo_pattern_add_color_stop_rgba(pattern, 1, end[0], end[1], end[2], end[3]); cairo_set_source(cr, pattern); } else cairo_set_source_rgba(cr, start[0], start[1], start[2], start[3]); cairo_rectangle(cr, 1, 1, MAX(1, width - 2), MAX(1, height - 2)); cairo_fill(cr); if (pattern) cairo_pattern_destroy(pattern); cairo_set_source_rgba(cr, 1, 1, 1, 0.55); cairo_set_line_width(cr, 1); cairo_rectangle(cr, 1, 1, MAX(1, width - 2), MAX(1, height - 2)); cairo_stroke(cr); return FALSE;
}

static GtkWidget *widget_color_button(App *app, const char *key) {
    GtkWidget *button = gtk_button_new(); GtkWidget *preview = gtk_drawing_area_new(); gtk_widget_set_size_request(preview, 88, 26); g_object_set_data(G_OBJECT(button), "widget-color-key", (gpointer)key); g_signal_connect(preview, "draw", G_CALLBACK(draw_widget_color_preview), app); gtk_container_add(GTK_CONTAINER(button), preview); return button;
}

static void refresh_widget_color_previews(App *app) {
    GtkWidget *buttons[] = { app->widget_settings_color, app->widget_settings_accent, app->widget_settings_secondary, app->widget_settings_box_color }; for (guint i = 0; i < G_N_ELEMENTS(buttons); i++) if (buttons[i]) gtk_widget_queue_draw(gtk_bin_get_child(GTK_BIN(buttons[i])));
}

typedef struct { App *app; GtkWidget *window, *list, *search, *target, *button; } FontPicker;

static gboolean font_row_filter(GtkListBoxRow *row, gpointer data) {
    FontPicker *picker = data; const char *term = gtk_entry_get_text(GTK_ENTRY(picker->search)); GtkWidget *label = gtk_bin_get_child(GTK_BIN(row)); const char *name = gtk_label_get_text(GTK_LABEL(label)); char *lower_name = g_ascii_strdown(name, -1); char *lower_term = g_ascii_strdown(term, -1); gboolean visible = !lower_term[0] || strstr(lower_name, lower_term) != NULL; g_free(lower_name); g_free(lower_term); return visible;
}

static void font_row_activated(GtkListBox *list, GtkListBoxRow *row, FontPicker *picker) {
    (void)list; GtkWidget *label = gtk_bin_get_child(GTK_BIN(row)); const char *font = gtk_label_get_text(GTK_LABEL(label));
    if (is_live_widget(picker->app, picker->target)) { DeskWidget *widget = desk_of(picker->target); g_free(widget->font); widget->font = g_strdup(font); if (picker->target == picker->app->selected_widget) gtk_button_set_label(GTK_BUTTON(picker->button), font); wallpaper_widgets_changed(picker->app); }
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
    if (!is_live_widget(picker->app, picker->target)) return;
    DeskWidget *widget = desk_of(picker->target); memcpy(desk_widget_color(widget, picker->key), picker->color, sizeof(picker->color));
    if (!strcmp(picker->key, "text")) memcpy(widget->gradient_color, picker->gradient_color, sizeof(picker->gradient_color));
    refresh_widget_color_previews(picker->app); wallpaper_widgets_changed(picker->app);
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
    if (!is_live_widget(picker->app, picker->target)) return;
    desk_of(picker->target)->gradient_angle = angle % 360; refresh_widget_color_previews(picker->app); wallpaper_widgets_changed(picker->app);
}
static void picker_gradient_toggled(GtkToggleButton *toggle, ColorPicker *picker) { gboolean active = gtk_toggle_button_get_active(toggle); if (picker->gradient_box) gtk_widget_set_visible(picker->gradient_box, active); if (!is_live_widget(picker->app, picker->target)) return; desk_of(picker->target)->gradient = active; refresh_widget_color_previews(picker->app); wallpaper_widgets_changed(picker->app); }
static void picker_destroy(GtkWidget *window, ColorPicker *picker) { (void)window; g_free(picker); }

static const char *color_title(const char *key) { return !strcmp(key, "accent") ? "Accent color" : !strcmp(key, "secondary") ? "Secondary color" : !strcmp(key, "panel") ? "Background color" : "Widget text color"; }

static void widget_color_button_clicked(GtkButton *button, App *app) {
    if (!app->selected_widget) return;
    ColorPicker *picker = g_new0(ColorPicker, 1); picker->app = app; picker->target = app->selected_widget; g_strlcpy(picker->key, g_object_get_data(G_OBJECT(button), "widget-color-key"), sizeof(picker->key)); DeskWidget *widget = desk_of(picker->target); memcpy(picker->color, desk_widget_color(widget, picker->key), sizeof(picker->color)); if (!strcmp(picker->key, "text")) memcpy(picker->gradient_color, widget->gradient_color, sizeof(picker->gradient_color)); picker->window = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(picker->window), color_title(picker->key)); gtk_window_set_transient_for(GTK_WINDOW(picker->window), GTK_WINDOW(app->window)); gtk_window_set_default_size(GTK_WINDOW(picker->window), 360, 560);
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); gtk_container_set_border_width(GTK_CONTAINER(outer), 12); gtk_container_add(GTK_CONTAINER(picker->window), outer); picker->sv_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->sv_area, -1, 230); gtk_widget_add_events(picker->sv_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(outer), picker->sv_area, TRUE, TRUE, 0); picker->hue_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->hue_area, -1, 24); gtk_widget_add_events(picker->hue_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(outer), picker->hue_area, FALSE, FALSE, 0); picker->alpha_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->alpha_area, -1, 20); gtk_widget_add_events(picker->alpha_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(outer), picker->alpha_area, FALSE, FALSE, 0);
    if (!strcmp(picker->key, "text")) { picker->gradient_toggle = gtk_check_button_new_with_label("Use gradient"); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(picker->gradient_toggle), widget->gradient); gtk_box_pack_start(GTK_BOX(outer), picker->gradient_toggle, FALSE, FALSE, 0); g_signal_connect(picker->gradient_toggle, "toggled", G_CALLBACK(picker_gradient_toggled), picker); picker->gradient_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); gtk_box_pack_start(GTK_BOX(picker->gradient_box), gtk_label_new("Gradient color"), FALSE, FALSE, 0); picker->gradient_sv_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->gradient_sv_area, -1, 150); gtk_widget_add_events(picker->gradient_sv_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->gradient_sv_area, FALSE, FALSE, 0); picker->gradient_hue_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->gradient_hue_area, -1, 18); gtk_widget_add_events(picker->gradient_hue_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->gradient_hue_area, FALSE, FALSE, 0); picker->gradient_alpha_area = gtk_drawing_area_new(); gtk_widget_set_size_request(picker->gradient_alpha_area, -1, 16); gtk_widget_add_events(picker->gradient_alpha_area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->gradient_alpha_area, FALSE, FALSE, 0); GtkWidget *angle_label = gtk_label_new("Gradient angle (degrees)"); gtk_widget_set_halign(angle_label, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(picker->gradient_box), angle_label, FALSE, FALSE, 0); picker->angle = gtk_spin_button_new_with_range(0, 360, 15); gtk_spin_button_set_digits(GTK_SPIN_BUTTON(picker->angle), 0); gtk_spin_button_set_value(GTK_SPIN_BUTTON(picker->angle), widget->gradient_angle); gtk_box_pack_start(GTK_BOX(picker->gradient_box), picker->angle, FALSE, FALSE, 0); g_signal_connect(picker->angle, "value-changed", G_CALLBACK(picker_angle_changed), picker); gtk_box_pack_start(GTK_BOX(outer), picker->gradient_box, FALSE, FALSE, 0); }
    g_signal_connect(picker->sv_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->hue_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->alpha_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->sv_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->hue_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->alpha_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->sv_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->hue_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->alpha_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->sv_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->hue_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->alpha_area, "button-release-event", G_CALLBACK(picker_event), picker); if (picker->gradient_box) { g_signal_connect(picker->gradient_sv_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->gradient_hue_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->gradient_alpha_area, "draw", G_CALLBACK(picker_draw), picker); g_signal_connect(picker->gradient_sv_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_hue_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_alpha_area, "motion-notify-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_sv_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_hue_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_alpha_area, "button-press-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_sv_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_hue_area, "button-release-event", G_CALLBACK(picker_event), picker); g_signal_connect(picker->gradient_alpha_area, "button-release-event", G_CALLBACK(picker_event), picker); }
    g_signal_connect(picker->window, "destroy", G_CALLBACK(picker_destroy), picker); gtk_widget_show_all(picker->window); if (picker->gradient_box) gtk_widget_set_visible(picker->gradient_box, widget->gradient);
}

static void widget_option_changed(GtkWidget *control, App *app) {
    GtkWidget *owner = g_object_get_data(G_OBJECT(control), "option-owner"); if (app->widget_settings_updating || !is_live_widget(app, owner)) return;
    DeskWidget *widget = desk_of(owner); const char *key = g_object_get_data(G_OBJECT(control), "option-key"); char *value = NULL;
    if (GTK_IS_SPIN_BUTTON(control)) value = g_strdup_printf("%d", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(control)));
    else if (GTK_IS_TOGGLE_BUTTON(control)) value = g_strdup(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control)) ? "1" : "0");
    else if (GTK_IS_COMBO_BOX(control)) value = g_strdup(gtk_combo_box_get_active_id(GTK_COMBO_BOX(control)));
    else if (GTK_IS_ENTRY(control)) value = g_strdup(gtk_entry_get_text(GTK_ENTRY(control)));
    if (value && strcmp(value, desk_widget_get(widget, key))) { desk_widget_set(widget, key, value); wallpaper_widgets_changed(app); }
    g_free(value);
}

static gboolean widget_option_focus_out(GtkWidget *entry, GdkEvent *event, App *app) { (void)event; widget_option_changed(entry, app); return FALSE; }

/* Builds the control for one kind-specific option from its description in widgets.c. */
static void add_widget_option(App *app, const WidgetOption *option) {
    DeskWidget *widget = desk_of(app->selected_widget); GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), *control = NULL; const char *value = desk_widget_get(widget, option->key);
    if (option->type != WIDGET_OPT_BOOL) gtk_box_pack_start(GTK_BOX(row), gtk_label_new(option->label), FALSE, FALSE, 0);
    if (option->type == WIDGET_OPT_BOOL) { control = gtk_check_button_new_with_label(option->label); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control), atoi(value)); g_signal_connect(control, "toggled", G_CALLBACK(widget_option_changed), app); }
    else if (option->type == WIDGET_OPT_INT) { control = gtk_spin_button_new_with_range(option->min, option->max, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(control), desk_widget_int(widget, option->key)); g_signal_connect(control, "value-changed", G_CALLBACK(widget_option_changed), app); }
    else if (option->type == WIDGET_OPT_CHOICE) {
        control = gtk_combo_box_text_new(); char **choices = g_strsplit(option->choices, "|", -1);
        for (guint i = 0; choices[i]; i++) { char **pair = g_strsplit(choices[i], ":", 2); if (pair[0] && pair[1]) gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(control), pair[0], pair[1]); g_strfreev(pair); }
        g_strfreev(choices); gtk_combo_box_set_active_id(GTK_COMBO_BOX(control), value); g_signal_connect(control, "changed", G_CALLBACK(widget_option_changed), app);
    } else {
        control = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(control), value); gtk_entry_set_width_chars(GTK_ENTRY(control), !strcmp(option->key, "command") ? 42 : 16); gtk_widget_set_tooltip_text(control, "Press Enter to apply");
        if (!strcmp(option->key, "player")) gtk_entry_set_placeholder_text(GTK_ENTRY(control), "Any player");
        g_signal_connect(control, "activate", G_CALLBACK(widget_option_changed), app); g_signal_connect(control, "focus-out-event", G_CALLBACK(widget_option_focus_out), app);
    }
    g_object_set_data(G_OBJECT(control), "option-key", (gpointer)option->key); g_object_set_data(G_OBJECT(control), "option-owner", app->selected_widget);
    gtk_box_pack_start(GTK_BOX(row), control, FALSE, FALSE, 0); gtk_container_add(GTK_CONTAINER(app->widget_settings_options), row);
}

static void update_widget_inspector(App *app) {
    DeskWidget *widget = desk_of(app->selected_widget);
    app->widget_settings_updating = TRUE;
    GList *children = gtk_container_get_children(GTK_CONTAINER(app->widget_settings_options)); for (GList *l = children; l; l = l->next) gtk_widget_destroy(l->data); g_list_free(children);
    if (!widget) {
        gtk_label_set_text(GTK_LABEL(app->widget_settings_name), "Select a widget");
        gtk_widget_set_sensitive(app->widget_settings_panel, FALSE); refresh_widget_color_previews(app);
        app->widget_settings_updating = FALSE;
        return;
    }
    gtk_label_set_text(GTK_LABEL(app->widget_settings_name), desk_widget_kind_label(widget->kind));
    gtk_button_set_label(GTK_BUTTON(app->widget_settings_font), widget->font ? widget->font : "Sans");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_size), widget->size); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_scale), widget->scale); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_width), widget->width); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_radius), widget->radius);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->widget_settings_bold), widget->bold); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->widget_settings_italic), widget->italic); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->widget_settings_box), widget->panel);
    gtk_range_set_value(GTK_RANGE(app->widget_settings_spacing), widget->spacing); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_stretch_x), widget->stretch_x); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_stretch_y), widget->stretch_y);
    guint count = 0; const WidgetOption *options = desk_widget_options(&count);
    for (guint i = 0; i < count; i++) if (!strcmp(options[i].kind, widget->kind)) add_widget_option(app, &options[i]);
    gtk_widget_show_all(app->widget_settings_options);
    refresh_widget_color_previews(app);
    gtk_widget_set_sensitive(app->widget_settings_panel, TRUE);
    app->widget_settings_updating = FALSE;
}

static void widget_style_changed(GtkWidget *control, App *app) {
    if (app->widget_settings_updating || !app->selected_widget) return;
    DeskWidget *widget = desk_of(app->selected_widget);
    if (GTK_IS_SPIN_BUTTON(control)) {
        int value = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(control));
        if (control == app->widget_settings_size) widget->size = CLAMP(value, 8, 160); else if (control == app->widget_settings_width) widget->width = CLAMP(value, 0, 1600); else if (control == app->widget_settings_scale) widget->scale = CLAMP(value, 25, 400);
        else if (control == app->widget_settings_stretch_x) widget->stretch_x = CLAMP(value, 50, 200); else if (control == app->widget_settings_stretch_y) widget->stretch_y = CLAMP(value, 50, 200); else if (control == app->widget_settings_radius) widget->radius = CLAMP(value, 0, 200);
    } else if (GTK_IS_RANGE(control)) widget->spacing = CLAMP((int)round(gtk_range_get_value(GTK_RANGE(control))), 0, 40);
    else if (control == app->widget_settings_bold) widget->bold = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control));
    else if (control == app->widget_settings_italic) widget->italic = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control));
    else if (control == app->widget_settings_box) widget->panel = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control));
    wallpaper_widgets_changed(app);
}

static void select_wallpaper_widget(App *app, GtkWidget *box) {
    if (app->selected_widget == box) return;
    if (app->selected_widget) gtk_style_context_remove_class(gtk_widget_get_style_context(app->selected_widget), "wallpaper-widget-selected");
    app->selected_widget = box;
    if (box) gtk_style_context_add_class(gtk_widget_get_style_context(box), "wallpaper-widget-selected");
    update_widget_inspector(app);
}

static void delete_selected_widget(GtkButton *button, App *app) {
    (void)button;
    GtkWidget *box = app->selected_widget; if (!box) return;
    select_wallpaper_widget(app, NULL);
    g_ptr_array_remove(app->wallpaper_widgets, box);
    gtk_widget_destroy(box);
    wallpaper_widgets_changed(app);
}

static gboolean wallpaper_widget_press(GtkWidget *widget, GdkEventButton *event, App *app) {
    if (event->button != GDK_BUTTON_PRIMARY) return FALSE;
    /* Focus first so a half-typed option entry commits to the widget it belongs to. */
    gtk_widget_grab_focus(widget); select_wallpaper_widget(app, widget);
    g_object_set_data(G_OBJECT(widget), "dragging", GINT_TO_POINTER(TRUE));
    g_object_set_data(G_OBJECT(widget), "drag-x", GINT_TO_POINTER((int)event->x_root));
    g_object_set_data(G_OBJECT(widget), "drag-y", GINT_TO_POINTER((int)event->y_root));
    g_object_set_data(G_OBJECT(widget), "drag-left", GINT_TO_POINTER(gtk_widget_get_margin_start(widget)));
    g_object_set_data(G_OBJECT(widget), "drag-top", GINT_TO_POINTER(gtk_widget_get_margin_top(widget)));
    gtk_widget_show(app->widget_grid); gtk_widget_queue_draw(app->widget_grid); return TRUE;
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

static GtkWidget *create_wallpaper_widget(App *app, DeskWidget *widget) {
    GtkWidget *event_box = gtk_event_box_new(); GtkWidget *label = gtk_drawing_area_new(); gint64 *bucket = g_new(gint64, 1); *bucket = -1;
    gtk_widget_set_can_focus(event_box, TRUE); gtk_widget_add_events(event_box, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
    gtk_style_context_add_class(gtk_widget_get_style_context(event_box), "wallpaper-widget"); gtk_container_add(GTK_CONTAINER(event_box), label);
    g_object_set_data_full(G_OBJECT(event_box), "desk-widget", widget, (GDestroyNotify)desk_widget_free); g_object_set_data(G_OBJECT(event_box), "widget-app", app); g_object_set_data(G_OBJECT(event_box), "widget-label", label); g_object_set_data_full(G_OBJECT(event_box), "widget-bucket", bucket, g_free);
    g_object_set_data(G_OBJECT(label), "widget-owner", event_box); g_signal_connect(label, "draw", G_CALLBACK(draw_wallpaper_widget), event_box);
    g_signal_connect(event_box, "button-press-event", G_CALLBACK(wallpaper_widget_press), app); g_signal_connect(event_box, "motion-notify-event", G_CALLBACK(wallpaper_widget_motion), app); g_signal_connect(event_box, "button-release-event", G_CALLBACK(wallpaper_widget_release), app); g_signal_connect(event_box, "map", G_CALLBACK(wallpaper_widget_map), app);
    char *tooltip = g_strdup_printf("%s · drag to move", desk_widget_kind_label(widget->kind)); gtk_widget_set_halign(event_box, GTK_ALIGN_START); gtk_widget_set_valign(event_box, GTK_ALIGN_START); gtk_widget_set_tooltip_text(event_box, tooltip); g_free(tooltip); set_widget_normalized_position(event_box, app, widget->x, widget->y);
    gtk_overlay_add_overlay(GTK_OVERLAY(app->widget_canvas), event_box); g_ptr_array_add(app->wallpaper_widgets, event_box); gtk_widget_show_all(event_box); return event_box;
}

static void add_wallpaper_widget(GtkMenuItem *item, App *app) {
    const char *kind = g_object_get_data(G_OBJECT(item), "widget-kind"); guint index = app->wallpaper_widgets->len; DeskWidget *widget = desk_widget_new(kind);
    widget->x = 0.04 + (index % 3) * 0.18; widget->y = 0.05 + (index / 3 % 6) * 0.10;
    GtkWidget *box = create_wallpaper_widget(app, widget); wallpaper_widgets_changed(app); select_wallpaper_widget(app, box);
}

static void load_saved_wallpaper_widgets(App *app) {
    char *contents = desk_widgets_read(); GPtrArray *widgets = desk_widgets_parse(contents); g_free(contents);
    g_ptr_array_set_free_func(widgets, NULL); for (guint i = 0; i < widgets->len; i++) create_wallpaper_widget(app, g_ptr_array_index(widgets, i));
    g_ptr_array_free(widgets, TRUE); sync_widget_feed(app); update_widget_labels(app);
}

static void widget_page_destroy(GtkWidget *page, App *app) {
    (void)page;
    if (app->widget_clock_id) { g_source_remove(app->widget_clock_id); app->widget_clock_id = 0; }
    widget_feed_free(app->widget_feed); app->widget_feed = NULL; widget_snapshot_clear(&app->widget_snapshot);
}

void add_css(void) {
    GtkCssProvider *widget_css = gtk_css_provider_new(); gtk_css_provider_load_from_data(widget_css, ".wallpaper-widget { border: 0; background: transparent; } .wallpaper-widget-selected { border: 0; box-shadow: 0 0 0 1px #ffd84a; } .widget-options flowboxchild, .widget-options flowboxchild:hover { padding: 2px 0; border: 0; background: transparent; } scrolledwindow.widget-options { border: 0; } spinbutton { color: #dce5e8; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(widget_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1); g_object_unref(widget_css);
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
    GtkWidget *widget_menu = gtk_menu_new(); guint kind_count = 0; const WidgetKind *kinds = desk_widget_kinds(&kind_count);
    for (guint i = 0; i < kind_count; i++) { GtkWidget *item = gtk_menu_item_new_with_label(kinds[i].label); gtk_widget_set_size_request(item, 160, -1); g_object_set_data(G_OBJECT(item), "widget-kind", (gpointer)kinds[i].kind); gtk_menu_shell_append(GTK_MENU_SHELL(widget_menu), item); g_signal_connect(item, "activate", G_CALLBACK(add_wallpaper_widget), app); }
    gtk_widget_show_all(widget_menu); gtk_menu_button_set_popup(GTK_MENU_BUTTON(add_widget), widget_menu);
    GtkWidget *inspector = gtk_frame_new("Selected widget"); app->widget_settings_panel = inspector; gtk_widget_set_hexpand(inspector, TRUE);
    GtkWidget *inspector_grid = gtk_grid_new(); gtk_container_set_border_width(GTK_CONTAINER(inspector_grid), 10); gtk_grid_set_row_spacing(GTK_GRID(inspector_grid), 6); gtk_grid_set_column_spacing(GTK_GRID(inspector_grid), 8); gtk_container_add(GTK_CONTAINER(inspector), inspector_grid);
    app->widget_settings_name = gtk_label_new("Select a widget"); gtk_widget_set_halign(app->widget_settings_name, GTK_ALIGN_START); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_name, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Font"), 1, 0, 1, 1); app->widget_settings_font = gtk_button_new_with_label("Sans"); gtk_widget_set_size_request(app->widget_settings_font, 130, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_font, 2, 0, 1, 1); g_signal_connect(app->widget_settings_font, "clicked", G_CALLBACK(font_button_clicked), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Size"), 3, 0, 1, 1); app->widget_settings_size = gtk_spin_button_new_with_range(8, 160, 1); gtk_widget_set_size_request(app->widget_settings_size, 68, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_size, 4, 0, 1, 1); g_signal_connect(app->widget_settings_size, "value-changed", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Scale %"), 5, 0, 1, 1); app->widget_settings_scale = gtk_spin_button_new_with_range(25, 400, 5); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_scale, 6, 0, 1, 1); g_signal_connect(app->widget_settings_scale, "value-changed", G_CALLBACK(widget_style_changed), app);
    app->widget_settings_bold = gtk_check_button_new_with_label("Bold"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_bold, 7, 0, 1, 1); g_signal_connect(app->widget_settings_bold, "toggled", G_CALLBACK(widget_style_changed), app);
    app->widget_settings_italic = gtk_check_button_new_with_label("Italic"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_italic, 8, 0, 1, 1); g_signal_connect(app->widget_settings_italic, "toggled", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Text"), 0, 1, 1, 1); app->widget_settings_color = widget_color_button(app, "text"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_color, 1, 1, 2, 1); g_signal_connect(app->widget_settings_color, "clicked", G_CALLBACK(widget_color_button_clicked), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Accent"), 3, 1, 1, 1); app->widget_settings_accent = widget_color_button(app, "accent"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_accent, 4, 1, 1, 1); g_signal_connect(app->widget_settings_accent, "clicked", G_CALLBACK(widget_color_button_clicked), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Secondary"), 5, 1, 1, 1); app->widget_settings_secondary = widget_color_button(app, "secondary"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_secondary, 6, 1, 1, 1); g_signal_connect(app->widget_settings_secondary, "clicked", G_CALLBACK(widget_color_button_clicked), app);
    app->widget_settings_box = gtk_check_button_new_with_label("Background"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_box, 7, 1, 1, 1); g_signal_connect(app->widget_settings_box, "toggled", G_CALLBACK(widget_style_changed), app); app->widget_settings_box_color = widget_color_button(app, "panel"); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_box_color, 8, 1, 1, 1); g_signal_connect(app->widget_settings_box_color, "clicked", G_CALLBACK(widget_color_button_clicked), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Spacing"), 0, 2, 1, 1); app->widget_settings_spacing = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 40, 1); gtk_scale_set_draw_value(GTK_SCALE(app->widget_settings_spacing), TRUE); gtk_widget_set_size_request(app->widget_settings_spacing, 120, -1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_spacing, 1, 2, 2, 1); g_signal_connect(app->widget_settings_spacing, "value-changed", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Stretch X"), 3, 2, 1, 1); app->widget_settings_stretch_x = gtk_spin_button_new_with_range(50, 200, 1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_stretch_x, 4, 2, 1, 1); g_signal_connect(app->widget_settings_stretch_x, "value-changed", G_CALLBACK(widget_style_changed), app); gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Stretch Y"), 5, 2, 1, 1); app->widget_settings_stretch_y = gtk_spin_button_new_with_range(50, 200, 1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_stretch_y, 6, 2, 1, 1); g_signal_connect(app->widget_settings_stretch_y, "value-changed", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Width"), 7, 2, 1, 1); app->widget_settings_width = gtk_spin_button_new_with_range(0, 1600, 1); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->widget_settings_width), 0); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_width, 8, 2, 1, 1); g_signal_connect(app->widget_settings_width, "value-changed", G_CALLBACK(widget_style_changed), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), gtk_label_new("Corner radius"), 0, 3, 1, 1); app->widget_settings_radius = gtk_spin_button_new_with_range(0, 200, 1); gtk_grid_attach(GTK_GRID(inspector_grid), app->widget_settings_radius, 1, 3, 1, 1); g_signal_connect(app->widget_settings_radius, "value-changed", G_CALLBACK(widget_style_changed), app);
    GtkWidget *delete_widget = gtk_button_new_with_label("Delete"); gtk_widget_set_halign(delete_widget, GTK_ALIGN_END); gtk_grid_attach(GTK_GRID(inspector_grid), delete_widget, 8, 3, 1, 1); g_signal_connect(delete_widget, "clicked", G_CALLBACK(delete_selected_widget), app);
    gtk_grid_attach(GTK_GRID(inspector_grid), section_label("OPTIONS"), 0, 4, 9, 1); app->widget_settings_options = gtk_flow_box_new(); gtk_style_context_add_class(gtk_widget_get_style_context(app->widget_settings_options), "widget-options"); gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(app->widget_settings_options), GTK_SELECTION_NONE); gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(app->widget_settings_options), 6); gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(app->widget_settings_options), 18); gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(app->widget_settings_options), 4); gtk_widget_set_hexpand(app->widget_settings_options, TRUE); gtk_widget_set_valign(app->widget_settings_options, GTK_ALIGN_START); GtkWidget *options_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(options_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC); gtk_widget_set_size_request(options_scroll, -1, 128); gtk_style_context_add_class(gtk_widget_get_style_context(options_scroll), "widget-options"); gtk_container_add(GTK_CONTAINER(options_scroll), app->widget_settings_options); gtk_grid_attach(GTK_GRID(inspector_grid), options_scroll, 0, 5, 9, 1);
    gtk_box_pack_start(GTK_BOX(widgets_page), inspector, FALSE, FALSE, 0); app->widget_feed = widget_feed_new(TRUE); update_widget_inspector(app); load_saved_wallpaper_widgets(app);
    g_signal_connect(widgets_page, "destroy", G_CALLBACK(widget_page_destroy), app); app->widget_clock_id = g_timeout_add(33, update_widget_labels, app); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), widgets_page, gtk_label_new("Widgets"));

    GtkWidget *toy_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(toy_page), 24); GtkWidget *toy_title = brand_header("Shadertoy"); gtk_box_pack_start(GTK_BOX(toy_page), toy_title, FALSE, FALSE, 0); GtkWidget *toy_hint = gtk_label_new("Browse Shadertoy directly. Use the injected import button on a shader page."); gtk_widget_set_halign(toy_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(toy_hint), "muted"); gtk_box_pack_start(GTK_BOX(toy_page), toy_hint, FALSE, FALSE, 3);
    app->toy_status = gtk_label_new(gtk_label_get_text(GTK_LABEL(app->status))); gtk_widget_set_halign(app->toy_status, GTK_ALIGN_START); gtk_label_set_line_wrap(GTK_LABEL(app->toy_status), TRUE); gtk_label_set_ellipsize(GTK_LABEL(app->toy_status), PANGO_ELLIPSIZE_END); gtk_label_set_lines(GTK_LABEL(app->toy_status), 3); gtk_box_pack_start(GTK_BOX(toy_page), app->toy_status, FALSE, FALSE, 3);
#ifdef HAVE_WEBKIT
    GtkWidget *browser_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); GtkWidget *back = gtk_button_new_with_label("‹"); GtkWidget *forward = gtk_button_new_with_label("›"); app->browser_url = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->browser_url), "https://www.shadertoy.com"); GtkWidget *go = gtk_button_new_with_label("Go"); gtk_box_pack_start(GTK_BOX(browser_bar), back, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), forward, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), app->browser_url, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), go, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(toy_page), browser_bar, FALSE, FALSE, 8);
    WebKitUserContentManager *content_manager = webkit_user_content_manager_new(); g_signal_connect(content_manager, "script-message-received::glacier", G_CALLBACK(browser_message), app); webkit_user_content_manager_register_script_message_handler(content_manager, "glacier"); WebKitUserScript *script = webkit_user_script_new(browser_import_script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL); webkit_user_content_manager_add_script(content_manager, script); webkit_user_script_unref(script); app->browser = webkit_web_view_new_with_user_content_manager(content_manager); webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->browser), "https://www.shadertoy.com"); gtk_widget_set_vexpand(app->browser, TRUE); gtk_box_pack_start(GTK_BOX(toy_page), app->browser, TRUE, TRUE, 8); g_signal_connect(back, "clicked", G_CALLBACK(browser_back), app); g_signal_connect(forward, "clicked", G_CALLBACK(browser_forward), app); g_signal_connect(go, "clicked", G_CALLBACK(browser_load), app); g_signal_connect(app->browser_url, "activate", G_CALLBACK(browser_load), app); g_signal_connect(app->browser, "load-changed", G_CALLBACK(browser_load_changed), app);
#else
    GtkWidget *browser_missing = gtk_label_new("Embedded browser unavailable in this build. Install libwebkit2gtk-4.0-dev, rebuild, and this tab will include an Import shader button on Shadertoy pages."); gtk_label_set_line_wrap(GTK_LABEL(browser_missing), TRUE); gtk_widget_set_halign(browser_missing, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), browser_missing, FALSE, FALSE, 10); GtkWidget *external = gtk_button_new_with_label("Open Shadertoy in browser"); gtk_widget_set_halign(external, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), external, FALSE, FALSE, 5); g_signal_connect(external, "clicked", G_CALLBACK(browser_external_clicked), app);
#endif
    gtk_box_pack_start(GTK_BOX(toy_page), section_label("PASTE SHADER CODE"), FALSE, FALSE, 0); app->code_view = gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->code_view), GTK_WRAP_NONE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->code_view), TRUE); GtkWidget *code_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(code_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); gtk_widget_set_size_request(code_scroll, -1, 130); gtk_container_add(GTK_CONTAINER(code_scroll), app->code_view); gtk_box_pack_start(GTK_BOX(toy_page), code_scroll, FALSE, FALSE, 5); GtkWidget *paste_button = gtk_button_new_with_label("Preview pasted shader"); gtk_widget_set_halign(paste_button, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), paste_button, FALSE, FALSE, 0); g_signal_connect(paste_button, "clicked", G_CALLBACK(load_pasted_clicked), app); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), toy_page, gtk_label_new("Shadertoy"));

    GtkWidget *settings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(settings_page), 36); GtkWidget *settings_title = brand_header("Settings"); gtk_box_pack_start(GTK_BOX(settings_page), settings_title, FALSE, FALSE, 0); GtkWidget *settings_hint = gtk_label_new("Connection and wallpaper behavior"); gtk_widget_set_halign(settings_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(settings_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), settings_hint, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(settings_page), section_label("OPTIONAL SHADERTOY API KEY"), FALSE, FALSE, 14); app->api_key = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(app->api_key), FALSE); gtk_entry_set_placeholder_text(GTK_ENTRY(app->api_key), "Paste your Shadertoy app key"); const char *env = g_getenv("SHADERTOY_API_KEY"); if (env) gtk_entry_set_text(GTK_ENTRY(app->api_key), env); gtk_box_pack_start(GTK_BOX(settings_page), app->api_key, FALSE, FALSE, 0); GtkWidget *key_hint = gtk_label_new("The embedded browser imports shaders without this key. The key is only for the legacy REST search."); gtk_widget_set_halign(key_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(key_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), key_hint, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(settings_page), section_label("PEAK WALLPAPER FPS"), FALSE, FALSE, 18);
    GdkMonitor *refresh_monitor = gdk_display_get_primary_monitor(display); if (!refresh_monitor && gdk_display_get_n_monitors(display)) refresh_monitor = gdk_display_get_monitor(display, 0); app->monitor_refresh = refresh_monitor ? (int)round(gdk_monitor_get_refresh_rate(refresh_monitor) / 1000.0) : 0;
    /* Common limits up to 240, plus the monitor's own refresh rate. */
    static const int fps_choices[] = { 24, 30, 45, 60, 75, 90, 100, 120, 144, 165, 180, 200, 240 }; GArray *rates = g_array_new(FALSE, FALSE, sizeof(int)); g_array_append_vals(rates, fps_choices, G_N_ELEMENTS(fps_choices)); gboolean refresh_listed = FALSE;
    for (guint i = 0; i < rates->len; i++) if (g_array_index(rates, int, i) == app->monitor_refresh) refresh_listed = TRUE;
    if (!refresh_listed && app->monitor_refresh > 0 && app->monitor_refresh <= 240) { guint at = 0; while (at < rates->len && g_array_index(rates, int, at) < app->monitor_refresh) at++; g_array_insert_val(rates, at, app->monitor_refresh); }
    app->fps = gtk_combo_box_text_new();
    for (guint i = 0; i < rates->len; i++) { int rate = g_array_index(rates, int, i); char id[16], *label = rate == app->monitor_refresh ? g_strdup_printf("%d FPS · monitor refresh", rate) : g_strdup_printf("%d FPS", rate); g_snprintf(id, sizeof(id), "%d", rate); gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->fps), id, label); g_free(label); }
    g_array_free(rates, TRUE); gtk_widget_set_halign(app->fps, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(settings_page), app->fps, FALSE, FALSE, 0);
    char *vsync_label = app->monitor_refresh > 0 ? g_strdup_printf("V-sync · show frames in step with the monitor (no tearing; caps the wallpaper at %d FPS)", app->monitor_refresh) : g_strdup("V-sync · show frames in step with the monitor (no tearing; caps the wallpaper at its refresh rate)");
    app->vsync = gtk_check_button_new_with_label(vsync_label); g_free(vsync_label); gtk_widget_set_halign(app->vsync, GTK_ALIGN_START); gtk_widget_set_margin_top(app->vsync, 10); gtk_box_pack_start(GTK_BOX(settings_page), app->vsync, FALSE, FALSE, 0);
    app->settings_status = gtk_label_new("Changes apply to the running wallpaper right away."); gtk_widget_set_halign(app->settings_status, GTK_ALIGN_START); gtk_label_set_line_wrap(GTK_LABEL(app->settings_status), TRUE); gtk_style_context_add_class(gtk_widget_get_style_context(app->settings_status), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), app->settings_status, FALSE, FALSE, 8);
    load_wallpaper_settings(app); g_signal_connect(app->fps, "changed", G_CALLBACK(wallpaper_settings_changed), app); g_signal_connect(app->vsync, "toggled", G_CALLBACK(wallpaper_settings_changed), app); GtkWidget *mint_hint = gtk_label_new("X11 mode · wallpaper is shared across all workspaces"); gtk_widget_set_halign(mint_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(mint_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), mint_hint, FALSE, FALSE, 22); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), settings_page, gtk_label_new("Settings"));
    gtk_box_set_spacing(GTK_BOX(selected_strip), 0); gtk_box_set_child_packing(GTK_BOX(selected_strip), app->preview, FALSE, FALSE, 0, GTK_PACK_START); gtk_box_set_child_packing(GTK_BOX(selected_strip), controls, FALSE, FALSE, 0, GTK_PACK_START); gtk_box_set_child_packing(GTK_BOX(selected_strip), constants_panel, TRUE, TRUE, 0, GTK_PACK_START); GtkWidget *section_widgets[] = { app->preview, controls, constants_panel }; for (guint i = 0; i < G_N_ELEMENTS(section_widgets); i++) { gtk_widget_set_margin_start(section_widgets[i], 0); gtk_widget_set_margin_end(section_widgets[i], 0); }
    GtkCssProvider *selected_css = gtk_css_provider_new(); gtk_css_provider_load_from_data(selected_css, ".selected-control { font-size: 13px; padding-left: 2px; padding-right: 2px; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(selected_css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1); g_object_unref(selected_css);
    return root;
}
