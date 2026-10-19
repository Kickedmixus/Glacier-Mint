#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const common_keys[] = { "kind", "x", "y", "font", "size", "bold", "italic", "spacing", "stretch_x", "stretch_y", "width", "scale", "gradient", "gradient_angle", "panel", "radius", "text", "gradient_color", "accent", "secondary", "panel_color", NULL };

char *desk_widgets_read(void) {
    char *path = data_path("widgets.ini"), *contents = read_file(path); g_free(path);
    if (!contents) { path = data_path("widgets.conf"); contents = read_file(path); g_free(path); }
    return contents;
}

static DeskWidget *legacy_widget(const char *line) {
    char kind[16] = "", format[16] = "", font[32] = "Sans"; double x = 0, y = 0; int size = 32, bold = TRUE, italic = FALSE, gradient = FALSE, box = 0, border = 0, radius = 0, glow = 0, glow_blur = 12, width = 0, angle = 0, glow_opacity = 55, spacing = 0, stretch_x = 100, stretch_y = 100;
    float text[4] = {0.90f, 0.96f, 0.98f, 1.0f}, grad[4] = {0.45f, 0.80f, 1.0f, 1.0f}, box_color[4] = {0};
    int fields = sscanf(line, "%15s %lf %lf %15s %31s %d %d %d %f %f %f %f %d %f %f %f %f %d %f %f %f %f %d %d %d %d %d %d %d %d %d %d", kind, &x, &y, format, font, &size, &bold, &italic, &text[0], &text[1], &text[2], &text[3], &gradient, &grad[0], &grad[1], &grad[2], &grad[3], &box, &box_color[0], &box_color[1], &box_color[2], &box_color[3], &border, &radius, &glow, &glow_blur, &width, &angle, &glow_opacity, &spacing, &stretch_x, &stretch_y);
    if (fields < 3 || (strcmp(kind, "time") && strcmp(kind, "date"))) return NULL;
    DeskWidget *widget = desk_widget_new(kind); widget->x = CLAMP(x, 0.0, 1.0); widget->y = CLAMP(y, 0.0, 1.0);
    if (fields >= 4) desk_widget_set(widget, "format", format);
    if (fields >= 5) { for (char *p = font; *p; p++) if (*p == '_') *p = ' '; g_free(widget->font); widget->font = g_strdup(font); }
    if (fields >= 6) widget->size = CLAMP(size, 8, 160);
    if (fields >= 7) widget->bold = !!bold;
    if (fields >= 8) widget->italic = !!italic;
    if (fields >= 12) memcpy(widget->text, text, sizeof(text));
    if (fields >= 17) { widget->gradient = !!gradient; memcpy(widget->gradient_color, grad, sizeof(grad)); }
    if (fields >= 27) widget->width = CLAMP(width, 0, 1600);
    if (fields >= 28) widget->gradient_angle = fields == 28 && angle <= 4 ? 0 : ((int)angle % 360 + 360) % 360;
    if (fields >= 30) widget->spacing = CLAMP(spacing, 0, 40);
    if (fields >= 31) widget->stretch_x = CLAMP(stretch_x, 50, 200);
    if (fields >= 32) widget->stretch_y = CLAMP(stretch_y, 50, 200);
    return widget;
}

static int key_int(GKeyFile *file, const char *group, const char *key, int fallback, int min, int max) {
    return g_key_file_has_key(file, group, key, NULL) ? CLAMP(g_key_file_get_integer(file, group, key, NULL), min, max) : fallback;
}

static void key_color(GKeyFile *file, const char *group, const char *key, float *color) {
    gsize count = 0; double *values = g_key_file_get_double_list(file, group, key, &count, NULL);
    if (values && count == 4) for (int i = 0; i < 4; i++) color[i] = (float)CLAMP(values[i], 0.0, 1.0);
    g_free(values);
}

static DeskWidget *ini_widget(GKeyFile *file, const char *group) {
    char *kind = g_key_file_get_string(file, group, "kind", NULL); if (!kind || !desk_widget_kind_known(kind)) { g_free(kind); return NULL; }
    DeskWidget *widget = desk_widget_new(kind); g_free(kind);
    if (g_key_file_has_key(file, group, "x", NULL)) widget->x = CLAMP(g_key_file_get_double(file, group, "x", NULL), 0.0, 1.0);
    if (g_key_file_has_key(file, group, "y", NULL)) widget->y = CLAMP(g_key_file_get_double(file, group, "y", NULL), 0.0, 1.0);
    char *font = g_key_file_get_string(file, group, "font", NULL); if (font && font[0]) { g_free(widget->font); widget->font = font; } else g_free(font);
    widget->size = key_int(file, group, "size", widget->size, 8, 160); widget->bold = key_int(file, group, "bold", widget->bold, 0, 1); widget->italic = key_int(file, group, "italic", widget->italic, 0, 1);
    widget->spacing = key_int(file, group, "spacing", widget->spacing, 0, 40); widget->stretch_x = key_int(file, group, "stretch_x", widget->stretch_x, 50, 200); widget->stretch_y = key_int(file, group, "stretch_y", widget->stretch_y, 50, 200);
    widget->width = key_int(file, group, "width", widget->width, 0, 1600); widget->scale = key_int(file, group, "scale", widget->scale, 25, 400);
    widget->gradient = key_int(file, group, "gradient", widget->gradient, 0, 1); widget->gradient_angle = key_int(file, group, "gradient_angle", widget->gradient_angle, 0, 359);
    widget->panel = key_int(file, group, "panel", widget->panel, 0, 1); widget->radius = key_int(file, group, "radius", widget->radius, 0, 200);
    key_color(file, group, "text", widget->text); key_color(file, group, "gradient_color", widget->gradient_color); key_color(file, group, "accent", widget->accent); key_color(file, group, "secondary", widget->secondary); key_color(file, group, "panel_color", widget->panel_color);
    gsize count = 0; char **keys = g_key_file_get_keys(file, group, &count, NULL);
    for (gsize i = 0; i < count; i++) if (!g_strv_contains(common_keys, keys[i])) { char *value = g_key_file_get_string(file, group, keys[i], NULL); if (value) g_hash_table_replace(widget->options, g_strdup(keys[i]), value); }
    g_strfreev(keys); return widget;
}

GPtrArray *desk_widgets_parse(const char *contents) {
    GPtrArray *widgets = g_ptr_array_new_with_free_func((GDestroyNotify)desk_widget_free); if (!contents) return widgets;
    GKeyFile *file = g_key_file_new();
    if (g_key_file_load_from_data(file, contents, -1, G_KEY_FILE_NONE, NULL)) {
        gsize count = 0; char **groups = g_key_file_get_groups(file, &count);
        for (gsize i = 0; i < count; i++) { DeskWidget *widget = ini_widget(file, groups[i]); if (widget) g_ptr_array_add(widgets, widget); }
        g_strfreev(groups);
    } else {
        char **lines = g_strsplit(contents, "\n", -1);
        for (guint i = 0; lines[i]; i++) { DeskWidget *widget = legacy_widget(lines[i]); if (widget) g_ptr_array_add(widgets, widget); }
        g_strfreev(lines);
    }
    g_key_file_free(file); return widgets;
}

static void set_number(GKeyFile *file, const char *group, const char *key, double value, const char *format) {
    char buffer[G_ASCII_DTOSTR_BUF_SIZE]; g_key_file_set_value(file, group, key, g_ascii_formatd(buffer, sizeof(buffer), format, value));
}

static void set_color(GKeyFile *file, const char *group, const char *key, const float *color) {
    char parts[4][G_ASCII_DTOSTR_BUF_SIZE]; for (int i = 0; i < 4; i++) g_ascii_formatd(parts[i], sizeof(parts[i]), "%.3f", color[i]);
    char *value = g_strdup_printf("%s;%s;%s;%s", parts[0], parts[1], parts[2], parts[3]); g_key_file_set_value(file, group, key, value); g_free(value);
}

void desk_widgets_save(GPtrArray *widgets) {
    GKeyFile *file = g_key_file_new();
    for (guint i = 0; i < widgets->len; i++) {
        DeskWidget *widget = g_ptr_array_index(widgets, i); char group[32]; g_snprintf(group, sizeof(group), "widget %u", i + 1);
        g_key_file_set_string(file, group, "kind", widget->kind); set_number(file, group, "x", widget->x, "%.6f"); set_number(file, group, "y", widget->y, "%.6f"); g_key_file_set_string(file, group, "font", widget->font ? widget->font : "Sans");
        g_key_file_set_integer(file, group, "size", widget->size); g_key_file_set_integer(file, group, "bold", widget->bold); g_key_file_set_integer(file, group, "italic", widget->italic); g_key_file_set_integer(file, group, "spacing", widget->spacing);
        g_key_file_set_integer(file, group, "stretch_x", widget->stretch_x); g_key_file_set_integer(file, group, "stretch_y", widget->stretch_y); g_key_file_set_integer(file, group, "width", widget->width); g_key_file_set_integer(file, group, "scale", widget->scale);
        g_key_file_set_integer(file, group, "gradient", widget->gradient); g_key_file_set_integer(file, group, "gradient_angle", widget->gradient_angle); g_key_file_set_integer(file, group, "panel", widget->panel); g_key_file_set_integer(file, group, "radius", widget->radius);
        set_color(file, group, "text", widget->text); set_color(file, group, "gradient_color", widget->gradient_color); set_color(file, group, "accent", widget->accent); set_color(file, group, "secondary", widget->secondary); set_color(file, group, "panel_color", widget->panel_color);
        GList *keys = g_list_sort(g_hash_table_get_keys(widget->options), (GCompareFunc)strcmp);
        for (GList *l = keys; l; l = l->next) g_key_file_set_string(file, group, l->data, g_hash_table_lookup(widget->options, l->data));
        g_list_free(keys);
    }
    char *data = g_key_file_to_data(file, NULL, NULL), *path = data_path("widgets.ini"); g_file_set_contents(path, data, -1, NULL);
    g_free(path); g_free(data); g_key_file_free(file);
}
