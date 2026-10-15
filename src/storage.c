#include "app.h"

#include <stdio.h>
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
    char *source = read_file(path); if (!source) return NULL; char *folder = g_path_get_dirname(path); char *title_path = g_build_filename(folder, "title.txt", NULL); char *title = read_file(title_path); g_free(title_path);
    if (!title || !title[0]) { g_clear_pointer(&title, g_free); title = g_path_get_basename(path); char *dot = strrchr(title, '.'); if (dot) *dot = 0; if (!strcmp(title, "shader")) { g_free(title); title = g_path_get_basename(folder); } g_strdelimit(title, "-_", ' '); }
    GtkFlowBoxChild *row = GTK_FLOW_BOX_CHILD(gtk_flow_box_child_new()); GtkWidget *content = shader_row(title, "My wallpaper", source, path); gtk_widget_set_size_request(content, 175, 108); gtk_widget_set_hexpand(content, FALSE); gtk_widget_set_vexpand(content, FALSE); gtk_widget_set_size_request(GTK_WIDGET(row), 185, 118); gtk_widget_set_halign(GTK_WIDGET(row), GTK_ALIGN_START); gtk_widget_set_valign(GTK_WIDGET(row), GTK_ALIGN_START); gtk_widget_set_hexpand(GTK_WIDGET(row), FALSE); gtk_widget_set_vexpand(GTK_WIDGET(row), FALSE); gtk_container_add(GTK_CONTAINER(row), content); g_object_set_data(G_OBJECT(row), "shader-title", g_object_get_data(G_OBJECT(content), "shader-title")); g_object_set_data_full(G_OBJECT(row), "shader-path", g_strdup(path), g_free); gtk_widget_show_all(GTK_WIDGET(row)); gtk_flow_box_insert(GTK_FLOW_BOX(app->local_list), GTK_WIDGET(row), -1); g_free(source); g_free(title); return row;
}

void populate_local(App *app) {
    char *dir = wallpapers_dir(); GDir *d = g_dir_open(dir, 0, NULL); if (!d) { g_free(dir); return; }
    const char *name; while ((name = g_dir_read_name(d))) {
        char *path = g_build_filename(dir, name, NULL);
        if (g_str_has_suffix(name, ".frag")) add_wallpaper_card(app, path);
        else if (g_file_test(path, G_FILE_TEST_IS_DIR)) { char *shader = g_build_filename(path, "shader.frag", NULL); if (g_file_test(shader, G_FILE_TEST_IS_REGULAR)) add_wallpaper_card(app, shader); g_free(shader); }
        g_free(path);
    }
    g_dir_close(d); g_free(dir);
}

char *data_path(const char *name) {
    char *dir = g_build_filename(g_get_user_data_dir(), APP_ID, NULL); g_mkdir_with_parents(dir, 0700); char *path = g_build_filename(dir, name, NULL); g_free(dir); return path;
}

char *wallpapers_dir(void) {
    char *dir = data_path("wallpapers"); g_mkdir_with_parents(dir, 0700); return dir;
}

static size_t channel_write(void *ptr, size_t size, size_t count, void *userdata) {
    GByteArray *data = userdata; g_byte_array_append(data, ptr, size * count); return size * count;
}

static gboolean download_channel(const char *url, const char *path, GError **error) {
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) { g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Unsupported channel URL"); return FALSE; }
    CURL *curl = curl_easy_init(); if (!curl) { g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "Could not initialize channel download"); return FALSE; }
    GByteArray *data = g_byte_array_new(); curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, channel_write); curl_easy_setopt(curl, CURLOPT_WRITEDATA, data); curl_easy_setopt(curl, CURLOPT_USERAGENT, "Glacier-Mint/1.0"); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    CURLcode result = curl_easy_perform(curl); long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status); curl_easy_cleanup(curl);
    gboolean ok = result == CURLE_OK && status < 400 && data->len > 0;
    if (!ok) g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED, "%s", result != CURLE_OK ? curl_easy_strerror(result) : "Channel download failed");
    else ok = g_file_set_contents(path, (const char *)data->data, data->len, error);
    g_byte_array_unref(data); return ok;
}

static gboolean save_base64_channel(const char *encoded, const char *path, GError **error) {
    gsize length = 0; guchar *decoded = g_base64_decode(encoded, &length);
    if (!decoded || !length) { g_free(decoded); g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL, "Invalid channel data"); return FALSE; }
    gboolean ok = g_file_set_contents(path, (const char *)decoded, length, error); g_free(decoded); return ok;
}

static char *cubemap_url(const char *url, int face) {
    if (face == 0) return g_strdup(url);
    const char *query = strchr(url, '?'); gsize base_length = query ? (gsize)(query - url) : strlen(url); const char *dot = NULL;
    for (gsize i = 0; i < base_length; i++) if (url[i] == '.') dot = url + i;
    const char *slash = strrchr(url, '/'); if (!dot || (slash && dot < slash)) dot = url + base_length;
    char *stem = g_strndup(url, (gsize)(dot - url)); char *suffix = g_strndup(dot, base_length - (gsize)(dot - url)); char *result = g_strdup_printf("%s_%d%s%s", stem, face, suffix, query ? query : ""); g_free(stem); g_free(suffix); return result;
}

static gboolean save_channel_manifest(const char *folder, const char *channels_json, App *app) {
    if (!channels_json || !channels_json[0]) return FALSE;
    JsonParser *parser = json_parser_new(); GError *error = NULL;
    if (!json_parser_load_from_data(parser, channels_json, -1, &error)) { set_status(app, error->message); g_error_free(error); g_object_unref(parser); return TRUE; }
    JsonNode *root = json_parser_get_root(parser); if (!JSON_NODE_HOLDS_ARRAY(root)) { g_object_unref(parser); return TRUE; }
    JsonArray *channels = json_node_get_array(root); gboolean failed = FALSE;
    for (guint i = 0; i < json_array_get_length(channels); i++) {
        JsonObject *channel = json_array_get_object_element(channels, i); if (!channel) continue;
        JsonNode *channel_node = json_object_get_member(channel, "channel"); JsonNode *src_node = json_object_get_member(channel, "src");
        if (!channel_node || !src_node || !JSON_NODE_HOLDS_VALUE(channel_node) || !JSON_NODE_HOLDS_VALUE(src_node)) continue;
        int index = (int)json_node_get_int(channel_node); const char *src = json_node_get_string(src_node); if (index < 0 || index > 3 || !src || !src[0]) continue;
        JsonNode *type_node = json_object_get_member(channel, "type"); const char *type = type_node && JSON_NODE_HOLDS_VALUE(type_node) ? json_node_get_string(type_node) : "texture"; gboolean cube = type && (!g_ascii_strcasecmp(type, "cubemap") || !g_ascii_strcasecmp(type, "cube")); if (type && type[0] && strcmp(type, "texture") && !cube) continue;
        JsonNode *data_node = json_object_get_member(channel, "data"); gboolean complete = TRUE; int face_count = cube ? 6 : 1;
        for (int face = 0; face < face_count; face++) {
            char *filename = face == 0 ? g_strdup_printf("channel%d", index) : g_strdup_printf("channel%d_%d", index, face); char *path = g_build_filename(folder, filename, NULL); GError *download_error = NULL; JsonNode *face_data = data_node && JSON_NODE_HOLDS_ARRAY(data_node) && face < (int)json_array_get_length(json_node_get_array(data_node)) ? json_array_get_element(json_node_get_array(data_node), face) : data_node; const char *encoded = face_data && JSON_NODE_HOLDS_VALUE(face_data) ? json_node_get_string(face_data) : NULL; char *face_url = cubemap_url(src, face); gboolean downloaded = encoded && encoded[0] ? save_base64_channel(encoded, path, &download_error) : download_channel(face_url, path, &download_error);
            if (!downloaded) { complete = FALSE; g_warning("Could not download channel %d face %d: %s", index, face, download_error ? download_error->message : "unknown error"); g_clear_error(&download_error); }
            g_free(face_url); g_free(filename); g_free(path);
        }
        if (complete) { char *local = g_strdup_printf("channel%d", index); json_object_set_string_member(channel, "path", local); json_object_remove_member(channel, "data"); g_free(local); } else failed = TRUE;
    }
    JsonGenerator *generator = json_generator_new(); json_generator_set_root(generator, root); char *saved = json_generator_to_data(generator, NULL); char *manifest = g_build_filename(folder, "channels.json", NULL); g_file_set_contents(manifest, saved, -1, NULL);
    g_free(manifest); g_free(saved); g_object_unref(generator); g_object_unref(parser); return failed;
}

static void remove_tree(const char *path) {
    if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
        GDir *dir = g_dir_open(path, 0, NULL); if (dir) { const char *name; while ((name = g_dir_read_name(dir))) { char *child = g_build_filename(path, name, NULL); remove_tree(child); g_free(child); } g_dir_close(dir); }
        g_rmdir(path);
    } else g_remove(path);
}

void remove_shader_storage(const char *shader_path) {
    if (!shader_path) return;
    char *folder = g_path_get_dirname(shader_path); if (g_str_has_suffix(shader_path, "shader.frag")) remove_tree(folder); else g_remove(shader_path); g_free(folder);
}

void import_shader(App *app, const char *source, const char *title, const char *id) {
    import_shader_with_channels(app, source, title, id, NULL);
}

void import_shader_with_channels(App *app, const char *source, const char *title, const char *id, const char *channels_json) {
    if (!source || !source[0]) return;
    char *dir = wallpapers_dir(); char *slug = g_strdup(title && title[0] ? title : "Imported shader"); g_strdelimit(slug, " /-", '_'); if (!slug[0]) { g_free(slug); slug = g_strdup("Imported_shader"); }
    char *folder_name = g_strdup_printf("%s-%" G_GINT64_FORMAT, slug, g_get_real_time()); char *folder = g_build_filename(dir, folder_name, NULL); char *path = g_build_filename(folder, "shader.frag", NULL); g_mkdir_with_parents(folder, 0700); GError *error = NULL;
    if (!g_file_set_contents(path, source, -1, &error)) { set_status(app, error->message); g_error_free(error); remove_tree(folder); g_free(dir); g_free(slug); g_free(folder_name); g_free(folder); g_free(path); return; }
    char *title_path = g_build_filename(folder, "title.txt", NULL); g_file_set_contents(title_path, title && title[0] ? title : "Imported shader", -1, NULL); g_free(title_path); gboolean missing_channels = save_channel_manifest(folder, channels_json, app);
    g_free(app->shader_path); app->shader_path = g_strdup(path);
    if (!load_preview(app, source, title, id)) { remove_tree(folder); g_free(app->shader_path); app->shader_path = NULL; }
    else { GtkFlowBoxChild *row = add_wallpaper_card(app, path); if (row) gtk_flow_box_select_child(GTK_FLOW_BOX(app->local_list), row); if (missing_channels) set_status(app, "Imported shader; one or more channels could not be downloaded"); }
    g_free(dir); g_free(slug); g_free(folder_name); g_free(folder); g_free(path);
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
