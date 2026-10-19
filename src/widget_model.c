#include "app.h"

#include <stdlib.h>
#include <string.h>

static const WidgetKind widget_kinds[] = {
    { "time", "Time" }, { "date", "Date" }, { "digital", "Digital clock" }, { "analog", "Analog clock" },
    { "stats", "PC performance" }, { "eq", "Audio EQ" }, { "playback", "Now playing" },
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
};

const WidgetKind *desk_widget_kinds(guint *count) { *count = G_N_ELEMENTS(widget_kinds); return widget_kinds; }
const WidgetOption *desk_widget_options(guint *count) { *count = G_N_ELEMENTS(widget_options); return widget_options; }

const char *desk_widget_kind_label(const char *kind) {
    for (guint i = 0; i < G_N_ELEMENTS(widget_kinds); i++) if (!strcmp(widget_kinds[i].kind, kind)) return widget_kinds[i].label;
    return kind;
}

gboolean desk_widget_kind_known(const char *kind) {
    for (guint i = 0; i < G_N_ELEMENTS(widget_kinds); i++) if (!strcmp(widget_kinds[i].kind, kind)) return TRUE;
    return FALSE;
}

const WidgetOption *desk_widget_option(const char *kind, const char *key) {
    for (guint i = 0; i < G_N_ELEMENTS(widget_options); i++) if (!strcmp(widget_options[i].kind, kind) && !strcmp(widget_options[i].key, key)) return &widget_options[i];
    return NULL;
}

static void set_rgba(float *color, float r, float g, float b, float a) { color[0] = r; color[1] = g; color[2] = b; color[3] = a; }

DeskWidget *desk_widget_new(const char *kind) {
    DeskWidget *widget = g_new0(DeskWidget, 1); g_strlcpy(widget->kind, kind, sizeof(widget->kind));
    widget->options = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free); widget->font = g_strdup("Sans"); widget->x = widget->y = 0.05;
    widget->size = !strcmp(kind, "time") ? 48 : !strcmp(kind, "date") ? 26 : !strcmp(kind, "digital") ? 56 : !strcmp(kind, "playback") ? 18 : !strcmp(kind, "stats") ? 15 : 16;
    widget->bold = strcmp(kind, "stats"); widget->stretch_x = widget->stretch_y = widget->scale = 100; widget->radius = 12;
    widget->panel = !strcmp(kind, "playback") || !strcmp(kind, "analog");
    set_rgba(widget->text, 0.90f, 0.96f, 0.98f, 1.0f); set_rgba(widget->gradient_color, 0.45f, 0.80f, 1.0f, 1.0f); set_rgba(widget->accent, 0.45f, 0.80f, 1.0f, 1.0f);
    set_rgba(widget->secondary, 0.66f, 0.88f, 0.94f, 0.55f); set_rgba(widget->panel_color, 0.01f, 0.03f, 0.05f, 0.55f);
    return widget;
}

void desk_widget_free(DeskWidget *widget) {
    if (!widget) return;
    g_free(widget->font); g_hash_table_destroy(widget->options); g_free(widget);
}

const char *desk_widget_get(const DeskWidget *widget, const char *key) {
    const char *value = g_hash_table_lookup(widget->options, key); if (value) return value;
    const WidgetOption *option = desk_widget_option(widget->kind, key); return option && option->fallback ? option->fallback : "";
}

int desk_widget_int(const DeskWidget *widget, const char *key) {
    const WidgetOption *option = desk_widget_option(widget->kind, key); int value = atoi(desk_widget_get(widget, key));
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
    return 1000;
}
