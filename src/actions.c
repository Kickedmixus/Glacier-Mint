#include "app.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void stop_wallpaper(void) {
    char *pid_path = data_path("wallpaper.pid"), *contents = read_file(pid_path); if (contents) { pid_t pid = (pid_t)strtol(contents, NULL, 10); if (pid > 1) kill(pid, SIGTERM); g_free(contents); } g_remove(pid_path); g_free(pid_path);
}

void apply_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_code) { set_status(app, "Choose a shader first"); return; }
    char *path = data_path("current.frag"), *pid_path = data_path("wallpaper.pid"); GError *error = NULL;
    if (!g_file_set_contents(path, app->shader_code, -1, &error)) { set_status(app, error->message); g_error_free(error); g_free(path); g_free(pid_path); return; }
    stop_wallpaper(); char *exe = self_executable(); const char *fps = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->fps));
    char *argv[] = { exe, "--wallpaper", path, "--fps", (char *)(fps ? fps : "30"), NULL }; GPid pid = 0;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, &error)) {
        set_status(app, error->message); g_error_free(error);
    } else { char buf[32]; snprintf(buf, sizeof(buf), "%d", (int)pid); g_file_set_contents(pid_path, buf, -1, NULL); char *message = g_strdup_printf("Wallpaper active · %s", app->shader_title); set_status(app, message); g_free(message); }
    g_free(exe); g_free(path); g_free(pid_path); if (fps) g_free((char *)fps);
}

void stop_clicked(GtkButton *button, App *app) { (void)button; stop_wallpaper(); set_status(app, "Wallpaper stopped"); }

void open_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_id || !app->shader_id[0]) { set_status(app, "This is a bundled shader"); return; }
    char *url = g_strdup_printf("https://www.shadertoy.com/view/%s", app->shader_id); gtk_show_uri_on_window(GTK_WINDOW(app->window), url, GDK_CURRENT_TIME, NULL); g_free(url);
}

void delete_selected_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_path || !g_file_test(app->shader_path, G_FILE_TEST_EXISTS)) { set_status(app, "Select a saved wallpaper first"); return; }
    char *dir = wallpapers_dir(); gboolean owned = g_str_has_prefix(app->shader_path, dir); g_free(dir); if (!owned) { set_status(app, "Only saved wallpapers can be deleted"); return; }
    char *deleted_title = g_strdup(app->shader_title ? app->shader_title : "Wallpaper"); GList *children = gtk_container_get_children(GTK_CONTAINER(app->local_list));
    for (GList *l = children; l; l = l->next) { const char *path = g_object_get_data(G_OBJECT(l->data), "shader-path"); if (path && !g_strcmp0(path, app->shader_path)) { gtk_widget_destroy(GTK_WIDGET(l->data)); break; } } g_list_free(children);
    g_remove(app->shader_path); g_free(app->shader_path); app->shader_path = NULL; if (app->program) { gtk_gl_area_make_current(GTK_GL_AREA(app->preview)); glDeleteProgram(app->program); app->program = 0; } g_clear_pointer(&app->shader_code, g_free); g_clear_pointer(&app->shader_title, g_free); g_clear_pointer(&app->shader_id, g_free);
    char *last = data_path("last.frag"), *title = data_path("last.title"), *id = data_path("last.id"), *path = data_path("last.path"); g_remove(last); g_remove(title); g_remove(id); g_remove(path); g_free(last); g_free(title); g_free(id); g_free(path); char *message = g_strdup_printf("Deleted · %s", deleted_title); set_status(app, message); g_free(message); g_free(deleted_title); gtk_gl_area_queue_render(GTK_GL_AREA(app->preview));
}

void load_pasted_clicked(GtkButton *button, App *app) {
    (void)button; GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->code_view)); GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end); char *code = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (!code[0]) { set_status(app, "Paste a Shadertoy Image shader first"); g_free(code); return; }
    import_shader(app, code, "Pasted shader", NULL); gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0); g_free(code);
}
