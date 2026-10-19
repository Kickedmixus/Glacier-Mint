#include "app.h"

#include <signal.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* After a reboot the saved pid can belong to an unrelated process, so only stop it if it is one of our wallpapers. */
static gboolean is_wallpaper_process(pid_t pid) {
    char *path = g_strdup_printf("/proc/%d/cmdline", (int)pid), *cmdline = NULL; gsize length = 0; gboolean ours = FALSE;
    if (g_file_get_contents(path, &cmdline, &length, NULL) && length) {
        char *name = g_path_get_basename(cmdline); ours = !strcmp(name, "glacier-mint");
        gboolean wallpaper = FALSE; for (const char *arg = cmdline; arg < cmdline + length; arg += strlen(arg) + 1) if (!strcmp(arg, "--wallpaper") || !strcmp(arg, "--restore")) wallpaper = TRUE;
        ours = ours && wallpaper; g_free(name);
    }
    g_free(cmdline); g_free(path); return ours;
}

void stop_wallpaper(void) {
    char *pid_path = data_path("wallpaper.pid"), *contents = read_file(pid_path);
    if (contents) {
        pid_t pid = (pid_t)strtol(contents, NULL, 10);
        if (pid > 1 && is_wallpaper_process(pid)) {
            kill(pid, SIGTERM);
            /* The wallpaper owns the desktop pixmap while it runs. Do not let
               the replacement capture that temporary pixmap as its "real"
               background. */
            for (int tries = 0; tries < 50 && is_wallpaper_process(pid); tries++) g_usleep(10000);
        }
        g_free(contents);
    }
    g_remove(pid_path); g_free(pid_path);
}

/* active.ini names the wallpaper that `glacier-mint --restore` starts at login or from the wallpaper switcher. */
static void save_active_wallpaper(const char *path, int fps, gboolean vsync, const char *title, int speed, gboolean chroma, const float key[3], float smoothing) {
    GKeyFile *file = g_key_file_new(); g_key_file_set_string(file, "wallpaper", "path", path); g_key_file_set_integer(file, "wallpaper", "fps", fps); g_key_file_set_boolean(file, "wallpaper", "vsync", vsync); g_key_file_set_string(file, "wallpaper", "title", title ? title : ""); g_key_file_set_integer(file, "wallpaper", "speed_percent", speed); g_key_file_set_boolean(file, "wallpaper", "chroma_enabled", chroma); g_key_file_set_double(file, "wallpaper", "chroma_red", key[0]); g_key_file_set_double(file, "wallpaper", "chroma_green", key[1]); g_key_file_set_double(file, "wallpaper", "chroma_blue", key[2]); g_key_file_set_double(file, "wallpaper", "chroma_smoothing", smoothing);
    char *data = g_key_file_to_data(file, NULL, NULL), *active = data_path("active.ini"); g_file_set_contents(active, data, -1, NULL); g_free(active); g_free(data); g_key_file_free(file);
}

static GKeyFile *load_key_file(const char *name) {
    char *path = data_path(name); GKeyFile *file = g_key_file_new(); gboolean loaded = g_key_file_load_from_file(file, path, G_KEY_FILE_NONE, NULL); g_free(path);
    if (!loaded) { g_key_file_free(file); return NULL; }
    return file;
}

static int key_file_int(GKeyFile *file, const char *key, int fallback) { return file && g_key_file_has_key(file, "wallpaper", key, NULL) ? g_key_file_get_integer(file, "wallpaper", key, NULL) : fallback; }
static gboolean key_file_bool(GKeyFile *file, const char *key, gboolean fallback) { return file && g_key_file_has_key(file, "wallpaper", key, NULL) ? g_key_file_get_boolean(file, "wallpaper", key, NULL) : fallback; }
static double key_file_double(GKeyFile *file, const char *key, double fallback) { return file && g_key_file_has_key(file, "wallpaper", key, NULL) ? g_key_file_get_double(file, "wallpaper", key, NULL) : fallback; }

static void load_active_effects(App *app) {
    GKeyFile *file = load_key_file("active.ini"); if (!file) return;
    if (app->speed) gtk_range_set_value(GTK_RANGE(app->speed), CLAMP(key_file_int(file, "speed_percent", 100), 0, 200));
    if (app->chroma_enable) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chroma_enable), key_file_bool(file, "chroma_enabled", FALSE));
    if (app->chroma_smoothing) gtk_range_set_value(GTK_RANGE(app->chroma_smoothing), CLAMP(key_file_double(file, "chroma_smoothing", 0.06), 0.005, 0.30));
    if (app->chroma_color) { GdkRGBA color = { key_file_double(file, "chroma_red", 0.0), key_file_double(file, "chroma_green", 1.0), key_file_double(file, "chroma_blue", 0.0), 1.0 }; gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(app->chroma_color), &color); }
    g_key_file_free(file);
}

int restore_wallpaper(void) {
    GKeyFile *file = load_key_file("active.ini"); char *path = file ? g_key_file_get_string(file, "wallpaper", "path", NULL) : NULL; int fps = key_file_int(file, "fps", 30), speed = CLAMP(key_file_int(file, "speed_percent", 100), 0, 200); gboolean vsync = key_file_bool(file, "vsync", TRUE), chroma = key_file_bool(file, "chroma_enabled", FALSE); float key[3] = { (float)key_file_double(file, "chroma_red", 0.0), (float)key_file_double(file, "chroma_green", 1.0), (float)key_file_double(file, "chroma_blue", 0.0) }, smoothing = CLAMP((float)key_file_double(file, "chroma_smoothing", 0.06), 0.005f, 0.30f); if (file) g_key_file_free(file);
    if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR)) { fprintf(stderr, "glacier-mint: no wallpaper to restore; choose one in Glacier Mint and click Set as current wallpaper\n"); g_free(path); return 1; }
    stop_wallpaper(); char *pid_path = data_path("wallpaper.pid"), pid[32]; g_snprintf(pid, sizeof(pid), "%d", (int)getpid()); g_file_set_contents(pid_path, pid, -1, NULL); g_free(pid_path);
    int status = wallpaper_main(path, CLAMP(fps, 1, 240), vsync, speed, chroma, key, smoothing); g_free(path); return status;
}

static int chosen_fps(App *app) { const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(app->fps)); return id ? CLAMP(atoi(id), 1, 240) : 30; }
static int chosen_speed(App *app) { return app->speed ? CLAMP((int)lround(gtk_range_get_value(GTK_RANGE(app->speed))), 0, 200) : 100; }
static void chosen_effects(App *app, gboolean *chroma, float key[3], float *smoothing) {
    GdkRGBA color = { 0.0, 1.0, 0.0, 1.0 }; if (app->chroma_color) gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(app->chroma_color), &color);
    *chroma = app->chroma_enable && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chroma_enable)); key[0] = (float)color.red; key[1] = (float)color.green; key[2] = (float)color.blue; *smoothing = app->chroma_smoothing ? CLAMP((float)gtk_range_get_value(GTK_RANGE(app->chroma_smoothing)), 0.005f, 0.30f) : 0.06f;
}

/* Starts (or restarts) the desktop wallpaper with the FPS limit and V-sync chosen in Settings. */
static gboolean launch_wallpaper(App *app, const char *path, const char *title) {
    int fps = chosen_fps(app), speed = chosen_speed(app); gboolean vsync = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->vsync)), chroma; float key[3], smoothing; char fps_text[16], speed_text[16], chroma_text[8], red_text[24], green_text[24], blue_text[24], smooth_text[24], *exe = self_executable(); GError *error = NULL; GPid pid = 0; chosen_effects(app, &chroma, key, &smoothing); g_snprintf(fps_text, sizeof(fps_text), "%d", fps); g_snprintf(speed_text, sizeof(speed_text), "%d", speed); g_snprintf(chroma_text, sizeof(chroma_text), "%d", chroma ? 1 : 0); g_ascii_dtostr(red_text, sizeof(red_text), key[0]); g_ascii_dtostr(green_text, sizeof(green_text), key[1]); g_ascii_dtostr(blue_text, sizeof(blue_text), key[2]); g_ascii_dtostr(smooth_text, sizeof(smooth_text), smoothing);
    stop_wallpaper();
    char *argv[] = { exe, "--wallpaper", (char *)path, "--fps", fps_text, "--vsync", vsync ? "1" : "0", "--speed", speed_text, "--chroma", chroma_text, "--key-r", red_text, "--key-g", green_text, "--key-b", blue_text, "--smooth", smooth_text, NULL };
    gboolean started = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, &error);
    if (!started) { set_status(app, error->message); g_error_free(error); }
    else { char *pid_path = data_path("wallpaper.pid"), buf[32]; snprintf(buf, sizeof(buf), "%d", (int)pid); g_file_set_contents(pid_path, buf, -1, NULL); g_free(pid_path); save_active_wallpaper(path, fps, vsync, title, speed, chroma, key, smoothing); }
    g_free(exe); return started;
}

void apply_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_code) { set_status(app, "Choose a shader first"); return; }
    gboolean saved_shader = app->shader_path && g_file_test(app->shader_path, G_FILE_TEST_IS_REGULAR); char *path = saved_shader ? g_strdup(app->shader_path) : data_path("current.frag"); GError *error = NULL;
    if (!saved_shader && !g_file_set_contents(path, app->shader_code, -1, &error)) { set_status(app, error->message); g_error_free(error); g_free(path); return; }
    if (launch_wallpaper(app, path, app->shader_title)) { char *message = g_strdup_printf("Wallpaper active · %s", app->shader_title); set_status(app, message); g_free(message); }
    g_free(path);
}

/* settings.ini keeps the FPS limit and V-sync between launches. */
void load_wallpaper_settings(App *app) {
    GKeyFile *file = load_key_file("settings.ini"); char id[16]; g_snprintf(id, sizeof(id), "%d", key_file_int(file, "fps", 30));
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->fps), id)) gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->fps), "30");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->vsync), key_file_int(file, "vsync", TRUE)); if (file) g_key_file_free(file); load_active_effects(app);
}

void wallpaper_settings_changed(GtkWidget *control, App *app) {
    (void)control; GKeyFile *file = g_key_file_new(); int fps = chosen_fps(app); gboolean vsync = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->vsync));
    g_key_file_set_integer(file, "wallpaper", "fps", fps); g_key_file_set_boolean(file, "wallpaper", "vsync", vsync);
    char *data = g_key_file_to_data(file, NULL, NULL), *path = data_path("settings.ini"); g_file_set_contents(path, data, -1, NULL); g_free(path); g_free(data); g_key_file_free(file);
    /* Apply to the running wallpaper right away. */
    char *pid_path = data_path("wallpaper.pid"), *pid = read_file(pid_path); g_free(pid_path); gboolean running = pid && is_wallpaper_process((pid_t)strtol(pid, NULL, 10)); g_free(pid);
    GKeyFile *active = running ? load_key_file("active.ini") : NULL; char *shader = active ? g_key_file_get_string(active, "wallpaper", "path", NULL) : NULL, *title = active ? g_key_file_get_string(active, "wallpaper", "title", NULL) : NULL;
    char *message = vsync && fps > app->monitor_refresh && app->monitor_refresh > 0 ? g_strdup_printf("Saved · with V-sync on the wallpaper runs at up to %d FPS, your monitor's refresh rate", app->monitor_refresh) : g_strdup_printf("Saved · %d FPS, V-sync %s", fps, vsync ? "on" : "off");
    if (shader && g_file_test(shader, G_FILE_TEST_IS_REGULAR) && launch_wallpaper(app, shader, title)) { char *applied = g_strdup_printf("%s · applied to the running wallpaper", message); g_free(message); message = applied; }
    gtk_label_set_text(GTK_LABEL(app->settings_status), message); g_free(message); g_free(shader); g_free(title); if (active) g_key_file_free(active);
}

void stop_clicked(GtkButton *button, App *app) { (void)button; stop_wallpaper(); char *active = data_path("active.ini"); g_remove(active); g_free(active); set_status(app, "Wallpaper stopped"); }

void open_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_id || !app->shader_id[0]) { set_status(app, "This is a bundled shader"); return; }
    char *url = g_strdup_printf("https://www.shadertoy.com/view/%s", app->shader_id); gtk_show_uri_on_window(GTK_WINDOW(app->window), url, GDK_CURRENT_TIME, NULL); g_free(url);
}

void delete_selected_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_path || !g_file_test(app->shader_path, G_FILE_TEST_EXISTS)) { set_status(app, "Select a saved wallpaper first"); return; }
    char *dir = wallpapers_dir(); gsize dir_len = strlen(dir); gboolean owned = g_str_has_prefix(app->shader_path, dir) && (app->shader_path[dir_len] == G_DIR_SEPARATOR || app->shader_path[dir_len] == '\0'); g_free(dir); if (!owned) { set_status(app, "Only saved wallpapers can be deleted"); return; }
    char *deleted_title = g_strdup(app->shader_title ? app->shader_title : "Wallpaper"); GList *children = gtk_container_get_children(GTK_CONTAINER(app->local_list));
    for (GList *l = children; l; l = l->next) { const char *path = g_object_get_data(G_OBJECT(l->data), "shader-path"); if (path && !g_strcmp0(path, app->shader_path)) { gtk_widget_destroy(GTK_WIDGET(l->data)); break; } } g_list_free(children);
    remove_shader_storage(app->shader_path); g_free(app->shader_path); app->shader_path = NULL; if (app->program) { gtk_gl_area_make_current(GTK_GL_AREA(app->preview)); glDeleteProgram(app->program); app->program = 0; } gtk_gl_area_make_current(GTK_GL_AREA(app->preview)); free_shader_channels(app->channel_textures); clear_widget_preview(app); if (app->constants_update_id) { g_source_remove(app->constants_update_id); app->constants_update_id = 0; } if (app->constants_box) { GList *constant_rows = gtk_container_get_children(GTK_CONTAINER(app->constants_box)); for (GList *l = constant_rows; l; l = l->next) gtk_widget_destroy(l->data); g_list_free(constant_rows); } if (app->constants) g_ptr_array_set_size(app->constants, 0); g_clear_pointer(&app->constants_source, g_free); g_clear_pointer(&app->shader_code, g_free); g_clear_pointer(&app->shader_title, g_free); g_clear_pointer(&app->shader_id, g_free);
    char *last = data_path("last.frag"), *title = data_path("last.title"), *id = data_path("last.id"), *path = data_path("last.path"); g_remove(last); g_remove(title); g_remove(id); g_remove(path); g_free(last); g_free(title); g_free(id); g_free(path); char *message = g_strdup_printf("Deleted · %s", deleted_title); set_status(app, message); g_free(message); g_free(deleted_title); gtk_gl_area_queue_render(GTK_GL_AREA(app->preview));
}

void load_pasted_clicked(GtkButton *button, App *app) {
    (void)button; GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->code_view)); GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end); char *code = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (!code[0]) { set_status(app, "Paste a Shadertoy Image shader first"); g_free(code); return; }
    if (import_shader(app, code, "Pasted shader", NULL)) gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
    g_free(code);
}
