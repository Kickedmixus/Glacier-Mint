#include "app.h"

#include <string.h>

static size_t curl_write(void *ptr, size_t size, size_t count, void *userdata) {
    GString *buf = userdata; g_string_append_len(buf, ptr, size * count); return size * count;
}

static gboolean search_done(gpointer data);
static gboolean load_done(gpointer data);

static char *http_get(const char *url, char **error_out) {
    CURL *curl = curl_easy_init(); if (!curl) { *error_out = g_strdup("libcurl unavailable"); return NULL; }
    GString *data = g_string_new(NULL); CURLcode result;
    curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data); curl_easy_setopt(curl, CURLOPT_USERAGENT, "Glacier-Mint/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    result = curl_easy_perform(curl); long status = 0; curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status); curl_easy_cleanup(curl);
    if (result != CURLE_OK || status >= 400) {
        *error_out = g_strdup(result != CURLE_OK ? curl_easy_strerror(result) : "Shadertoy returned an error");
        g_string_free(data, TRUE); return NULL;
    }
    return g_string_free(data, FALSE);
}

static JsonArray *json_array_member(JsonNode *root, const char *name) {
    if (JSON_NODE_HOLDS_ARRAY(root)) return json_node_get_array(root);
    if (!JSON_NODE_HOLDS_OBJECT(root)) return NULL;
    JsonObject *obj = json_node_get_object(root); JsonNode *member = json_object_get_member(obj, name);
    return member && JSON_NODE_HOLDS_ARRAY(member) ? json_node_get_array(member) : NULL;
}

static char *json_string(JsonObject *obj, const char *key) {
    JsonNode *n = json_object_get_member(obj, key); return n && JSON_NODE_HOLDS_VALUE(n) ? g_strdup(json_node_get_string(n)) : g_strdup("");
}

static gpointer search_worker(gpointer data) {
    SearchJob *job = data;
    char *query = g_uri_escape_string(job->query, NULL, TRUE);
    char *url = g_strdup_printf(API_ROOT "/shaders/query/%s?from=0&num=24&key=%s", query, job->key);
    char *error = NULL, *body = http_get(url, &error); GPtrArray *items = g_ptr_array_new();
    if (body) {
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, body, -1, NULL)) {
            JsonArray *array = json_array_member(json_parser_get_root(parser), "Shaders");
            if (array) for (guint i = 0; i < json_array_get_length(array); i++) {
                JsonObject *item = json_array_get_object_element(array, i); JsonObject *info = item;
                JsonNode *in = json_object_get_member(item, "info"); if (in && JSON_NODE_HOLDS_OBJECT(in)) info = json_node_get_object(in);
                ShaderResult *result = g_new0(ShaderResult, 1); result->id = json_string(info, "id");
                result->title = json_string(info, "name"); result->author = json_string(info, "username"); g_ptr_array_add(items, result);
            }
        } else error = g_strdup("Invalid response from Shadertoy");
        g_object_unref(parser); g_free(body);
    }
    g_free(url); g_free(query);
    SearchDone *done = g_new0(SearchDone, 1);
    done->app = job->app; done->items = items; done->error = error; g_free(job->query); g_free(job->key); g_free(job); g_idle_add(search_done, done); return NULL;
}

static gboolean search_done(gpointer data) {
    SearchDone *done = data; App *app = done->app;
    GList *children = gtk_container_get_children(GTK_CONTAINER(app->results)); for (GList *l = children; l; l = l->next) gtk_widget_destroy(l->data); g_list_free(children);
    if (done->error) set_status(app, done->error);
    else if (!done->items->len) set_status(app, "No public API shaders found");
    else {
        set_status(app, "Select a result to load it");
        for (guint i = 0; i < done->items->len; i++) {
            ShaderResult *r = g_ptr_array_index(done->items, i); GtkWidget *row = gtk_flow_box_child_new();
            GtkWidget *content = shader_row(r->title[0] ? r->title : r->id, r->author[0] ? r->author : r->id, NULL);
            gtk_widget_set_size_request(content, 175, 58); gtk_container_add(GTK_CONTAINER(row), content); g_object_set_data_full(G_OBJECT(row), "shader-id", g_strdup(r->id), g_free);
            gtk_widget_show_all(row); gtk_flow_box_insert(GTK_FLOW_BOX(app->results), row, -1);
        }
    }
    for (guint i = 0; i < done->items->len; i++) { ShaderResult *r = g_ptr_array_index(done->items, i); g_free(r->id); g_free(r->title); g_free(r->author); g_free(r); }
    g_ptr_array_free(done->items, TRUE); g_free(done->error); g_free(done); return G_SOURCE_REMOVE;
}

static G_GNUC_UNUSED void search_clicked(GtkButton *button, App *app) {
    (void)button; const char *query = gtk_entry_get_text(GTK_ENTRY(app->search)); const char *key = gtk_entry_get_text(GTK_ENTRY(app->api_key));
    if (!query[0]) { set_status(app, "Type a search term first"); return; }
    if (!key[0]) { set_status(app, "Shadertoy API needs an app key · set SHADERTOY_API_KEY"); return; }
    set_status(app, "Searching Shadertoy…");
    SearchJob *job = g_new0(SearchJob, 1); job->app = app; job->query = g_strdup(query); job->key = g_strdup(key);
    GThread *thread = g_thread_new("shadertoy-search", search_worker, job); g_thread_unref(thread);
}

static gpointer load_worker(gpointer data) {
    LoadJob *job = data; char *url = g_strdup_printf(API_ROOT "/shaders/%s?key=%s", job->id, job->key);
    char *error = NULL, *body = http_get(url, &error); char *code = NULL, *title = NULL;
    if (body) {
        JsonParser *parser = json_parser_new();
        if (json_parser_load_from_data(parser, body, -1, NULL)) {
            JsonNode *root = json_parser_get_root(parser); JsonObject *obj = JSON_NODE_HOLDS_OBJECT(root) ? json_node_get_object(root) : NULL;
            JsonNode *info_node = obj ? json_object_get_member(obj, "info") : NULL; JsonObject *info = info_node && JSON_NODE_HOLDS_OBJECT(info_node) ? json_node_get_object(info_node) : NULL;
            if (info) title = json_string(info, "name");
            JsonNode *passes_node = obj ? json_object_get_member(obj, "renderpass") : NULL;
            if (passes_node && JSON_NODE_HOLDS_ARRAY(passes_node)) {
                JsonArray *passes = json_node_get_array(passes_node);
                for (guint i = 0; i < json_array_get_length(passes); i++) { JsonObject *pass = json_array_get_object_element(passes, i); char *type = json_string(pass, "type"); if (!code && (!strcmp(type, "Image") || i == 0)) code = json_string(pass, "code"); g_free(type); }
            }
        } else error = g_strdup("Invalid shader response");
        g_object_unref(parser); g_free(body);
    }
    g_free(url); LoadDone *done = g_new0(LoadDone, 1);
    done->app = job->app; done->id = g_strdup(job->id); done->code = code; done->title = title; done->error = error; g_free(job->id); g_free(job->key); g_free(job); g_idle_add(load_done, done); return NULL;
}

static gboolean load_done(gpointer data) {
    LoadDone *done = data;
    if (done->error) set_status(done->app, done->error); else if (!done->code) set_status(done->app, "Shader has no Image pass");
    else load_preview(done->app, done->code, done->title, done->id);
    g_free(done->id); g_free(done->code); g_free(done->title); g_free(done->error); g_free(done); return G_SOURCE_REMOVE;
}

static G_GNUC_UNUSED void result_activated(GtkFlowBox *box, GtkFlowBoxChild *row, App *app) {
    (void)box; const char *id = g_object_get_data(G_OBJECT(row), "shader-id"); if (!id) return;
    const char *key = gtk_entry_get_text(GTK_ENTRY(app->api_key)); if (!key[0]) return; set_status(app, "Downloading shader…");
    LoadJob *job = g_new0(LoadJob, 1); job->app = app; job->id = g_strdup(id); job->key = g_strdup(key); GThread *thread = g_thread_new("shadertoy-load", load_worker, job); g_thread_unref(thread);
}
