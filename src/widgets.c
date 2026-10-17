#include "app.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const WidgetKind widget_kinds[] = {
    { "time", "Time" }, { "date", "Date" }, { "digital", "Digital clock" }, { "analog", "Analog clock" },
    { "stats", "PC performance" }, { "eq", "Audio EQ" }, { "playback", "Now playing" }, { "terminal", "Terminal" },
};

static const WidgetOption widget_options[] = {
    { "time", "format", "Format", WIDGET_OPT_CHOICE, "24", 0, 0, "24:24-hour|12:12-hour" },
    { "date", "format", "Format", WIDGET_OPT_CHOICE, "long", 0, 0, "long:Long date|short:Short date" },
    { "digital", "style", "Style", WIDGET_OPT_CHOICE, "font", 0, 0, "font:Font digits|segments:Seven-segment" },
    { "digital", "format", "Format", WIDGET_OPT_CHOICE, "24", 0, 0, "24:24-hour|12:12-hour" },
    { "digital", "date", "Date line", WIDGET_OPT_CHOICE, "none", 0, 0, "none:None|short:Short|long:Long" },
    { "digital", "seconds", "Seconds", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "digital", "blink", "Blinking colon", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "digital", "ampm", "AM/PM", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "analog", "diameter", "Diameter", WIDGET_OPT_INT, "180", 40, 1400, NULL },
    { "analog", "hand_width", "Hand width", WIDGET_OPT_INT, "5", 1, 40, NULL },
    { "analog", "numerals", "Numerals", WIDGET_OPT_CHOICE, "quarters", 0, 0, "none:None|quarters:12 3 6 9|all:All" },
    { "analog", "ticks", "Ticks", WIDGET_OPT_CHOICE, "all", 0, 0, "none:None|hours:Hours|all:Hours and minutes" },
    { "analog", "ring", "Outer ring", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "analog", "seconds", "Second hand", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "analog", "smooth", "Smooth sweep", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "stats", "style", "Style", WIDGET_OPT_CHOICE, "bars", 0, 0, "bars:Bars|graphs:Graphs|text:Text only" },
    { "stats", "length", "Bar length", WIDGET_OPT_INT, "220", 80, 1600, NULL },
    { "stats", "cpu", "CPU", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "stats", "gpu", "GPU", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "stats", "ram", "RAM", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "stats", "swap", "Swap", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "stats", "disk", "Disk", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "stats", "temp", "Temperatures", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "stats", "net", "Network", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "stats", "uptime", "Uptime", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "eq", "style", "Style", WIDGET_OPT_CHOICE, "bars", 0, 0, "bars:Bars|mirror:Mirrored|line:Line|dots:Dots" },
    { "eq", "bars", "Bars", WIDGET_OPT_INT, "28", 4, WIDGET_MAX_BARS, NULL },
    { "eq", "length", "Length", WIDGET_OPT_INT, "320", 40, 3000, NULL },
    { "eq", "height", "Height", WIDGET_OPT_INT, "80", 12, 1000, NULL },
    { "eq", "gap", "Gap", WIDGET_OPT_INT, "3", 0, 40, NULL },
    { "eq", "sensitivity", "Sensitivity %", WIDGET_OPT_INT, "100", 10, 400, NULL },
    { "eq", "smoothing", "Smoothing %", WIDGET_OPT_INT, "60", 0, 95, NULL },
    { "eq", "rounded", "Rounded", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "eq", "peaks", "Peak caps", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "playback", "layout", "Layout", WIDGET_OPT_CHOICE, "horizontal", 0, 0, "horizontal:Horizontal|vertical:Vertical" },
    { "playback", "length", "Text width", WIDGET_OPT_INT, "300", 80, 1600, NULL },
    { "playback", "art_size", "Art size", WIDGET_OPT_INT, "96", 24, 800, NULL },
    { "playback", "art", "Album art", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "playback", "album", "Album name", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "playback", "progress", "Progress bar", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "playback", "times", "Times", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "playback", "status", "Play state", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "playback", "hide_idle", "Hide when idle", WIDGET_OPT_BOOL, "0", 0, 0, NULL },
    { "playback", "player", "Player", WIDGET_OPT_TEXT, "", 0, 0, NULL },
    { "playback", "idle_text", "Idle text", WIDGET_OPT_TEXT, "Nothing playing", 0, 0, NULL },
    { "terminal", "command", "Command", WIDGET_OPT_TEXT, "uname -sr; uptime -p; df -h / | tail -n 1", 0, 0, NULL },
    { "terminal", "interval", "Refresh (s)", WIDGET_OPT_INT, "5", 1, 3600, NULL },
    { "terminal", "lines", "Lines", WIDGET_OPT_INT, "8", 1, 80, NULL },
    { "terminal", "columns", "Columns", WIDGET_OPT_INT, "56", 10, 240, NULL },
    { "terminal", "title", "Title", WIDGET_OPT_TEXT, "Terminal", 0, 0, NULL },
    { "terminal", "titlebar", "Title bar", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "terminal", "prompt", "Prompt line", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
    { "terminal", "cursor", "Cursor", WIDGET_OPT_BOOL, "1", 0, 0, NULL },
};

static const char *const common_keys[] = { "kind", "x", "y", "font", "size", "bold", "italic", "spacing", "stretch_x", "stretch_y", "width", "scale", "gradient", "gradient_angle", "panel", "radius", "text", "gradient_color", "accent", "secondary", "panel_color", NULL };

const WidgetKind *desk_widget_kinds(guint *count) { *count = G_N_ELEMENTS(widget_kinds); return widget_kinds; }
const WidgetOption *desk_widget_options(guint *count) { *count = G_N_ELEMENTS(widget_options); return widget_options; }

const char *desk_widget_kind_label(const char *kind) {
    for (guint i = 0; i < G_N_ELEMENTS(widget_kinds); i++) if (!strcmp(widget_kinds[i].kind, kind)) return widget_kinds[i].label;
    return kind;
}

static gboolean known_kind(const char *kind) {
    for (guint i = 0; i < G_N_ELEMENTS(widget_kinds); i++) if (!strcmp(widget_kinds[i].kind, kind)) return TRUE;
    return FALSE;
}

static const WidgetOption *find_option(const char *kind, const char *key) {
    for (guint i = 0; i < G_N_ELEMENTS(widget_options); i++) if (!strcmp(widget_options[i].kind, kind) && !strcmp(widget_options[i].key, key)) return &widget_options[i];
    return NULL;
}

static void set_rgba(float *color, float r, float g, float b, float a) { color[0] = r; color[1] = g; color[2] = b; color[3] = a; }

DeskWidget *desk_widget_new(const char *kind) {
    DeskWidget *widget = g_new0(DeskWidget, 1); g_strlcpy(widget->kind, kind, sizeof(widget->kind));
    gboolean terminal = !strcmp(kind, "terminal");
    widget->options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free); widget->font = g_strdup(terminal ? "Monospace" : "Sans"); widget->x = widget->y = 0.05;
    widget->size = !strcmp(kind, "time") ? 48 : !strcmp(kind, "date") ? 26 : !strcmp(kind, "digital") ? 56 : !strcmp(kind, "playback") ? 18 : !strcmp(kind, "stats") ? 15 : terminal ? 13 : 16;
    widget->bold = !terminal && strcmp(kind, "stats"); widget->stretch_x = widget->stretch_y = widget->scale = 100; widget->radius = 12;
    widget->panel = terminal || !strcmp(kind, "playback") || !strcmp(kind, "analog");
    set_rgba(widget->text, 0.90f, 0.96f, 0.98f, 1.0f); set_rgba(widget->gradient_color, 0.45f, 0.80f, 1.0f, 1.0f); set_rgba(widget->accent, 0.45f, 0.80f, 1.0f, 1.0f);
    set_rgba(widget->secondary, 0.66f, 0.88f, 0.94f, 0.55f); set_rgba(widget->panel_color, 0.01f, 0.03f, 0.05f, terminal ? 0.82f : 0.55f);
    return widget;
}

void desk_widget_free(DeskWidget *widget) {
    if (!widget) return;
    g_free(widget->font); g_hash_table_destroy(widget->options); g_free(widget);
}

const char *desk_widget_get(const DeskWidget *widget, const char *key) {
    const char *value = g_hash_table_lookup(widget->options, key); if (value) return value;
    const WidgetOption *option = find_option(widget->kind, key); return option && option->fallback ? option->fallback : "";
}

int desk_widget_int(const DeskWidget *widget, const char *key) {
    const WidgetOption *option = find_option(widget->kind, key); int value = atoi(desk_widget_get(widget, key));
    return option && option->type == WIDGET_OPT_INT ? CLAMP(value, option->min, option->max) : value;
}

void desk_widget_set(DeskWidget *widget, const char *key, const char *value) { g_hash_table_replace(widget->options, g_strdup(key), g_strdup(value ? value : "")); }

float *desk_widget_color(DeskWidget *widget, const char *key) {
    if (!strcmp(key, "gradient")) return widget->gradient_color;
    if (!strcmp(key, "accent")) return widget->accent;
    if (!strcmp(key, "secondary")) return widget->secondary;
    if (!strcmp(key, "panel")) return widget->panel_color;
    return widget->text;
}

int desk_widget_refresh_ms(const DeskWidget *widget) {
    if (!strcmp(widget->kind, "eq")) return 33;
    if (!strcmp(widget->kind, "analog")) return desk_widget_int(widget, "seconds") && desk_widget_int(widget, "smooth") ? 50 : 1000;
    if (!strcmp(widget->kind, "digital")) return desk_widget_int(widget, "blink") ? 500 : 1000;
    if (!strcmp(widget->kind, "terminal")) return desk_widget_int(widget, "cursor") ? 500 : 1000;
    return 1000;
}

/* Settings file. widgets.conf is the pre-1.1 positional format and is only read for migration. */

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
    if (fields >= 28) widget->gradient_angle = fields == 28 && angle <= 4 ? 0 : ((angle % 360) + 360) % 360;
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
    char *kind = g_key_file_get_string(file, group, "kind", NULL); if (!kind || !known_kind(kind)) { g_free(kind); return NULL; }
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

/* Drawing. Every widget lays out in unscaled "logical" units; scale and stretch are applied on top. */

typedef struct { cairo_t *cr; DeskWidget *widget; const WidgetSnapshot *data; double width, height; gboolean hidden; } Canvas;

static void use_font(Canvas *c, double size, gboolean bold) {
    cairo_select_font_face(c->cr, c->widget->font ? c->widget->font : "Sans", c->widget->italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(c->cr, MAX(1.0, size));
}

static int character_count(const char *text) { return MAX(1, (int)g_utf8_strlen(text ? text : "", -1)); }

static double text_advance(Canvas *c, const char *text) {
    cairo_text_extents_t ext; cairo_text_extents(c->cr, text, &ext); return ext.x_advance + MAX(0, character_count(text) - 1) * c->widget->spacing;
}

static void text_path(Canvas *c, double x, double y, const char *text) {
    for (const char *cursor = text; cursor && *cursor; cursor = g_utf8_next_char(cursor)) {
        char part[8]; int length = g_unichar_to_utf8(g_utf8_get_char(cursor), part); part[length] = '\0';
        cairo_move_to(c->cr, x, y); cairo_text_path(c->cr, part); cairo_text_extents_t ext; cairo_text_extents(c->cr, part, &ext); x += ext.x_advance + c->widget->spacing;
    }
}

static void source_color(Canvas *c, const float *color, double alpha) { cairo_set_source_rgba(c->cr, color[0], color[1], color[2], color[3] * alpha); }

/* The text color, or its gradient across the whole widget box. */
static void source_primary(Canvas *c) {
    DeskWidget *w = c->widget; if (!w->gradient) { source_color(c, w->text, 1.0); return; }
    double angle = w->gradient_angle * G_PI / 180.0, cx = c->width / 2.0, cy = c->height / 2.0, dx = cos(angle) * c->width / 2.0, dy = sin(angle) * c->height / 2.0;
    cairo_pattern_t *pattern = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy);
    cairo_pattern_add_color_stop_rgba(pattern, 0, w->text[0], w->text[1], w->text[2], w->text[3]); cairo_pattern_add_color_stop_rgba(pattern, 1, w->gradient_color[0], w->gradient_color[1], w->gradient_color[2], w->gradient_color[3]);
    cairo_set_source(c->cr, pattern); cairo_pattern_destroy(pattern);
}

static void draw_text(Canvas *c, double x, double y, const char *text, const float *color) {
    if (!text || !text[0]) return;
    if (color) source_color(c, color, 1.0); else source_primary(c);
    text_path(c, x, y, text); cairo_fill(c->cr);
}

static void rounded_rect(cairo_t *cr, double x, double y, double width, double height, double radius) {
    radius = CLAMP(radius, 0.0, MIN(width, height) / 2.0);
    if (radius <= 0) { cairo_rectangle(cr, x, y, width, height); return; }
    cairo_new_sub_path(cr); cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI / 2, 0); cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI / 2);
    cairo_arc(cr, x + radius, y + height - radius, radius, G_PI / 2, G_PI); cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2); cairo_close_path(cr);
}

/* Cuts text to fit max_width, ending it with an ellipsis. */
static char *fit_text(Canvas *c, const char *text, double max_width) {
    if (!text) return g_strdup("");
    if (text_advance(c, text) <= max_width) return g_strdup(text);
    for (long length = g_utf8_strlen(text, -1) - 1; length > 0; length--) {
        char *part = g_utf8_substring(text, 0, length), *candidate = g_strconcat(g_strchomp(part), "…", NULL); g_free(part);
        if (text_advance(c, candidate) <= max_width) return candidate;
        g_free(candidate);
    }
    return g_strdup("…");
}

static void local_now(struct tm *local, gint64 *usec) {
    gint64 now = g_get_real_time(); time_t seconds = (time_t)(now / G_USEC_PER_SEC); localtime_r(&seconds, local); if (usec) *usec = now % G_USEC_PER_SEC;
}

/* Time and date: the original widgets. Layout is kept identical to earlier versions so saved positions still line up. */

static char *clock_string(Canvas *c) {
    struct tm local; local_now(&local, NULL); char text[96]; const char *format = desk_widget_get(c->widget, "format");
    if (!strcmp(c->widget->kind, "time")) strftime(text, sizeof(text), !strcmp(format, "12") ? "%I:%M %p" : "%H:%M", &local);
    else strftime(text, sizeof(text), !strcmp(format, "short") ? "%Y-%m-%d" : "%A, %B %d", &local);
    return g_strdup(text);
}

static void text_clock_layout(Canvas *c) {
    char *text = clock_string(c); double padding = 8.0; use_font(c, c->widget->size, c->widget->bold); cairo_font_extents_t font; cairo_font_extents(c->cr, &font);
    c->width = MAX(c->widget->width, (int)ceil(text_advance(c, text) + padding * 2.0)); c->height = (int)ceil(font.height + padding * 2.0); g_free(text);
}

static void text_clock_paint(Canvas *c) {
    char *text = clock_string(c); double padding = 8.0; use_font(c, c->widget->size, c->widget->bold); cairo_font_extents_t font; cairo_font_extents(c->cr, &font);
    draw_text(c, padding, padding + (c->height - padding * 2.0 - font.height) / 2.0 + font.height, text, NULL); g_free(text);
}

/* Digital clock: tabular digits so the width never jumps, or drawn seven-segment digits. */

typedef struct { char main[16], suffix[8], date[96]; gboolean colon_on; } DigitalText;

static void digital_text(Canvas *c, DigitalText *t) {
    struct tm local; gint64 usec; local_now(&local, &usec); DeskWidget *w = c->widget;
    gboolean twelve = !strcmp(desk_widget_get(w, "format"), "12"), seconds = desk_widget_int(w, "seconds"); const char *date = desk_widget_get(w, "date");
    strftime(t->main, sizeof(t->main), twelve ? (seconds ? "%I:%M:%S" : "%I:%M") : (seconds ? "%H:%M:%S" : "%H:%M"), &local);
    t->suffix[0] = '\0'; if (twelve && desk_widget_int(w, "ampm")) strftime(t->suffix, sizeof(t->suffix), "%p", &local);
    t->date[0] = '\0'; if (!strcmp(date, "short")) strftime(t->date, sizeof(t->date), "%Y-%m-%d", &local); else if (!strcmp(date, "long")) strftime(t->date, sizeof(t->date), "%A, %B %d", &local);
    t->colon_on = !desk_widget_int(w, "blink") || usec < 500000;
}

typedef struct { double digit, colon, digit_top, segment_width, segment_thickness, shear; gboolean segments; } DigitalMetrics;

static void digital_metrics(Canvas *c, DigitalMetrics *m) {
    double size = c->widget->size; memset(m, 0, sizeof(*m)); m->segments = !strcmp(desk_widget_get(c->widget, "style"), "segments");
    if (m->segments) { m->digit = size * 0.56; m->segment_thickness = size * (c->widget->bold ? 0.14 : 0.10); m->colon = m->segment_thickness * 2.6; m->digit_top = size; m->shear = c->widget->italic ? size * 0.1 : 0; return; }
    use_font(c, size, c->widget->bold); cairo_text_extents_t ext;
    for (char digit = '0'; digit <= '9'; digit++) { char text[2] = { digit, '\0' }; cairo_text_extents(c->cr, text, &ext); m->digit = MAX(m->digit, ext.x_advance); m->digit_top = MAX(m->digit_top, -ext.y_bearing); }
    cairo_text_extents(c->cr, ":", &ext); m->colon = ext.x_advance;
}

static double digital_main_width(Canvas *c, const DigitalMetrics *m, const char *text) {
    double width = 0, gap = m->segments ? c->widget->size * 0.16 : 0; int count = 0;
    for (const char *p = text; *p; p++, count++) width += *p == ':' ? m->colon : m->digit;
    return width + MAX(0, count - 1) * (gap + c->widget->spacing) + m->shear;
}

static void digital_layout(Canvas *c) {
    DigitalText t; DigitalMetrics m; digital_text(c, &t); digital_metrics(c, &m); double size = c->widget->size, padding = size * 0.18, width = digital_main_width(c, &m, t.main), height = m.digit_top;
    if (t.suffix[0]) { use_font(c, size * 0.34, TRUE); width += size * 0.14 + text_advance(c, t.suffix); }
    if (t.date[0]) { use_font(c, size * 0.3, FALSE); cairo_font_extents_t font; cairo_font_extents(c->cr, &font); width = MAX(width, text_advance(c, t.date)); height += size * 0.24 + font.ascent + font.descent; }
    c->width = MAX(c->widget->width, (int)ceil(width + padding * 2.0)); c->height = (int)ceil(height + padding * 2.0);
}

static void segment_path(cairo_t *cr, double x0, double y0, double x1, double y1, double thickness) {
    double length = hypot(x1 - x0, y1 - y0), dx = (x1 - x0) / length, dy = (y1 - y0) / length, nx = -dy, ny = dx, h = thickness / 2.0, inset = thickness * 0.14;
    x0 += dx * inset; y0 += dy * inset; x1 -= dx * inset; y1 -= dy * inset;
    cairo_move_to(cr, x0, y0); cairo_line_to(cr, x0 + dx * h + nx * h, y0 + dy * h + ny * h); cairo_line_to(cr, x1 - dx * h + nx * h, y1 - dy * h + ny * h);
    cairo_line_to(cr, x1, y1); cairo_line_to(cr, x1 - dx * h - nx * h, y1 - dy * h - ny * h); cairo_line_to(cr, x0 + dx * h - nx * h, y0 + dy * h - ny * h); cairo_close_path(cr);
}

/* Adds lit (or unlit) segments of one digit to the current path. */
static void digit_segments(cairo_t *cr, double x, double y, double width, double height, double thickness, int digit, gboolean lit) {
    static const int masks[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
    double h = thickness / 2.0, left = x + h, right = x + width - h, top = y + h, middle = y + height / 2.0, bottom = y + height - h;
    double segments[7][4] = { { left, top, right, top }, { right, top, right, middle }, { right, middle, right, bottom }, { left, bottom, right, bottom }, { left, middle, left, bottom }, { left, top, left, middle }, { left, middle, right, middle } };
    for (int i = 0; i < 7; i++) if (!!(masks[digit] & (1 << i)) == lit) segment_path(cr, segments[i][0], segments[i][1], segments[i][2], segments[i][3], thickness);
}

static void digital_paint(Canvas *c) {
    DigitalText t; DigitalMetrics m; digital_text(c, &t); digital_metrics(c, &m); DeskWidget *w = c->widget; double size = w->size, padding = size * 0.18, x = padding, top = padding, baseline = padding + m.digit_top;
    double gap = (m.segments ? size * 0.16 : 0) + w->spacing;
    if (m.segments) {
        cairo_matrix_t shear; cairo_matrix_init(&shear, 1, 0, -0.1 * (m.shear > 0), 1, 0.1 * (m.shear > 0) * (top + size), 0);
        for (int lit = 0; lit <= 1; lit++) {
            double cx = x; cairo_save(c->cr); cairo_transform(c->cr, &shear);
            for (const char *p = t.main; *p; p++) {
                if (*p == ':') { if (t.colon_on == lit) { double s = m.segment_thickness; cairo_rectangle(c->cr, cx + (m.colon - s) / 2, top + size * 0.32 - s / 2, s, s); cairo_rectangle(c->cr, cx + (m.colon - s) / 2, top + size * 0.68 - s / 2, s, s); } cx += m.colon + gap; }
                else { digit_segments(c->cr, cx, top, m.digit, size, m.segment_thickness, *p - '0', lit); cx += m.digit + gap; }
            }
            cairo_restore(c->cr); if (lit) source_primary(c); else source_color(c, w->secondary, 0.22); cairo_fill(c->cr);
        }
    } else {
        use_font(c, size, w->bold); double cx = x;
        for (int lit = 0; lit <= 1; lit++) {
            cx = x;
            for (const char *p = t.main; *p; p++) {
                char part[2] = { *p, '\0' }; cairo_text_extents_t ext; cairo_text_extents(c->cr, part, &ext); double cell = *p == ':' ? m.colon : m.digit;
                if ((*p == ':' ? t.colon_on : TRUE) == lit) { cairo_move_to(c->cr, cx + (cell - ext.x_advance) / 2.0, baseline); cairo_text_path(c->cr, part); }
                cx += cell + gap;
            }
            if (lit) source_primary(c); else source_color(c, w->secondary, 1.0); cairo_fill(c->cr);
        }
    }
    double after = x + digital_main_width(c, &m, t.main);
    if (t.suffix[0]) { use_font(c, size * 0.34, TRUE); cairo_text_extents_t ext; cairo_text_extents(c->cr, "AM", &ext); draw_text(c, after + size * 0.14, top - ext.y_bearing, t.suffix, NULL); }
    if (t.date[0]) { use_font(c, size * 0.3, FALSE); cairo_font_extents_t font; cairo_font_extents(c->cr, &font); draw_text(c, x, top + m.digit_top + size * 0.24 + font.ascent, t.date, w->secondary); }
}

/* Analog clock. With Background on, the face is a filled circle. */

static void analog_layout(Canvas *c) { c->width = c->height = desk_widget_int(c->widget, "diameter") + 4; }

static void hand(cairo_t *cr, double cx, double cy, double angle, double tail, double length) {
    cairo_move_to(cr, cx - sin(angle) * tail, cy + cos(angle) * tail); cairo_line_to(cr, cx + sin(angle) * length, cy - cos(angle) * length);
}

static void analog_paint(Canvas *c) {
    DeskWidget *w = c->widget; cairo_t *cr = c->cr; double cx = c->width / 2.0, cy = c->height / 2.0, radius = desk_widget_int(w, "diameter") / 2.0, hand_width = desk_widget_int(w, "hand_width");
    const char *ticks = desk_widget_get(w, "ticks"), *numerals = desk_widget_get(w, "numerals"); gboolean ring = desk_widget_int(w, "ring");
    if (w->panel) { cairo_arc(cr, cx, cy, radius, 0, 2 * G_PI); source_color(c, w->panel_color, 1.0); cairo_fill(cr); }
    if (ring) { double width = MAX(1.5, radius * 0.035); cairo_arc(cr, cx, cy, radius - width / 2.0, 0, 2 * G_PI); cairo_set_line_width(cr, width); source_color(c, w->secondary, 1.0); cairo_stroke(cr); }
    double outer = radius * (ring ? 0.90 : 0.96);
    if (strcmp(ticks, "none")) for (int i = 0; i < 60; i++) {
        gboolean hour = i % 5 == 0; if (!hour && strcmp(ticks, "all")) continue;
        double angle = i * G_PI / 30.0, length = hour ? radius * 0.09 : radius * 0.035;
        hand(cr, cx, cy, angle, -(outer - length), outer); cairo_set_line_width(cr, hour ? MAX(1.0, radius * 0.025) : MAX(0.8, radius * 0.012)); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT); source_color(c, w->secondary, hour ? 1.6 : 1.0); cairo_stroke(cr);
    }
    if (strcmp(numerals, "none")) {
        use_font(c, w->size, w->bold); double distance = outer - radius * 0.09 - w->size * 0.75;
        for (int n = 1; n <= 12; n++) {
            if (!strcmp(numerals, "quarters") && n % 3) continue;
            char text[4]; g_snprintf(text, sizeof(text), "%d", n); cairo_text_extents_t ext; cairo_text_extents(cr, text, &ext); double angle = n * G_PI / 6.0;
            text_path(c, cx + sin(angle) * distance - (ext.x_bearing + ext.width / 2.0 + (character_count(text) - 1) * w->spacing / 2.0), cy - cos(angle) * distance - (ext.height / 2.0 + ext.y_bearing), text);
        }
        source_primary(c); cairo_fill(cr);
    }
    struct tm local; gint64 usec; local_now(&local, &usec); double seconds = local.tm_sec + (desk_widget_int(w, "smooth") ? usec / 1e6 : 0.0), minutes = local.tm_min + local.tm_sec / 60.0, hours = (local.tm_hour % 12) + minutes / 60.0;
    source_primary(c); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    hand(cr, cx, cy, hours * G_PI / 6.0, radius * 0.08, radius * 0.5); cairo_set_line_width(cr, hand_width); cairo_stroke(cr);
    hand(cr, cx, cy, minutes * G_PI / 30.0, radius * 0.08, radius * 0.74); cairo_set_line_width(cr, MAX(1.0, hand_width * 0.65)); cairo_stroke(cr);
    if (desk_widget_int(w, "seconds")) {
        source_color(c, w->accent, 1.0); hand(cr, cx, cy, seconds * G_PI / 30.0, radius * 0.16, radius * 0.84); cairo_set_line_width(cr, MAX(1.0, hand_width * 0.28)); cairo_stroke(cr);
        cairo_arc(cr, cx, cy, MAX(1.5, hand_width * 0.8), 0, 2 * G_PI); cairo_fill(cr);
    } else { cairo_arc(cr, cx, cy, MAX(1.5, hand_width * 0.7), 0, 2 * G_PI); cairo_fill(cr); }
}

/* PC performance. */

typedef struct { const char *key, *label; int history; gboolean bar; } StatRow;
static const StatRow stat_rows[] = {
    { "cpu", "CPU", FEED_CPU, TRUE }, { "gpu", "GPU", FEED_GPU, TRUE }, { "ram", "RAM", FEED_RAM, TRUE }, { "swap", "SWAP", -1, TRUE },
    { "disk", "DISK", -1, TRUE }, { "temp", "TEMP", -1, TRUE }, { "net", "NET", FEED_NET, FALSE }, { "uptime", "UPTIME", -1, FALSE },
};

static double stats_padding(Canvas *c) { return c->widget->panel ? c->widget->size * 0.8 : 2.0; }

static double stat_row_height(Canvas *c, const StatRow *row) {
    double size = c->widget->size; const char *style = desk_widget_get(c->widget, "style");
    if (!strcmp(style, "graphs") && row->history >= 0) return size * 1.4 + size * 1.8 + size * 0.45;
    if (strcmp(style, "text") && row->bar) return size * 1.4 + size * 0.28 + size * 0.45;
    return size * 1.4;
}

static void stats_layout(Canvas *c) {
    double padding = stats_padding(c), height = 0;
    for (guint i = 0; i < G_N_ELEMENTS(stat_rows); i++) if (desk_widget_int(c->widget, stat_rows[i].key)) height += stat_row_height(c, &stat_rows[i]);
    if (height <= 0) height = c->widget->size * 1.4;
    c->width = MAX(c->widget->width, (int)ceil(desk_widget_int(c->widget, "length") + padding * 2)); c->height = (int)ceil(height + padding * 2);
}

static char *format_bytes(double bytes) {
    if (bytes >= 1073741824.0) return g_strdup_printf("%.1f GB", bytes / 1073741824.0);
    if (bytes >= 1048576.0) return g_strdup_printf("%.1f MB", bytes / 1048576.0);
    return g_strdup_printf("%.0f KB", bytes / 1024.0);
}

static char *stat_value(const WidgetSnapshot *d, const char *key, double *fraction) {
    *fraction = -1;
    if (!strcmp(key, "cpu")) { *fraction = d->cpu / 100.0; return g_strdup_printf("%.0f%%", d->cpu); }
    if (!strcmp(key, "gpu")) { if (!d->has_gpu) return g_strdup("n/a"); *fraction = d->gpu / 100.0; return g_strdup_printf("%.0f%%", d->gpu); }
    if (!strcmp(key, "ram")) { *fraction = d->ram / 100.0; return g_strdup_printf("%.1f / %.1f GB", d->ram_used / 1073741824.0, d->ram_total / 1073741824.0); }
    if (!strcmp(key, "swap")) { if (!d->swap_total) { *fraction = 0; return g_strdup("off"); } *fraction = d->swap / 100.0; return g_strdup_printf("%.1f / %.1f GB", d->swap_used / 1073741824.0, d->swap_total / 1073741824.0); }
    if (!strcmp(key, "disk")) { *fraction = d->disk / 100.0; return g_strdup_printf("%.0f / %.0f GB", d->disk_used / 1073741824.0, d->disk_total / 1073741824.0); }
    if (!strcmp(key, "temp")) {
        GString *text = g_string_new(NULL); double hottest = 0;
        if (d->has_cpu_temp) { g_string_append_printf(text, "CPU %.0f°C", d->cpu_temp); hottest = d->cpu_temp; }
        if (d->has_gpu_temp) { g_string_append_printf(text, "%sGPU %.0f°C", text->len ? "  " : "", d->gpu_temp); hottest = MAX(hottest, d->gpu_temp); }
        if (!text->len) g_string_append(text, "n/a"); else *fraction = hottest / 100.0;
        return g_string_free(text, FALSE);
    }
    if (!strcmp(key, "uptime")) { long minutes = (long)(d->uptime / 60), days = minutes / 1440, hours = minutes / 60 % 24; return days ? g_strdup_printf("%ldd %ldh %ldm", days, hours, minutes % 60) : g_strdup_printf("%ldh %ldm", hours, minutes % 60); }
    return g_strdup("");
}

static void triangle(cairo_t *cr, double x, double y, double size, gboolean down) {
    if (down) { cairo_move_to(cr, x, y - size * 0.7); cairo_line_to(cr, x + size, y - size * 0.7); cairo_line_to(cr, x + size / 2, y); }
    else { cairo_move_to(cr, x, y); cairo_line_to(cr, x + size, y); cairo_line_to(cr, x + size / 2, y - size * 0.7); }
    cairo_close_path(cr);
}

static void stats_bar(Canvas *c, double x, double y, double length, double height, double fraction) {
    rounded_rect(c->cr, x, y, length, height, height / 2); source_color(c, c->widget->secondary, 0.45); cairo_fill(c->cr);
    if (fraction > 0) { rounded_rect(c->cr, x, y, MAX(height, length * CLAMP(fraction, 0.0, 1.0)), height, height / 2); source_color(c, c->widget->accent, 1.0); cairo_fill(c->cr); }
}

static void stats_graph(Canvas *c, double x, double y, double length, double height, const float *values, int count, double maximum) {
    cairo_t *cr = c->cr; rounded_rect(cr, x, y, length, height, 3); source_color(c, c->widget->secondary, 0.18); cairo_fill(cr);
    if (count < 2 || maximum <= 0) return;
    for (int i = 0; i < count; i++) { double px = x + length * i / (FEED_HISTORY - 1.0) + length * (FEED_HISTORY - count) / (FEED_HISTORY - 1.0), py = y + height - height * CLAMP(values[i] / maximum, 0.0, 1.0); if (i) cairo_line_to(cr, px, py); else cairo_move_to(cr, px, py); }
    cairo_path_t *line = cairo_copy_path(cr); cairo_line_to(cr, x + length, y + height); cairo_line_to(cr, x + length * (FEED_HISTORY - count) / (FEED_HISTORY - 1.0), y + height); cairo_close_path(cr);
    source_color(c, c->widget->accent, 0.25); cairo_fill(cr); cairo_append_path(cr, line); cairo_path_destroy(line);
    cairo_set_line_width(cr, MAX(1.0, c->widget->size * 0.09)); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); source_color(c, c->widget->accent, 1.0); cairo_stroke(cr);
}

static void stats_paint(Canvas *c) {
    DeskWidget *w = c->widget; const WidgetSnapshot *d = c->data; double size = w->size, padding = stats_padding(c), length = desk_widget_int(w, "length"), y = padding; const char *style = desk_widget_get(w, "style"); gboolean any = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(stat_rows); i++) {
        const StatRow *row = &stat_rows[i]; if (!desk_widget_int(w, row->key)) continue; any = TRUE;
        double baseline = y + size * 1.05, fraction; use_font(c, size * 0.78, TRUE); draw_text(c, padding, baseline, row->label, w->secondary); use_font(c, size, w->bold);
        if (!strcmp(row->key, "net")) {
            char *down = format_bytes(d->net_down), *up = format_bytes(d->net_up), *down_text = g_strconcat(down, "/s", NULL), *up_text = g_strconcat(up, "/s", NULL); double arrow = size * 0.55, gap = size * 0.3;
            double up_x = padding + length - text_advance(c, up_text), up_arrow = up_x - gap - arrow, down_x = up_arrow - size * 0.9 - text_advance(c, down_text), down_arrow = down_x - gap - arrow;
            draw_text(c, down_x, baseline, down_text, NULL); draw_text(c, up_x, baseline, up_text, NULL);
            triangle(c->cr, down_arrow, baseline, arrow, TRUE); triangle(c->cr, up_arrow, baseline, arrow, FALSE); source_color(c, w->accent, 1.0); cairo_fill(c->cr);
            g_free(down); g_free(up); g_free(down_text); g_free(up_text); fraction = -1;
        } else { char *value = stat_value(d, row->key, &fraction); draw_text(c, padding + length - text_advance(c, value), baseline, value, NULL); g_free(value); }
        double below = y + size * 1.4;
        if (!strcmp(style, "graphs") && row->history >= 0) {
            double maximum = row->history == FEED_NET ? 1024.0 : 100.0; for (int k = 0; row->history == FEED_NET && k < d->history_count; k++) maximum = MAX(maximum, d->history[FEED_NET][k]);
            stats_graph(c, padding, below, length, size * 1.8, d->history[row->history], d->history_count, maximum);
        } else if (strcmp(style, "text") && row->bar) stats_bar(c, padding, below, length, MAX(2.0, size * 0.28), fraction);
        y += stat_row_height(c, row);
    }
    if (!any) { use_font(c, size, w->bold); draw_text(c, padding, padding + size * 1.05, "No stats selected", w->secondary); }
}

/* Audio EQ: levels come from the feed's spectrum and are smoothed per widget. */

static double eq_padding(Canvas *c) { return c->widget->panel ? 12.0 : 2.0; }

static void eq_layout(Canvas *c) {
    double padding = eq_padding(c); c->width = MAX(c->widget->width, (int)ceil(desk_widget_int(c->widget, "length") + padding * 2)); c->height = (int)ceil(desk_widget_int(c->widget, "height") + padding * 2);
}

static void eq_update(Canvas *c, int bars) {
    DeskWidget *w = c->widget; gint64 now = g_get_monotonic_time(); double frames = w->levels_at ? CLAMP((now - w->levels_at) / 33333.0, 0.0, 10.0) : 1.0; w->levels_at = now;
    double smoothing = pow(desk_widget_int(w, "smoothing") / 100.0, frames), gain = desk_widget_int(w, "sensitivity") / 100.0, peak_fall = 0.012 * frames;
    for (int i = 0; i < bars; i++) {
        int start = i * FEED_BANDS / bars, end = MAX(start + 1, (i + 1) * FEED_BANDS / bars); double sum = 0;
        for (int b = start; b < end; b++) sum += c->data->bands[MIN(b, FEED_BANDS - 1)];
        double target = CLAMP(sum / (end - start) * gain, 0.0, 1.0), level = w->levels[i];
        level = target > level ? level + (target - level) * (1.0 - smoothing * 0.5) : level * smoothing + target * (1.0 - smoothing);
        w->levels[i] = (float)level; w->peaks[i] = (float)MAX(w->peaks[i] - peak_fall, level);
    }
}

static void eq_paint(Canvas *c) {
    DeskWidget *w = c->widget; cairo_t *cr = c->cr; int bars = desk_widget_int(w, "bars"); double padding = eq_padding(c), length = desk_widget_int(w, "length"), height = desk_widget_int(w, "height"), gap = desk_widget_int(w, "gap");
    const char *style = desk_widget_get(w, "style"); gboolean rounded = desk_widget_int(w, "rounded"), mirror = !strcmp(style, "mirror"); double bar = MAX(1.0, (length - gap * (bars - 1)) / bars), floor_height = 2.0;
    eq_update(c, bars);
    if (!strcmp(style, "line")) {
        for (int i = 0; i < bars; i++) {
            double x = padding + i * (bar + gap) + bar / 2, y = padding + height - MAX(floor_height, w->levels[i] * height);
            if (!i) { cairo_move_to(cr, x, y); continue; }
            double px = padding + (i - 1) * (bar + gap) + bar / 2, py = padding + height - MAX(floor_height, w->levels[i - 1] * height), mx = (px + x) / 2;
            cairo_curve_to(cr, mx, py, mx, y, x, y);
        }
        cairo_set_line_width(cr, MAX(1.5, height * 0.03)); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); source_primary(c); cairo_stroke(cr); return;
    }
    for (int i = 0; i < bars; i++) {
        double x = padding + i * (bar + gap), level = MAX(floor_height, w->levels[i] * height);
        if (!strcmp(style, "dots")) {
            double dot = MIN(bar, height), step = dot + MAX(1.0, gap); int count = MAX(1, (int)round(level / step));
            for (int k = 0; k < count && (k + 1) * step <= height + gap; k++) { double y = padding + height - dot - k * step; if (rounded) { cairo_new_sub_path(cr); cairo_arc(cr, x + bar / 2, y + dot / 2, dot / 2, 0, 2 * G_PI); } else cairo_rectangle(cr, x, y, bar, dot); }
        } else rounded_rect(cr, x, mirror ? padding + (height - level) / 2 : padding + height - level, bar, level, rounded ? bar / 2 : 0);
    }
    source_primary(c); cairo_fill(cr);
    if (desk_widget_int(w, "peaks") && strcmp(style, "dots")) {
        for (int i = 0; i < bars; i++) {
            double x = padding + i * (bar + gap), peak = MAX(floor_height, w->peaks[i] * height) + 3;
            if (mirror) { cairo_rectangle(cr, x, padding + (height - peak) / 2 - 2, bar, 2); cairo_rectangle(cr, x, padding + (height + peak) / 2, bar, 2); }
            else cairo_rectangle(cr, x, MAX(padding, padding + height - peak - 2), bar, 2);
        }
        source_color(c, w->accent, 1.0); cairo_fill(cr);
    }
}

/* Now playing, from the MPRIS player the feed picked. */

typedef struct { double padding, art, gap, text_width, block; gboolean vertical; } PlaybackLayout;

static void playback_metrics(Canvas *c, PlaybackLayout *p) {
    DeskWidget *w = c->widget; double size = w->size;
    p->padding = w->panel ? size * 0.8 : 2.0; p->art = desk_widget_int(w, "art") ? desk_widget_int(w, "art_size") : 0; p->gap = p->art ? size * 0.8 : 0; p->vertical = !strcmp(desk_widget_get(w, "layout"), "vertical");
    p->text_width = desk_widget_int(w, "length"); if (p->vertical) p->text_width = MAX(p->text_width, p->art);
    p->block = size * 1.35 + size * 1.15 + (desk_widget_int(w, "album") ? size * 1.05 : 0) + (desk_widget_int(w, "progress") ? size * 0.85 : 0) + (desk_widget_int(w, "times") || desk_widget_int(w, "status") ? size * 1.05 : 0);
}

static void playback_layout(Canvas *c) {
    PlaybackLayout p; playback_metrics(c, &p);
    c->hidden = !c->data->has_media && desk_widget_int(c->widget, "hide_idle") && !c->data->editing;
    double width = p.vertical ? p.text_width : p.art + p.gap + p.text_width, height = p.vertical ? p.art + p.gap + p.block : MAX(p.art, p.block);
    c->width = MAX(c->widget->width, (int)ceil(width + p.padding * 2)); c->height = (int)ceil(height + p.padding * 2);
}

static char *format_time(gint64 usec) { long seconds = (long)(MAX(usec, 0) / G_USEC_PER_SEC); return seconds >= 3600 ? g_strdup_printf("%ld:%02ld:%02ld", seconds / 3600, seconds / 60 % 60, seconds % 60) : g_strdup_printf("%ld:%02ld", seconds / 60, seconds % 60); }

static void playback_art(Canvas *c, double x, double y, double size) {
    cairo_t *cr = c->cr; double radius = MIN(c->widget->radius * 0.7, size / 4); cairo_surface_t *art = c->data->has_media ? c->data->art : NULL;
    if (art) {
        int width = cairo_image_surface_get_width(art), height = cairo_image_surface_get_height(art); double scale = MAX(size / MAX(1, width), size / MAX(1, height));
        cairo_save(cr); rounded_rect(cr, x, y, size, size, radius); cairo_clip(cr); cairo_translate(cr, x + (size - width * scale) / 2, y + (size - height * scale) / 2); cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, art, 0, 0); cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD); cairo_paint(cr); cairo_restore(cr); return;
    }
    rounded_rect(cr, x, y, size, size, radius); source_color(c, c->widget->secondary, 0.35); cairo_fill(cr);
    double cx = x + size / 2, cy = y + size / 2; cairo_new_sub_path(cr); cairo_arc(cr, cx - size * 0.08, cy + size * 0.14, size * 0.1, 0, 2 * G_PI); cairo_fill(cr);
    cairo_move_to(cr, cx + size * 0.02 - size * 0.025, cy + size * 0.14); cairo_line_to(cr, cx + size * 0.02 - size * 0.025, cy - size * 0.22); cairo_line_to(cr, cx + size * 0.18, cy - size * 0.12);
    cairo_set_line_width(cr, MAX(1.0, size * 0.05)); cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND); cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND); source_primary(c); cairo_stroke(cr);
}

static void playback_paint(Canvas *c) {
    DeskWidget *w = c->widget; const WidgetSnapshot *d = c->data; cairo_t *cr = c->cr; PlaybackLayout p; playback_metrics(c, &p); double size = w->size;
    if (c->hidden) return;
    if (!d->has_media && desk_widget_int(w, "hide_idle")) { rounded_rect(cr, 1, 1, c->width - 2, c->height - 2, w->radius); double dash = 6; cairo_set_dash(cr, &dash, 1, 0); cairo_set_line_width(cr, 1); source_color(c, w->secondary, 1.0); cairo_stroke(cr); cairo_set_dash(cr, NULL, 0, 0); }
    double inner_height = c->height - p.padding * 2, x = p.padding, y;
    if (p.art) { if (p.vertical) playback_art(c, p.padding + (p.text_width - p.art) / 2, p.padding, p.art); else playback_art(c, p.padding, p.padding + (inner_height - p.art) / 2, p.art); }
    if (p.vertical) y = p.padding + p.art + p.gap; else { x += p.art + p.gap; y = p.padding + (inner_height - p.block) / 2; }
    double width = p.text_width;
    use_font(c, size, w->bold); char *title = fit_text(c, d->has_media ? d->title : desk_widget_get(w, "idle_text"), width); draw_text(c, x, y + size * 1.0, title, NULL); g_free(title); y += size * 1.35;
    use_font(c, size * 0.78, FALSE); char *artist = fit_text(c, d->has_media ? d->artist : "", width); draw_text(c, x, y + size * 0.85, artist, w->secondary); g_free(artist); y += size * 1.15;
    if (desk_widget_int(w, "album")) { use_font(c, size * 0.7, FALSE); char *album = fit_text(c, d->has_media ? d->album : "", width); draw_text(c, x, y + size * 0.8, album, w->secondary); g_free(album); y += size * 1.05; }
    double fraction = d->has_media && d->length_us > 0 ? (double)d->position_us / d->length_us : 0;
    if (desk_widget_int(w, "progress")) { y += size * 0.25; stats_bar(c, x, y, width, MAX(2.0, size * 0.22), fraction); y += size * 0.6; }
    gboolean status = desk_widget_int(w, "status"), times = desk_widget_int(w, "times");
    if (status || times) {
        double baseline = y + size * 0.85, icon = size * 0.55, tx = x;
        if (status) {
            source_color(c, w->accent, 1.0);
            if (d->has_media && d->playing) { cairo_move_to(cr, x, baseline - icon); cairo_line_to(cr, x + icon * 0.9, baseline - icon / 2); cairo_line_to(cr, x, baseline); cairo_close_path(cr); }
            else { cairo_rectangle(cr, x, baseline - icon, icon * 0.32, icon); cairo_rectangle(cr, x + icon * 0.58, baseline - icon, icon * 0.32, icon); }
            cairo_fill(cr); tx += icon + size * 0.45;
        }
        if (times && d->has_media) {
            use_font(c, size * 0.68, FALSE); char *elapsed = format_time(d->position_us); draw_text(c, tx, baseline, elapsed, w->secondary); g_free(elapsed);
            if (d->length_us > 0) { char *total = format_time(d->length_us); draw_text(c, x + width - text_advance(c, total), baseline, total, w->secondary); g_free(total); }
        }
    }
}

/* Terminal: shows the latest output of a shell command the feed reruns every few seconds. */

typedef struct { double cell, line, ascent, titlebar, padding; int rows; } TerminalMetrics;

static void terminal_metrics(Canvas *c, TerminalMetrics *m) {
    DeskWidget *w = c->widget; use_font(c, w->size, w->bold); cairo_text_extents_t ext; cairo_text_extents(c->cr, "M", &ext); cairo_font_extents_t font; cairo_font_extents(c->cr, &font);
    m->cell = ext.x_advance + w->spacing; m->line = ceil(MAX(font.height, font.ascent + font.descent) * 1.05); m->ascent = font.ascent; m->titlebar = desk_widget_int(w, "titlebar") ? w->size * 2.1 : 0; m->padding = w->size * 0.9;
    m->rows = desk_widget_int(w, "lines") + (desk_widget_int(w, "prompt") ? 1 : 0) + (desk_widget_int(w, "cursor") ? 1 : 0);
}

static void terminal_layout(Canvas *c) {
    TerminalMetrics m; terminal_metrics(c, &m);
    c->width = MAX(c->widget->width, (int)ceil(m.padding * 2 + desk_widget_int(c->widget, "columns") * m.cell)); c->height = (int)ceil(m.titlebar + m.padding * 2 + m.rows * m.line);
}

static char *clip_columns(const char *text, int columns) { return g_utf8_strlen(text, -1) > columns ? g_utf8_substring(text, 0, columns) : g_strdup(text); }

/* Draws "user@host:~$ " and returns the x where the command starts. */
static double terminal_prompt(Canvas *c, double x, double y, const TerminalMetrics *m) {
    char *who = g_strdup_printf("%s@%s", g_get_user_name(), g_get_host_name()); draw_text(c, x, y, who, c->widget->accent); x += character_count(who) * m->cell;
    draw_text(c, x, y, ":~$ ", c->widget->secondary); g_free(who); return x + 4 * m->cell;
}

static void terminal_paint(Canvas *c) {
    DeskWidget *w = c->widget; cairo_t *cr = c->cr; TerminalMetrics m; terminal_metrics(c, &m); int columns = desk_widget_int(w, "columns"), lines = desk_widget_int(w, "lines"); const char *command = desk_widget_get(w, "command");
    if (m.titlebar > 0) {
        static const float lights[3][4] = { { 0.98f, 0.37f, 0.34f, 0.9f }, { 0.99f, 0.74f, 0.18f, 0.9f }, { 0.16f, 0.78f, 0.25f, 0.9f } }; double r = w->size * 0.3;
        for (int i = 0; i < 3; i++) { cairo_arc(cr, m.padding + r + i * r * 3.2, m.titlebar / 2, r, 0, 2 * G_PI); source_color(c, lights[i], 1.0); cairo_fill(cr); }
        use_font(c, w->size * 0.9, FALSE); char *title = fit_text(c, desk_widget_get(w, "title"), c->width - m.padding * 2 - r * 10); cairo_font_extents_t font; cairo_font_extents(cr, &font);
        draw_text(c, (c->width - text_advance(c, title)) / 2, (m.titlebar + font.ascent - font.descent) / 2, title, w->secondary); g_free(title);
        cairo_rectangle(cr, 0, m.titlebar - 1, c->width, 1); source_color(c, w->secondary, 0.4); cairo_fill(cr); use_font(c, w->size, w->bold);
    }
    double x = m.padding, y = m.titlebar + m.padding + m.ascent;
    if (desk_widget_int(w, "prompt")) { double start = terminal_prompt(c, x, y, &m); char *clipped = clip_columns(command, MAX(0, columns - (int)round((start - x) / m.cell))); draw_text(c, start, y, clipped, NULL); g_free(clipped); y += m.line; }
    const char *output = c->data->outputs ? g_hash_table_lookup(c->data->outputs, command) : NULL; char **rows = g_strsplit(output ? output : "…", "\n", -1); int count = g_strv_length(rows);
    while (count > 0 && !rows[count - 1][0]) count--;
    for (int i = MAX(0, count - lines); i < count; i++) { char *clipped = clip_columns(rows[i], columns); draw_text(c, x, y, clipped, NULL); g_free(clipped); y += m.line; }
    g_strfreev(rows);
    if (desk_widget_int(w, "cursor")) {
        double cursor_x = desk_widget_int(w, "prompt") ? terminal_prompt(c, x, y, &m) : x;
        if ((g_get_real_time() / 500000) % 2 == 0) { cairo_rectangle(cr, cursor_x, y - m.ascent, MAX(1.0, m.cell - 1), m.ascent + 2); source_color(c, w->accent, 1.0); cairo_fill(cr); }
    }
}

typedef struct { const char *kind; void (*layout)(Canvas *); void (*paint)(Canvas *); } Painter;
static const Painter painters[] = {
    { "time", text_clock_layout, text_clock_paint }, { "date", text_clock_layout, text_clock_paint }, { "digital", digital_layout, digital_paint }, { "analog", analog_layout, analog_paint },
    { "stats", stats_layout, stats_paint }, { "eq", eq_layout, eq_paint }, { "playback", playback_layout, playback_paint }, { "terminal", terminal_layout, terminal_paint },
};

static const Painter *find_painter(const char *kind) {
    for (guint i = 0; i < G_N_ELEMENTS(painters); i++) if (!strcmp(painters[i].kind, kind)) return &painters[i];
    return &painters[0];
}

static const WidgetSnapshot empty_snapshot;

void desk_widget_measure(DeskWidget *widget, const WidgetSnapshot *data, int *width, int *height) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *cr = cairo_create(surface);
    Canvas canvas = { cr, widget, data ? data : &empty_snapshot, 0, 0, FALSE }; find_painter(widget->kind)->layout(&canvas); double scale = widget->scale / 100.0;
    *width = MAX(1, (int)ceil(canvas.width * scale * widget->stretch_x / 100.0)); *height = MAX(1, (int)ceil(canvas.height * scale * widget->stretch_y / 100.0));
    cairo_destroy(cr); cairo_surface_destroy(surface);
}

void desk_widget_draw(cairo_t *cr, DeskWidget *widget, const WidgetSnapshot *data) {
    const Painter *painter = find_painter(widget->kind); Canvas canvas = { cr, widget, data ? data : &empty_snapshot, 0, 0, FALSE }; double scale = widget->scale / 100.0;
    cairo_save(cr); cairo_scale(cr, scale * widget->stretch_x / 100.0, scale * widget->stretch_y / 100.0); painter->layout(&canvas);
    if (!canvas.hidden) {
        if (widget->panel && strcmp(widget->kind, "analog")) { rounded_rect(cr, 0, 0, canvas.width, canvas.height, widget->radius); source_color(&canvas, widget->panel_color, 1.0); cairo_fill(cr); }
        painter->paint(&canvas);
    }
    cairo_restore(cr);
}
