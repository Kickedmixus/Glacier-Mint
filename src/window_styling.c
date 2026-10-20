#include "app.h"

#include <glib/gstdio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_EXTENSION_UUID "glacier-mint-window-styling@glacier-mint"
#define CINNAMON_EXTENSION_UUID "glacier-mint-window-styling@glacier-mint"

static const char extension_metadata[] =
    "{\n"
    "  \"uuid\": \"glacier-mint-window-styling@glacier-mint\",\n"
    "  \"name\": \"Glacier Mint Window Styling\",\n"
    "  \"description\": \"Applies Glacier Mint window border styling.\",\n"
    "  \"version\": 1,\n"
    "  \"shell-version\": [\"40\", \"41\", \"42\", \"43\", \"44\", \"45\", \"46\", \"47\", \"48\", \"49\", \"50\", \"51\"]\n"
    "}\n";

static const char legacy_extension_metadata[] =
    "{\n"
    "  \"uuid\": \"glacier-mint-window-styling@glacier-mint\",\n"
    "  \"name\": \"Glacier Mint Window Styling\",\n"
    "  \"description\": \"Applies Glacier Mint window border styling.\",\n"
    "  \"version\": 1,\n"
    "  \"shell-version\": [\"40\", \"41\", \"42\", \"43\", \"44\", \"45\", \"46\", \"47\", \"48\", \"49\", \"50\", \"51\"]\n"
    "}\n";

/* The extension deliberately reads Glacier Mint's ordinary user data file. That
   keeps the GTK app as the editor and lets the shell side update without a
   GSettings schema or a second set of controls. */
static const char extension_source[] =
    "import GLib from 'gi://GLib';\n"
    "import Cairo from 'gi://cairo';\n"
    "import Meta from 'gi://Meta';\n"
    "import St from 'gi://St';\n"
    "import * as Main from 'resource:///org/gnome/shell/ui/main.js';\n"
    "import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';\n"
    "\n"
    "const UUID = 'glacier-mint-window-styling@glacier-mint';\n"
    "const NORMAL_TYPES = [Meta.WindowType.NORMAL, Meta.WindowType.DIALOG, Meta.WindowType.MODAL_DIALOG];\n"
    "const GENERATED_SETTINGS = { enabled: %s, width: %d, radius: %d, opacity: %.9g, gradient: %s, angle: %d, red: %.9g, green: %.9g, blue: %.9g, gradientRed: %.9g, gradientGreen: %.9g, gradientBlue: %.9g };\n"
    "\n"
    "export default class GlacierMintWindowStylingExtension extends Extension {\n"
    "    enable() {\n"
    "        this._windows = new Map();\n"
    "        this._settingsPath = GLib.build_filenamev([GLib.get_user_data_dir(), 'glacier-mint', 'window-styling.ini']);\n"
    "        this._settings = {...GENERATED_SETTINGS};\n"
    "        this._loadSettings();\n"
    "        this._createdId = global.display.connect('window-created', (_display, window) => {\n"
    "            GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => { this._attachWindow(window.get_compositor_private()); return GLib.SOURCE_REMOVE; });\n"
    "        });\n"
    "        this._pollId = GLib.timeout_add_seconds(GLib.PRIORITY_DEFAULT, 1, () => { this._loadSettings(); return GLib.SOURCE_CONTINUE; });\n"
    "        global.get_window_actors().forEach(actor => this._attachWindow(actor));\n"
    "    }\n"
    "\n"
    "    disable() {\n"
    "        if (this._createdId) { global.display.disconnect(this._createdId); this._createdId = 0; }\n"
    "        if (this._pollId) { GLib.source_remove(this._pollId); this._pollId = 0; }\n"
    "        for (const actor of this._windows.keys()) this._detachWindow(actor);\n"
        "        this._windows.clear();\n"
    "    }\n"
    "\n"
    "    _loadSettings() {\n"
    "        const keyFile = new GLib.KeyFile(); try { keyFile.load_from_file(this._settingsPath, GLib.KeyFileFlags.NONE); } catch (_error) { return; }\n"
    "        const get = (key, fallback) => { try { return keyFile.get_string('window-styling', key); } catch (_error) { return fallback; } }; const number = (key, fallback) => Number(get(key, String(fallback))); const old = JSON.stringify(this._settings);\n"
    "        this._settings = { enabled: get('enabled', GENERATED_SETTINGS.enabled ? 'true' : 'false') === 'true', width: number('border_width', GENERATED_SETTINGS.width), radius: number('border_radius', GENERATED_SETTINGS.radius), opacity: number('border_opacity', GENERATED_SETTINGS.opacity * 100) / 100, gradient: get('gradient', GENERATED_SETTINGS.gradient ? 'true' : 'false') === 'true', angle: number('gradient_angle', GENERATED_SETTINGS.angle), red: number('border_red', GENERATED_SETTINGS.red), green: number('border_green', GENERATED_SETTINGS.green), blue: number('border_blue', GENERATED_SETTINGS.blue), gradientRed: number('gradient_red', GENERATED_SETTINGS.gradientRed), gradientGreen: number('gradient_green', GENERATED_SETTINGS.gradientGreen), gradientBlue: number('gradient_blue', GENERATED_SETTINGS.gradientBlue) };\n"
    "        if (old !== JSON.stringify(this._settings)) this._applyAll();\n"
    "    }\n"
    "\n"
    "    _metaWindow(actor) { try { if (!actor || actor.destroyed) return null; return actor.get_meta_window() || null; } catch (_error) { return null; } }\n"
    "\n"
    "    _attachWindow(actor) {\n"
    "        if (!actor || this._windows.has(actor)) return;\n"
    "        const window = this._metaWindow(actor);\n"
    "        if (!window || !NORMAL_TYPES.includes(window.get_window_type())) return;\n"
    "        const border = new St.DrawingArea({reactive: false, can_focus: false});\n"
    "        border.connect('repaint', area => this._paint(area));\n"
    "        Main.layoutManager.addChrome(border, {trackFullscreen: true});\n"
    "        const entry = {border, window, sizeId: actor.connect('notify::size', () => this._size(entry, actor)), positionId: actor.connect('notify::position', () => this._size(entry, actor)), frameSizeId: window.connect('size-changed', () => this._size(entry, actor)), framePositionId: window.connect('position-changed', () => this._size(entry, actor)), destroyId: actor.connect('destroy', () => this._detachWindow(actor))};\n"
    "        this._windows.set(actor, entry);\n"
    "        this._size(entry, actor);\n"
    "        this._refreshBorder(entry);\n"
    "    }\n"
    "\n"
    "    _size(entry, actor) { if (!entry || !entry.window || !actor || actor.destroyed || !actor.get_stage()) return; const width = Math.max(0, this._settings.width); const [x, y] = actor.get_transformed_position(); const [windowWidth, windowHeight] = actor.get_transformed_size(); entry.border.set_position(Math.round(x - width), Math.round(y - width)); entry.border.set_size(Math.max(1, Math.round(windowWidth + width * 2)), Math.max(1, Math.round(windowHeight + width * 2))); }\n"
    "    _paint(area) {\n"
    "        const s = this._settings; const cr = area.get_context(); const [width, height] = area.get_surface_size(); cr.setOperator(Cairo.Operator.CLEAR); cr.paint(); cr.setOperator(Cairo.Operator.OVER); const line = Math.max(0, s.width); if (!s.enabled || line <= 0) { cr.$dispose(); return; }\n"
    "        const radius = s.radius > 0 ? s.radius + line / 2 : 0; const x = line / 2; const y = line / 2; const w = Math.max(1, width - line); const h = Math.max(1, height - line); const r = Math.min(radius, Math.min(w, h) / 2); if (r <= 0) cr.rectangle(x, y, w, h); else { cr.newSubPath(); cr.arc(x + w - r, y + r, r, -Math.PI / 2, 0); cr.arc(x + w - r, y + h - r, r, 0, Math.PI / 2); cr.arc(x + r, y + h - r, r, Math.PI / 2, Math.PI); cr.arc(x + r, y + r, r, Math.PI, 3 * Math.PI / 2); cr.closePath(); }\n"
    "        let paint = null; if (s.gradient) { const angle = s.angle * Math.PI / 180; const dx = Math.cos(angle) * width / 2; const dy = Math.sin(angle) * height / 2; paint = new Cairo.LinearGradient(width / 2 - dx, height / 2 - dy, width / 2 + dx, height / 2 + dy); paint.addColorStopRGBA(0, s.red, s.green, s.blue, s.opacity); paint.addColorStopRGBA(1, s.gradientRed, s.gradientGreen, s.gradientBlue, s.opacity); cr.setSource(paint); } else cr.setSourceRGBA(s.red, s.green, s.blue, s.opacity); cr.setLineWidth(line); cr.setLineJoin(Cairo.LineJoin.MITER); cr.setLineCap(Cairo.LineCap.BUTT); cr.stroke(); if (paint) paint.$dispose(); cr.$dispose();\n"
    "    }\n"
    "    _refreshBorder(entry) { entry.border.visible = this._settings.enabled; entry.border.queue_repaint(); }\n"
    "    _applyAll() { for (const [actor, entry] of this._windows) { this._size(entry, actor); this._refreshBorder(entry); } }\n"
    "    _detachWindow(actor) {\n"
    "        const entry = this._windows.get(actor);\n"
    "        if (!entry) return;\n"
    "        if (entry.sizeId) actor.disconnect(entry.sizeId);\n"
    "        if (entry.positionId) actor.disconnect(entry.positionId);\n"
    "        if (entry.frameSizeId && entry.window) entry.window.disconnect(entry.frameSizeId);\n"
    "        if (entry.framePositionId && entry.window) entry.window.disconnect(entry.framePositionId);\n"
    "        if (entry.border && !entry.border.destroyed) Main.layoutManager.removeChrome(entry.border);\n"
    "        if (entry.destroyId && !actor.destroyed) actor.disconnect(entry.destroyId);\n"
    "        if (entry.border && !entry.border.destroyed) entry.border.destroy();\n"
    "        this._windows.delete(actor);\n"
    "    }\n"
    "}\n";

static const char cinnamon_metadata[] =
    "{\n"
    "  \"uuid\": \"glacier-mint-window-styling@glacier-mint\",\n"
    "  \"name\": \"Glacier Mint Window Styling\",\n"
    "  \"description\": \"Applies Glacier Mint window border styling.\",\n"
    "  \"version\": 1\n"
    "}\n";

static const char cinnamon_source[] =
    "const GLib = imports.gi.GLib;\n"
    "const Cairo = imports.cairo;\n"
    "const Meta = imports.gi.Meta;\n"
    "const St = imports.gi.St;\n"
    "const Mainloop = imports.mainloop;\n"
    "const TYPES = [Meta.WindowType.NORMAL, Meta.WindowType.DIALOG, Meta.WindowType.MODAL_DIALOG];\n"
    "const GENERATED_SETTINGS = { enabled: %s, width: %d, radius: %d, opacity: %.9g, gradient: %s, angle: %d, red: %.9g, green: %.9g, blue: %.9g, gradientRed: %.9g, gradientGreen: %.9g, gradientBlue: %.9g };\n"
    "\n"
    "function Extension() {}\n"
    "Extension.prototype = {\n"
    "    enable() {\n"
    "        this._windows = new Map(); this._settingsPath = GLib.build_filenamev([GLib.get_user_data_dir(), 'glacier-mint', 'window-styling.ini']); this._settings = {...GENERATED_SETTINGS}; this._load();\n"
    "        this._createdId = global.display.connect('window-created', (_display, window) => Mainloop.idle_add(() => { this._attach(window.get_compositor_private()); return false; }));\n"
    "        this._pollId = Mainloop.timeout_add_seconds(1, () => { this._load(); return true; });\n"
    "        global.get_window_actors().forEach(actor => this._attach(actor));\n"
    "    },\n"
    "    disable() {\n"
    "        if (this._createdId) global.display.disconnect(this._createdId); if (this._pollId) Mainloop.source_remove(this._pollId);\n"
    "        for (let actor of this._windows.keys()) this._detach(actor); this._windows.clear();\n"
    "    },\n"
    "    _load() { let file = new GLib.KeyFile(); try { file.load_from_file(this._settingsPath, GLib.KeyFileFlags.NONE); } catch (_e) { return; } let get = (key, fallback) => { try { return file.get_string('window-styling', key); } catch (_e) { return fallback; } }; let n = (key, fallback) => Number(get(key, String(fallback))); let old = JSON.stringify(this._settings); this._settings = {enabled: get('enabled', GENERATED_SETTINGS.enabled ? 'true' : 'false') === 'true', width: n('border_width', GENERATED_SETTINGS.width), radius: n('border_radius', GENERATED_SETTINGS.radius), opacity: n('border_opacity', GENERATED_SETTINGS.opacity * 100) / 100, gradient: get('gradient', GENERATED_SETTINGS.gradient ? 'true' : 'false') === 'true', angle: n('gradient_angle', GENERATED_SETTINGS.angle), red: n('border_red', GENERATED_SETTINGS.red), green: n('border_green', GENERATED_SETTINGS.green), blue: n('border_blue', GENERATED_SETTINGS.blue), gradientRed: n('gradient_red', GENERATED_SETTINGS.gradientRed), gradientGreen: n('gradient_green', GENERATED_SETTINGS.gradientGreen), gradientBlue: n('gradient_blue', GENERATED_SETTINGS.gradientBlue)}; if (old !== JSON.stringify(this._settings)) this._apply(); },\n"
    "    _metaWindow(actor) { try { if (!actor || actor.destroyed) return null; return actor.get_meta_window() || null; } catch (_e) { return null; } },\n"
    "    _attach(actor) {\n"
    "        if (!actor || this._windows.has(actor)) return; let window = this._metaWindow(actor); if (!window || TYPES.indexOf(window.get_window_type()) < 0) return;\n"
    "        let border = new St.DrawingArea({reactive: false, can_focus: false}); border.connect('repaint', area => this._paint(area)); actor.add_child(border); let entry = {border: border, sizeId: actor.connect('notify::size', () => this._size(entry, actor)), destroyId: actor.connect('destroy', () => this._detach(actor))}; this._windows.set(actor, entry); this._size(entry, actor); this._refreshBorder(entry);\n"
    "    },\n"
    "    _size(entry, actor) { let width = Math.max(0, this._settings.width); entry.border.set_position(-width, -width); entry.border.set_size(Math.max(1, actor.width + width * 2), Math.max(1, actor.height + width * 2)); },\n"
    "    _paint(area) { let s = this._settings; let cr = area.get_context(); let size = area.get_surface_size(); let width = size[0], height = size[1]; cr.setOperator(Cairo.Operator.CLEAR); cr.paint(); cr.setOperator(Cairo.Operator.OVER); let line = Math.max(0, s.width); if (!s.enabled || line <= 0) { cr.$dispose(); return; } let radius = s.radius > 0 ? s.radius + line / 2 : 0, x = line / 2, y = line / 2, w = Math.max(1, width - line), h = Math.max(1, height - line), r = Math.min(radius, Math.min(w, h) / 2); if (r <= 0) cr.rectangle(x, y, w, h); else { cr.newSubPath(); cr.arc(x + w - r, y + r, r, -Math.PI / 2, 0); cr.arc(x + w - r, y + h - r, r, 0, Math.PI / 2); cr.arc(x + r, y + h - r, r, Math.PI / 2, Math.PI); cr.arc(x + r, y + r, r, Math.PI, 3 * Math.PI / 2); cr.closePath(); } let paint = null; if (s.gradient) { let angle = s.angle * Math.PI / 180, dx = Math.cos(angle) * width / 2, dy = Math.sin(angle) * height / 2; paint = new Cairo.LinearGradient(width / 2 - dx, height / 2 - dy, width / 2 + dx, height / 2 + dy); paint.addColorStopRGBA(0, s.red, s.green, s.blue, s.opacity); paint.addColorStopRGBA(1, s.gradientRed, s.gradientGreen, s.gradientBlue, s.opacity); cr.setSource(paint); } else cr.setSourceRGBA(s.red, s.green, s.blue, s.opacity); cr.setLineWidth(line); cr.setLineJoin(Cairo.LineJoin.MITER); cr.setLineCap(Cairo.LineCap.BUTT); cr.stroke(); if (paint) paint.$dispose(); cr.$dispose(); },\n"
    "    _refreshBorder(entry) { entry.border.visible = this._settings.enabled; entry.border.queue_repaint(); },\n"
    "    _apply() { for (let [actor, entry] of this._windows) { this._size(entry, actor); this._refreshBorder(entry); } },\n"
    "    _detach(actor) { let entry = this._windows.get(actor); if (!entry) return; if (entry.sizeId) actor.disconnect(entry.sizeId); if (entry.border) entry.border.destroy(); this._windows.delete(actor); }\n"
    "};\n"
    "function init() { return new Extension(); }\n";

static char *window_styling_path(void) { return data_path("window-styling.ini"); }

static char *extension_dir(void) {
    /* gnome-extensions uses GLib's user data directory.  This matters when
       XDG_DATA_HOME is set: ~/.local/share is then not the directory GNOME
       scans, even though it is the usual default. */
    return g_build_filename(g_get_user_data_dir(), "gnome-shell", "extensions", WINDOW_EXTENSION_UUID, NULL);
}

static char *cinnamon_extension_dir(void) {
    return g_build_filename(g_get_user_data_dir(), "cinnamon", "extensions", CINNAMON_EXTENSION_UUID, NULL);
}

static int gnome_shell_major_version(void) {
    char *program = g_find_program_in_path("gnome-shell"); const char *argument = "--version";
    if (!program) { program = g_find_program_in_path("gnome-extensions"); argument = "version"; }
    if (!program) return 0;
    char *argv[] = {program, (char *)argument, NULL}; gchar *output = NULL, *error_output = NULL; gint status = -1; GError *error = NULL; int version = 0;
    if (g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &output, &error_output, &status, &error) && output) {
        sscanf(output, "GNOME Shell %d", &version);
        if (!version) { const char *digit = output; while (*digit && !g_ascii_isdigit(*digit)) digit++; if (*digit) version = atoi(digit); }
    }
    g_free(output); g_free(error_output); g_clear_error(&error); g_free(program); return version;
}

static gboolean key_bool(GKeyFile *file, const char *key, gboolean fallback) {
    GError *error = NULL; gboolean value = g_key_file_get_boolean(file, "window-styling", key, &error);
    if (error) { g_error_free(error); return fallback; }
    return value;
}

static double key_double(GKeyFile *file, const char *key, double fallback) {
    GError *error = NULL; double value = g_key_file_get_double(file, "window-styling", key, &error);
    if (error) { g_error_free(error); return fallback; }
    return value;
}

static void window_styling_values(App *app, gboolean *enabled, int *width, int *radius, int *opacity, gboolean *gradient, int *angle, GdkRGBA *color, GdkRGBA *gradient_color) {
    *enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->window_style_enabled));
    *width = (int)lround(gtk_range_get_value(GTK_RANGE(app->window_style_border_width)));
    *radius = (int)lround(gtk_range_get_value(GTK_RANGE(app->window_style_radius)));
    *opacity = (int)lround(gtk_range_get_value(GTK_RANGE(app->window_style_opacity)));
    *gradient = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->window_style_gradient));
    *angle = (int)lround(gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->window_style_gradient_angle)));
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(app->window_style_color), color);
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(app->window_style_gradient_color), gradient_color);
}

void load_window_styling(App *app) {
    GKeyFile *file = g_key_file_new(); char *path = window_styling_path();
    gboolean enabled = TRUE, gradient = FALSE; int width = 2, radius = 8, opacity = 100, angle = 0; GdkRGBA color = {0.66, 0.88, 0.94, 1.0}, gradient_color = {0.35, 0.65, 1.0, 1.0};
    if (g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
        enabled = key_bool(file, "enabled", enabled); width = (int)key_double(file, "border_width", width); radius = (int)key_double(file, "border_radius", radius); opacity = (int)key_double(file, "border_opacity", opacity); gradient = key_bool(file, "gradient", gradient); angle = (int)key_double(file, "gradient_angle", angle);
        color.red = key_double(file, "border_red", color.red); color.green = key_double(file, "border_green", color.green); color.blue = key_double(file, "border_blue", color.blue);
        gradient_color.red = key_double(file, "gradient_red", gradient_color.red); gradient_color.green = key_double(file, "gradient_green", gradient_color.green); gradient_color.blue = key_double(file, "gradient_blue", gradient_color.blue);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->window_style_enabled), enabled); gtk_range_set_value(GTK_RANGE(app->window_style_border_width), CLAMP(width, 0, 16)); gtk_range_set_value(GTK_RANGE(app->window_style_radius), CLAMP(radius, 0, 48)); gtk_range_set_value(GTK_RANGE(app->window_style_opacity), CLAMP(opacity, 0, 100)); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->window_style_gradient), gradient); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->window_style_gradient_angle), CLAMP(angle, 0, 360)); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(app->window_style_color), &color); gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(app->window_style_gradient_color), &gradient_color);
    g_key_file_free(file); g_free(path); gtk_widget_queue_draw(app->window_style_preview);
}

void window_styling_changed(GtkWidget *control, App *app) {
    (void)control; gboolean enabled, gradient; int width, radius, opacity, angle; GdkRGBA color, gradient_color; window_styling_values(app, &enabled, &width, &radius, &opacity, &gradient, &angle, &color, &gradient_color);
    GKeyFile *file = g_key_file_new(); g_key_file_set_boolean(file, "window-styling", "enabled", enabled); g_key_file_set_integer(file, "window-styling", "border_width", width); g_key_file_set_integer(file, "window-styling", "border_radius", radius); g_key_file_set_integer(file, "window-styling", "border_opacity", opacity); g_key_file_set_boolean(file, "window-styling", "gradient", gradient); g_key_file_set_integer(file, "window-styling", "gradient_angle", angle); g_key_file_set_double(file, "window-styling", "border_red", color.red); g_key_file_set_double(file, "window-styling", "border_green", color.green); g_key_file_set_double(file, "window-styling", "border_blue", color.blue); g_key_file_set_double(file, "window-styling", "gradient_red", gradient_color.red); g_key_file_set_double(file, "window-styling", "gradient_green", gradient_color.green); g_key_file_set_double(file, "window-styling", "gradient_blue", gradient_color.blue);
    char *data = g_key_file_to_data(file, NULL, NULL), *path = window_styling_path(); g_file_set_contents(path, data, -1, NULL); g_free(path); g_free(data); g_key_file_free(file); gtk_widget_queue_draw(app->window_style_preview);
}

static void saved_window_styling_values(gboolean *enabled, int *width, int *radius, int *opacity, gboolean *gradient, int *angle, GdkRGBA *color, GdkRGBA *gradient_color) {
    GKeyFile *file = g_key_file_new(); char *path = window_styling_path();
    *enabled = TRUE; *width = 2; *radius = 8; *opacity = 100; *gradient = FALSE; *angle = 0; color->red = 0.66; color->green = 0.88; color->blue = 0.94; gradient_color->red = 0.35; gradient_color->green = 0.65; gradient_color->blue = 1.0;
    if (g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL)) {
        *enabled = key_bool(file, "enabled", *enabled); *width = (int)key_double(file, "border_width", *width); *radius = (int)key_double(file, "border_radius", *radius); *opacity = (int)key_double(file, "border_opacity", *opacity); *gradient = key_bool(file, "gradient", *gradient); *angle = (int)key_double(file, "gradient_angle", *angle);
        color->red = key_double(file, "border_red", color->red); color->green = key_double(file, "border_green", color->green); color->blue = key_double(file, "border_blue", color->blue);
        gradient_color->red = key_double(file, "gradient_red", gradient_color->red); gradient_color->green = key_double(file, "gradient_green", gradient_color->green); gradient_color->blue = key_double(file, "gradient_blue", gradient_color->blue);
    }
    *width = CLAMP(*width, 0, 16); *radius = CLAMP(*radius, 0, 48); *opacity = CLAMP(*opacity, 0, 100); *angle = CLAMP(*angle, 0, 360); color->red = CLAMP(color->red, 0.0, 1.0); color->green = CLAMP(color->green, 0.0, 1.0); color->blue = CLAMP(color->blue, 0.0, 1.0); gradient_color->red = CLAMP(gradient_color->red, 0.0, 1.0); gradient_color->green = CLAMP(gradient_color->green, 0.0, 1.0); gradient_color->blue = CLAMP(gradient_color->blue, 0.0, 1.0);
    g_key_file_free(file); g_free(path);
}

static gboolean run_extension_command(const char *command) {
    char *program = g_find_program_in_path("gnome-extensions");
    if (!program) return FALSE;
    char *argv[] = { program, (char *)command, (char *)WINDOW_EXTENSION_UUID, NULL }; gchar *standard_output = NULL, *standard_error = NULL; gint status = -1; GError *error = NULL;
    gboolean spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &standard_output, &standard_error, &status, &error);
    gboolean ok = spawned && status == 0;
    g_free(standard_output); g_free(standard_error); g_clear_error(&error); g_free(program); return ok;
}

static gboolean enable_gnome_extension(void) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.gnome.shell", TRUE) : NULL;
    if (!schema || !g_settings_schema_has_key(schema, "enabled-extensions")) return FALSE;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL);
    GVariant *current = g_settings_get_value(settings, "enabled-extensions");
    GVariantIter iter; const char *id; gboolean found = FALSE;
    GVariantBuilder builder; g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    g_variant_iter_init(&iter, current);
    while (g_variant_iter_loop(&iter, "&s", &id)) {
        if (!strcmp(id, WINDOW_EXTENSION_UUID)) found = TRUE;
        g_variant_builder_add(&builder, "s", id);
    }
    if (!found) g_variant_builder_add(&builder, "s", WINDOW_EXTENSION_UUID);
    GVariant *updated = g_variant_ref_sink(g_variant_builder_end(&builder));
    gboolean ok = found || g_settings_set_value(settings, "enabled-extensions", updated);
    g_variant_unref(updated); g_variant_unref(current); g_object_unref(settings);
    return ok;
}

static gboolean enable_cinnamon_extension(void) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default(); GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.cinnamon", TRUE) : NULL;
    if (!schema || !g_settings_schema_has_key(schema, "enabled-extensions")) return FALSE;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL); GVariant *current = g_settings_get_value(settings, "enabled-extensions"); GVariantIter iter; const char *id; gboolean found = FALSE; GVariantBuilder builder; g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
    g_variant_iter_init(&iter, current); while (g_variant_iter_loop(&iter, "&s", &id)) { if (!strcmp(id, CINNAMON_EXTENSION_UUID)) found = TRUE; g_variant_builder_add(&builder, "s", id); }
    if (!found) g_variant_builder_add(&builder, "s", CINNAMON_EXTENSION_UUID);
    GVariant *updated = g_variant_ref_sink(g_variant_builder_end(&builder)); gboolean ok = found || g_settings_set_value(settings, "enabled-extensions", updated); g_variant_unref(updated); g_variant_unref(current); g_object_unref(settings); return ok;
}

static void refresh_cinnamon_extension_cache(void) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default(); GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.cinnamon", TRUE) : NULL;
    if (!schema || !g_settings_schema_has_key(schema, "extension-cache-updated")) return;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL); g_settings_set_int(settings, "extension-cache-updated", (gint)(g_get_real_time() / G_USEC_PER_SEC)); g_object_unref(settings);
}

static gboolean clear_extension_dir(const char *dir) {
    if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) return TRUE;
    GDir *directory = g_dir_open(dir, 0, NULL); if (!directory) return FALSE;
    const char *name; gboolean ok = TRUE;
    while ((name = g_dir_read_name(directory))) {
        char *child = g_build_filename(dir, name, NULL);
        if (g_file_test(child, G_FILE_TEST_IS_DIR)) ok = clear_extension_dir(child);
        else ok = g_remove(child) == 0;
        g_free(child); if (!ok) break;
    }
    g_dir_close(directory); if (ok) ok = g_rmdir(dir) == 0; return ok;
}

static char *generated_extension_source(const char *format, gboolean enabled, int width, int radius, int opacity, gboolean gradient, int angle, const GdkRGBA *color, const GdkRGBA *gradient_color) {
    return g_strdup_printf(format, enabled ? "true" : "false", width, radius, opacity / 100.0, gradient ? "true" : "false", angle, color->red, color->green, color->blue, gradient_color->red, gradient_color->green, gradient_color->blue);
}

void apply_window_styling_clicked(GtkButton *button, App *app) {
    (void)button; window_styling_changed(NULL, app); int status = install_window_extension_files();
    if (status == 0) gtk_label_set_text(GTK_LABEL(app->window_style_status), "Applied. Extension was wiped, regenerated from these settings, and reloaded.");
    else gtk_label_set_text(GTK_LABEL(app->window_style_status), "Could not rewrite the desktop extension; see the terminal output.");
}

gboolean gnome_window_extension_installed(void) {
    char *dir = extension_dir(), *metadata = g_build_filename(dir, "metadata.json", NULL), *source = g_build_filename(dir, "extension.js", NULL); gboolean installed = g_file_test(metadata, G_FILE_TEST_IS_REGULAR) && g_file_test(source, G_FILE_TEST_IS_REGULAR); g_free(dir); g_free(metadata); g_free(source); return installed;
}

gboolean cinnamon_window_extension_installed(void) {
    char *dir = cinnamon_extension_dir(), *metadata = g_build_filename(dir, "metadata.json", NULL), *source = g_build_filename(dir, "extension.js", NULL); gboolean installed = g_file_test(metadata, G_FILE_TEST_IS_REGULAR) && g_file_test(source, G_FILE_TEST_IS_REGULAR); g_free(dir); g_free(metadata); g_free(source); return installed;
}

int install_window_extension_files(void) {
    gboolean enabled, gradient; int width, radius, opacity, angle; GdkRGBA color, gradient_color; saved_window_styling_values(&enabled, &width, &radius, &opacity, &gradient, &angle, &color, &gradient_color);
    int shell_version = gnome_shell_major_version(); const char *gnome_metadata = shell_version > 0 && shell_version < 45 ? legacy_extension_metadata : extension_metadata; char *gnome_source = generated_extension_source(shell_version > 0 && shell_version < 45 ? cinnamon_source : extension_source, enabled, width, radius, opacity, gradient, angle, &color, &gradient_color); char *cinnamon_source_generated = generated_extension_source(cinnamon_source, enabled, width, radius, opacity, gradient, angle, &color, &gradient_color); char *dir = extension_dir(); char *cinnamon_dir = cinnamon_extension_dir(); run_extension_command("disable"); gboolean ok = clear_extension_dir(dir) && clear_extension_dir(cinnamon_dir); g_mkdir_with_parents(dir, 0700); g_mkdir_with_parents(cinnamon_dir, 0700); char *metadata = g_build_filename(dir, "metadata.json", NULL), *source = g_build_filename(dir, "extension.js", NULL), *cinnamon_metadata_path = g_build_filename(cinnamon_dir, "metadata.json", NULL), *cinnamon_source_path = g_build_filename(cinnamon_dir, "extension.js", NULL); GError *error = NULL;
    if (ok) ok = g_file_set_contents(metadata, gnome_metadata, -1, &error);
    if (ok) ok = g_file_set_contents(source, gnome_source, -1, &error);
    if (ok) ok = g_file_set_contents(cinnamon_metadata_path, cinnamon_metadata, -1, &error);
    if (ok) ok = g_file_set_contents(cinnamon_source_path, cinnamon_source_generated, -1, &error);
    if (ok) { enable_gnome_extension(); refresh_cinnamon_extension_cache(); enable_cinnamon_extension(); run_extension_command("enable"); }
    if (!ok) g_printerr("glacier-mint: extension install failed: %s\n", error ? error->message : "unknown error");
    else g_print("glacier-mint: installed GNOME extension at %s (GNOME Shell %d)\n", dir, shell_version);
    if (error) g_error_free(error);
    g_free(gnome_source); g_free(cinnamon_source_generated); g_free(metadata); g_free(source); g_free(cinnamon_metadata_path); g_free(cinnamon_source_path); g_free(dir); g_free(cinnamon_dir); return ok ? 0 : 1;
}

void install_gnome_extension_clicked(GtkButton *button, App *app) {
    (void)button; window_styling_changed(NULL, app); int status = install_window_extension_files();
    if (status == 0) { if (app->extension_status) gtk_label_set_text(GTK_LABEL(app->extension_status), "Installed for GNOME Shell and Cinnamon. Enable Glacier Mint Window Styling in your desktop's extension manager."); }
    else if (app->extension_status) gtk_label_set_text(GTK_LABEL(app->extension_status), "Could not install the extension; see the terminal output.");
}

void open_extension_manager_clicked(GtkButton *button, App *app) {
    (void)button; char *program = g_find_program_in_path("cinnamon-settings"); char *argv[3] = {NULL, NULL, NULL};
    if (program) { argv[0] = program; argv[1] = "extensions"; }
    else { g_free(program); program = g_find_program_in_path("gnome-extensions-app"); if (program) argv[0] = program; }
    if (program && g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL)) { g_free(program); return; }
    g_free(program); gtk_label_set_text(GTK_LABEL(app->extension_status), "Extension files are installed. Open your desktop's Extensions settings from the system menu.");
}

static void rounded_rectangle(cairo_t *cr, double x, double y, double width, double height, double radius) {
    if (radius <= 0) { cairo_rectangle(cr, x, y, width, height); return; } radius = MIN(radius, MIN(width, height) / 2.0); cairo_new_sub_path(cr); cairo_arc(cr, x + width - radius, y + radius, radius, -G_PI / 2, 0); cairo_arc(cr, x + width - radius, y + height - radius, radius, 0, G_PI / 2); cairo_arc(cr, x + radius, y + height - radius, radius, G_PI / 2, G_PI); cairo_arc(cr, x + radius, y + radius, radius, G_PI, 3 * G_PI / 2); cairo_close_path(cr);
}

gboolean window_style_preview_draw(GtkWidget *widget, cairo_t *cr, App *app) {
    double width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget), x = 44, y = 38, window_width = MAX(160, width - 88), window_height = MAX(120, height - 76), radius = gtk_range_get_value(GTK_RANGE(app->window_style_radius)), opacity = gtk_range_get_value(GTK_RANGE(app->window_style_opacity)) / 100.0, border = gtk_range_get_value(GTK_RANGE(app->window_style_border_width)); gboolean gradient = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->window_style_gradient)); double angle = gtk_spin_button_get_value(GTK_SPIN_BUTTON(app->window_style_gradient_angle)) * G_PI / 180.0; GdkRGBA color, gradient_color; gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(app->window_style_color), &color); gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(app->window_style_gradient_color), &gradient_color);
    cairo_set_source_rgb(cr, 0.025, 0.04, 0.05); cairo_paint(cr); cairo_set_source_rgb(cr, 0.05, 0.08, 0.10); cairo_rectangle(cr, 0, 0, width, height); cairo_fill(cr);
    rounded_rectangle(cr, x, y, window_width, window_height, radius); cairo_set_source_rgb(cr, 0.055, 0.07, 0.08); cairo_fill(cr); cairo_save(cr); rounded_rectangle(cr, x, y, window_width, window_height, radius); cairo_clip(cr); cairo_set_source_rgb(cr, 0.10, 0.14, 0.16); cairo_rectangle(cr, x, y, window_width, 38); cairo_fill(cr); cairo_set_source_rgb(cr, 0.75, 0.82, 0.84); cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD); cairo_set_font_size(cr, 14); cairo_move_to(cr, x + 18, y + 24); cairo_show_text(cr, "Example window"); cairo_set_source_rgba(cr, 0.6, 0.7, 0.73, 0.5); cairo_arc(cr, x + window_width - 24, y + 19, 4, 0, 2 * G_PI); cairo_fill(cr); cairo_restore(cr);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->window_style_enabled)) && border > 0) { double outer_radius = radius > 0 ? radius + border / 2.0 : 0; rounded_rectangle(cr, x - border / 2.0, y - border / 2.0, window_width + border, window_height + border, outer_radius); cairo_pattern_t *pattern = NULL; if (gradient) { double dx = cos(angle) * window_width / 2.0, dy = sin(angle) * window_height / 2.0; pattern = cairo_pattern_create_linear(x + window_width / 2.0 - dx, y + window_height / 2.0 - dy, x + window_width / 2.0 + dx, y + window_height / 2.0 + dy); cairo_pattern_add_color_stop_rgba(pattern, 0, color.red, color.green, color.blue, opacity); cairo_pattern_add_color_stop_rgba(pattern, 1, gradient_color.red, gradient_color.green, gradient_color.blue, opacity); cairo_set_source(cr, pattern); } else cairo_set_source_rgba(cr, color.red, color.green, color.blue, opacity); cairo_set_line_width(cr, border); cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER); cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT); cairo_stroke(cr); if (pattern) cairo_pattern_destroy(pattern); }
    return FALSE;
}
