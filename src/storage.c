#include "app.h"

#include <string.h>
#include <unistd.h>

void set_status(App *app, const char *text) {
    gtk_label_set_text(GTK_LABEL(app->status), text);
}

char *read_file(const char *path) {
    char *data = NULL; gsize len = 0; GError *error = NULL;
    if (!g_file_get_contents(path, &data, &len, &error)) {
        g_error_free(error); return NULL;
    }
    return data;
}

GtkFlowBoxChild *add_wallpaper_card(App *app, const char *path) {
    char *source = read_file(path); if (!source) return NULL; char *title = g_path_get_basename(path); char *dot = strrchr(title, '.'); if (dot) *dot = 0; g_strdelimit(title, "-_", ' ');
    GtkFlowBoxChild *row = GTK_FLOW_BOX_CHILD(gtk_flow_box_child_new()); GtkWidget *content = shader_row(title, "My wallpaper", source); gtk_widget_set_size_request(content, 175, 108); gtk_container_add(GTK_CONTAINER(row), content); g_object_set_data(G_OBJECT(row), "shader-title", g_object_get_data(G_OBJECT(content), "shader-title")); g_object_set_data_full(G_OBJECT(row), "shader-path", g_strdup(path), g_free); gtk_widget_show_all(GTK_WIDGET(row)); gtk_flow_box_insert(GTK_FLOW_BOX(app->local_list), GTK_WIDGET(row), -1); g_free(source); g_free(title); return row;
}

void populate_local(App *app) {
    char *dir = wallpapers_dir(); GDir *d = g_dir_open(dir, 0, NULL); if (!d) { g_free(dir); return; }
    const char *name; while ((name = g_dir_read_name(d))) { if (!g_str_has_suffix(name, ".frag")) continue; char *path = g_build_filename(dir, name, NULL); add_wallpaper_card(app, path); g_free(path); }
    g_dir_close(d); g_free(dir);
}

char *data_path(const char *name) {
    char *dir = g_build_filename(g_get_user_data_dir(), APP_ID, NULL); g_mkdir_with_parents(dir, 0700); char *path = g_build_filename(dir, name, NULL); g_free(dir); return path;
}

char *wallpapers_dir(void) {
    char *dir = data_path("wallpapers"); g_mkdir_with_parents(dir, 0700); return dir;
}

void import_shader(App *app, const char *source, const char *title, const char *id) {
    if (!source || !source[0]) return;
    char *dir = wallpapers_dir(); char *slug = g_strdup(title && title[0] ? title : "Imported shader"); g_strdelimit(slug, " /-_", '_'); if (!slug[0]) { g_free(slug); slug = g_strdup("Imported_shader"); }
    char *filename = g_strdup_printf("%s-%" G_GINT64_FORMAT ".frag", slug, g_get_real_time()); char *path = g_build_filename(dir, filename, NULL); GError *error = NULL;
    if (!g_file_set_contents(path, source, -1, &error)) { set_status(app, error->message); g_error_free(error); g_free(dir); g_free(slug); g_free(filename); g_free(path); return; }
    g_free(app->shader_path); app->shader_path = g_strdup(path);
    if (!load_preview(app, source, title, id)) { g_remove(path); g_free(app->shader_path); app->shader_path = NULL; }
    else { GtkFlowBoxChild *row = add_wallpaper_card(app, path); if (row) gtk_flow_box_select_child(GTK_FLOW_BOX(app->local_list), row); }
    g_free(dir); g_free(slug); g_free(filename); g_free(path);
}

gboolean restore_last_shader(App *app) {
    char *path = data_path("last.frag"); char *source = read_file(path); char *saved_path = data_path("last.path"); char *saved = read_file(saved_path); if (saved && saved[0] && g_file_test(saved, G_FILE_TEST_EXISTS)) { g_free(app->shader_path); app->shader_path = g_strdup(saved); } g_free(path); g_free(saved_path); g_free(saved);
    if (!source) return G_SOURCE_REMOVE;
    char *title_path = data_path("last.title"); char *id_path = data_path("last.id"); char *title = read_file(title_path); char *id = read_file(id_path);
    load_preview(app, source, title && title[0] ? title : "Last shader", id && id[0] ? id : NULL);
    g_free(source); g_free(title); g_free(id); g_free(title_path); g_free(id_path); return G_SOURCE_REMOVE;
}

char *self_executable(void) {
    char buf[4096]; ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1); if (n <= 0) return g_strdup("glacier-mint"); buf[n] = 0; return g_strdup(buf);
}

char *logo_path(void) {
    char *path = g_build_filename(g_get_current_dir(), LOGO_FILE, NULL);
    if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
    g_free(path);
    char *exe = self_executable(); char *dir = g_path_get_dirname(exe);
    path = g_build_filename(dir, LOGO_FILE, NULL);
    g_free(dir); g_free(exe); return path;
}
