#include <gtk/gtk.h>
#include <glib/gstdio.h>
#ifdef HAVE_WEBKIT
#include <webkit2/webkit2.h>
#endif
#include <epoxy/gl.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define APP_NAME "Glacier Mint"
#define APP_ID "glacier-mint"
#define API_ROOT "https://www.shadertoy.com/api/v1"

typedef struct { char *id, *title, *author; } ShaderResult;
typedef struct { char *code, *title, *id; } ShaderLoad;

typedef struct {
    GtkWidget *window, *notebook, *preview, *results, *local_list, *status;
    GtkWidget *search, *api_key, *fps, *scale, *code_view, *browser, *browser_url;
    char *shader_code, *shader_title, *shader_id, *shader_path;
    GLuint program, vao;
    gint64 started;
    gboolean preview_ready;
} App;
typedef struct { App *app; char *query; char *key; } SearchJob;
typedef struct { App *app; GPtrArray *items; char *error; } SearchDone;
typedef struct { App *app; char *id; char *key; } LoadJob;
typedef struct { App *app; char *id; char *code; char *title; char *error; } LoadDone;
typedef struct { char *source; GLuint program, vao; gint64 started; } ShaderThumb;

static char *data_path(const char *name);
static char *wallpapers_dir(void);
static GtkFlowBoxChild *add_wallpaper_card(App *app, const char *path);
static void import_shader(App *app, const char *source, const char *title, const char *id);

static const char *vertex_shader =
    "#version 330 core\n"
    "void main() {\n"
    "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static const char *fragment_header =
    "#version 330 core\n"
    "#define texture2D texture\n"
    "#define textureCube texture\n"
    "uniform vec3 iResolution; uniform float iTime; uniform float iTimeDelta;\n"
    "uniform float iFrameRate; uniform int iFrame;\n"
    "uniform float iChannelTime[4]; uniform vec3 iChannelResolution[4];\n"
    "uniform vec4 iMouse; uniform vec4 iDate; uniform float iSampleRate;\n"
    "uniform float iBeat; uniform float iBPM; uniform float iAudioLevel; uniform float iAudioBass;\n"
    "uniform sampler2D iChannel0; uniform sampler2D iChannel1;\n"
    "uniform sampler2D iChannel2; uniform sampler2D iChannel3;\n"
    "out vec4 glacierColor;\n";

static const char *fragment_footer =
    "void main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); glacierColor = vec4(c.rgb, 1.0); }\n";

static void set_status(App *app, const char *text) {
    gtk_label_set_text(GTK_LABEL(app->status), text);
}

static char *read_file(const char *path) {
    char *data = NULL; gsize len = 0; GError *error = NULL;
    if (!g_file_get_contents(path, &data, &len, &error)) {
        g_error_free(error); return NULL;
    }
    return data;
}

static char *normalize_shader(const char *source) {
    GString *out = g_string_new(NULL);
    const char *p = source;
    while (*p) {
        const char *end = strchr(p, '\n');
        gsize n = end ? (gsize)(end - p) : strlen(p);
        while (n && (p[n - 1] == '\r' || p[n - 1] == ' ' || p[n - 1] == '\t')) n--;
        if (g_str_has_prefix(p, "#version") || g_str_has_prefix(p, "precision ")) {
            /* The wrapper supplies a desktop GLSL version and precision defaults. */
        } else {
            g_string_append_len(out, p, n); g_string_append_c(out, '\n');
        }
        p = end ? end + 1 : p + strlen(p);
    }
    return g_string_free(out, FALSE);
}

static GLuint compile_one(GLenum type, const char *source, char **error) {
    GLuint shader = glCreateShader(type); GLint ok = 0, length = 0;
    glShaderSource(shader, 1, &source, NULL); glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok) return shader;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    char *log = g_malloc0((gsize)MAX(length, 1));
    glGetShaderInfoLog(shader, length, NULL, log);
    if (error) *error = log; else g_free(log);
    glDeleteShader(shader); return 0;
}

static GLuint make_program(const char *user_source, char **error) {
    char *normalized = normalize_shader(user_source);
    GString *frag = g_string_new(fragment_header);
    g_string_append(frag, normalized); g_string_append(frag, fragment_footer);
    g_free(normalized);
    GLuint vs = compile_one(GL_VERTEX_SHADER, vertex_shader, error);
    if (!vs) { g_string_free(frag, TRUE); return 0; }
    GLuint fs = compile_one(GL_FRAGMENT_SHADER, frag->str, error);
    g_string_free(frag, TRUE);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint program = glCreateProgram(); GLint ok = 0, length = 0;
    glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program);
    glDeleteShader(vs); glDeleteShader(fs); glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) return program;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    char *log = g_malloc0((gsize)MAX(length, 1));
    glGetProgramInfoLog(program, length, NULL, log);
    if (error) *error = log; else g_free(log);
    glDeleteProgram(program); return 0;
}

static void set_uniforms(GLuint program, float width, float height, float time_s, int frame) {
    GLint loc;
    loc = glGetUniformLocation(program, "iResolution"); if (loc >= 0) glUniform3f(loc, width, height, 1.0f);
    loc = glGetUniformLocation(program, "iTime"); if (loc >= 0) glUniform1f(loc, time_s);
    loc = glGetUniformLocation(program, "iTimeDelta"); if (loc >= 0) glUniform1f(loc, 1.0f / 60.0f);
    loc = glGetUniformLocation(program, "iFrameRate"); if (loc >= 0) glUniform1f(loc, 60.0f);
    loc = glGetUniformLocation(program, "iFrame"); if (loc >= 0) glUniform1i(loc, frame);
    loc = glGetUniformLocation(program, "iBeat"); if (loc >= 0) glUniform1f(loc, time_s);
    loc = glGetUniformLocation(program, "iBPM"); if (loc >= 0) glUniform1f(loc, 60.0f);
    loc = glGetUniformLocation(program, "iAudioLevel"); if (loc >= 0) glUniform1f(loc, 0.0f);
    loc = glGetUniformLocation(program, "iAudioBass"); if (loc >= 0) glUniform1f(loc, 0.0f);
    loc = glGetUniformLocation(program, "iSampleRate"); if (loc >= 0) glUniform1f(loc, 44100.0f);
    loc = glGetUniformLocation(program, "iMouse"); if (loc >= 0) glUniform4f(loc, 0, 0, 0, 0);
}

static void preview_realize(GtkGLArea *area, App *app) {
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) return;
    glGenVertexArrays(1, &app->vao); glBindVertexArray(app->vao);
    app->preview_ready = TRUE;
}

static gboolean preview_render(GtkGLArea *area, GdkGLContext *context, App *app) {
    (void)context;
    if (!app->preview_ready || !app->program) return TRUE;
    int width = gtk_widget_get_allocated_width(GTK_WIDGET(area));
    int height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    float elapsed = (float)(g_get_monotonic_time() - app->started) / 1000000.0f;
    glViewport(0, 0, width, height); glClearColor(0.025f, 0.035f, 0.06f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(app->program); set_uniforms(app->program, width, height, elapsed, (int)(elapsed * 60));
    glBindVertexArray(app->vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

static gboolean tick_preview(App *app) { gtk_gl_area_queue_render(GTK_GL_AREA(app->preview)); return G_SOURCE_CONTINUE; }

static gboolean load_preview(App *app, const char *source, const char *title, const char *id) {
    if (!source) return FALSE;
    gtk_gl_area_make_current(GTK_GL_AREA(app->preview));
    if (gtk_gl_area_get_error(GTK_GL_AREA(app->preview))) return FALSE;
    char *error = NULL; GLuint program = make_program(source, &error);
    if (!program) {
        char *short_error = g_strndup(error ? error : "unknown shader error", 220);
        char *message = g_strdup_printf("Shader error: %s", short_error);
        set_status(app, message); g_free(message); g_free(short_error); g_free(error); return FALSE;
    }
    if (app->program) glDeleteProgram(app->program);
    app->program = program; g_free(app->shader_code); app->shader_code = g_strdup(source);
    g_free(app->shader_title); app->shader_title = g_strdup(title ? title : "Untitled shader");
    g_free(app->shader_id); app->shader_id = g_strdup(id ? id : "");
    char *last_shader = data_path("last.frag"); char *last_title = data_path("last.title"); char *last_id = data_path("last.id"); char *last_path = data_path("last.path");
    g_file_set_contents(last_shader, source, -1, NULL); g_file_set_contents(last_title, app->shader_title, -1, NULL); g_file_set_contents(last_id, app->shader_id, -1, NULL);
    g_file_set_contents(last_path, app->shader_path ? app->shader_path : "", -1, NULL); g_free(last_shader); g_free(last_title); g_free(last_id); g_free(last_path);
    char *message = g_strdup_printf("Ready · %s", app->shader_title); set_status(app, message); g_free(message);
    gtk_gl_area_queue_render(GTK_GL_AREA(app->preview));
    return TRUE;
}

static void choose_shader_file(App *app, const char *path, const char *title) {
    char *source = read_file(path);
    if (!source) { set_status(app, "Could not read shader file"); return; }
    g_free(app->shader_path); app->shader_path = g_strdup(path);
    load_preview(app, source, title, NULL); g_free(source);
}

static void local_row_activated(GtkFlowBox *box, GtkFlowBoxChild *row, App *app) {
    (void)box;
    const char *path = g_object_get_data(G_OBJECT(row), "shader-path");
    const char *title = gtk_label_get_text(GTK_LABEL(g_object_get_data(G_OBJECT(row), "shader-title")));
    choose_shader_file(app, path, title);
}

static void thumb_free(ShaderThumb *thumb) { g_free(thumb->source); g_free(thumb); }

static void thumb_realize(GtkGLArea *area, ShaderThumb *thumb) {
    gtk_gl_area_make_current(area); if (gtk_gl_area_get_error(area)) return;
    char *error = NULL; thumb->program = make_program(thumb->source, &error); g_free(error); if (thumb->program) glGenVertexArrays(1, &thumb->vao); thumb->started = g_get_monotonic_time();
}

static gboolean thumb_render(GtkGLArea *area, GdkGLContext *context, ShaderThumb *thumb) {
    (void)context; int width = gtk_widget_get_allocated_width(GTK_WIDGET(area)), height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    glViewport(0, 0, width, height); glClearColor(0.035f, 0.045f, 0.055f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    if (!thumb->program) return TRUE;
    float elapsed = (float)(g_get_monotonic_time() - thumb->started) / 1000000.0f; glUseProgram(thumb->program); set_uniforms(thumb->program, width, height, elapsed, (int)(elapsed * 60)); glBindVertexArray(thumb->vao); glDrawArrays(GL_TRIANGLES, 0, 3); return TRUE;
}

static gboolean thumb_tick(GtkGLArea *area) { if (gtk_widget_get_realized(GTK_WIDGET(area))) gtk_gl_area_queue_render(area); return G_SOURCE_CONTINUE; }

static GtkWidget *shader_row(const char *title, const char *subtitle, const char *source) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    if (source) {
        GtkWidget *thumb_area = gtk_gl_area_new(); ShaderThumb *thumb = g_new0(ShaderThumb, 1); thumb->source = g_strdup(source);
        gtk_widget_set_size_request(thumb_area, 175, 82); gtk_style_context_add_class(gtk_widget_get_style_context(thumb_area), "thumb"); g_object_set_data_full(G_OBJECT(thumb_area), "shader-thumb", thumb, (GDestroyNotify)thumb_free);
        g_signal_connect(thumb_area, "realize", G_CALLBACK(thumb_realize), thumb); g_signal_connect(thumb_area, "render", G_CALLBACK(thumb_render), thumb); g_timeout_add(33, (GSourceFunc)thumb_tick, thumb_area); gtk_box_pack_start(GTK_BOX(box), thumb_area, FALSE, FALSE, 0);
    }
    GtkWidget *name = gtk_label_new(title); GtkWidget *sub = gtk_label_new(subtitle ? subtitle : "");
    gtk_widget_set_halign(name, GTK_ALIGN_START); gtk_widget_set_halign(sub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "row-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "row-subtitle");
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(box), "shader-title", name); return box;
}

static GtkFlowBoxChild *add_wallpaper_card(App *app, const char *path) {
    char *source = read_file(path); if (!source) return NULL; char *title = g_path_get_basename(path); char *dot = strrchr(title, '.'); if (dot) *dot = 0; g_strdelimit(title, "-_", ' ');
    GtkFlowBoxChild *row = GTK_FLOW_BOX_CHILD(gtk_flow_box_child_new()); GtkWidget *content = shader_row(title, "My wallpaper", source); gtk_widget_set_size_request(content, 175, 108); gtk_container_add(GTK_CONTAINER(row), content); g_object_set_data(G_OBJECT(row), "shader-title", g_object_get_data(G_OBJECT(content), "shader-title")); g_object_set_data_full(G_OBJECT(row), "shader-path", g_strdup(path), g_free); gtk_widget_show_all(GTK_WIDGET(row)); gtk_flow_box_insert(GTK_FLOW_BOX(app->local_list), GTK_WIDGET(row), -1); g_free(source); g_free(title); return row;
}

static void populate_local(App *app) {
    char *dir = wallpapers_dir(); GDir *d = g_dir_open(dir, 0, NULL); if (!d) { g_free(dir); return; }
    const char *name; while ((name = g_dir_read_name(d))) { if (!g_str_has_suffix(name, ".frag")) continue; char *path = g_build_filename(dir, name, NULL); add_wallpaper_card(app, path); g_free(path); }
    g_dir_close(d); g_free(dir);
}

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

static char *data_path(const char *name) {
    char *dir = g_build_filename(g_get_user_data_dir(), APP_ID, NULL); g_mkdir_with_parents(dir, 0700); char *path = g_build_filename(dir, name, NULL); g_free(dir); return path;
}

static char *wallpapers_dir(void) {
    char *dir = data_path("wallpapers"); g_mkdir_with_parents(dir, 0700); return dir;
}

static void import_shader(App *app, const char *source, const char *title, const char *id) {
    if (!source || !source[0]) return;
    char *dir = wallpapers_dir(); char *slug = g_strdup(title && title[0] ? title : "Imported shader"); g_strdelimit(slug, " /-_", '_'); if (!slug[0]) { g_free(slug); slug = g_strdup("Imported_shader"); }
    char *filename = g_strdup_printf("%s-%" G_GINT64_FORMAT ".frag", slug, g_get_real_time()); char *path = g_build_filename(dir, filename, NULL); GError *error = NULL;
    if (!g_file_set_contents(path, source, -1, &error)) { set_status(app, error->message); g_error_free(error); g_free(dir); g_free(slug); g_free(filename); g_free(path); return; }
    g_free(app->shader_path); app->shader_path = g_strdup(path);
    if (!load_preview(app, source, title, id)) { g_remove(path); g_free(app->shader_path); app->shader_path = NULL; }
    else { GtkFlowBoxChild *row = add_wallpaper_card(app, path); if (row) gtk_flow_box_select_child(GTK_FLOW_BOX(app->local_list), row); }
    g_free(dir); g_free(slug); g_free(filename); g_free(path);
}

static gboolean restore_last_shader(App *app) {
    char *path = data_path("last.frag"); char *source = read_file(path); char *saved_path = data_path("last.path"); char *saved = read_file(saved_path); if (saved && saved[0] && g_file_test(saved, G_FILE_TEST_EXISTS)) { g_free(app->shader_path); app->shader_path = g_strdup(saved); } g_free(path); g_free(saved_path); g_free(saved);
    if (!source) return G_SOURCE_REMOVE;
    char *title_path = data_path("last.title"); char *id_path = data_path("last.id"); char *title = read_file(title_path); char *id = read_file(id_path);
    load_preview(app, source, title && title[0] ? title : "Last shader", id && id[0] ? id : NULL);
    g_free(source); g_free(title); g_free(id); g_free(title_path); g_free(id_path); return G_SOURCE_REMOVE;
}

static char *self_executable(void) {
    char buf[4096]; ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1); if (n <= 0) return g_strdup("glacier-mint"); buf[n] = 0; return g_strdup(buf);
}

static void stop_wallpaper(void) {
    char *pid_path = data_path("wallpaper.pid"), *contents = read_file(pid_path); if (contents) { pid_t pid = (pid_t)strtol(contents, NULL, 10); if (pid > 1) kill(pid, SIGTERM); g_free(contents); } g_remove(pid_path); g_free(pid_path);
}

static void apply_clicked(GtkButton *button, App *app) {
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

static void stop_clicked(GtkButton *button, App *app) { (void)button; stop_wallpaper(); set_status(app, "Wallpaper stopped"); }

static void open_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_id || !app->shader_id[0]) { set_status(app, "This is a bundled shader"); return; }
    char *url = g_strdup_printf("https://www.shadertoy.com/view/%s", app->shader_id); gtk_show_uri_on_window(GTK_WINDOW(app->window), url, GDK_CURRENT_TIME, NULL); g_free(url);
}

static void delete_selected_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->shader_path || !g_file_test(app->shader_path, G_FILE_TEST_EXISTS)) { set_status(app, "Select a saved wallpaper first"); return; }
    char *dir = wallpapers_dir(); gboolean owned = g_str_has_prefix(app->shader_path, dir); g_free(dir); if (!owned) { set_status(app, "Only saved wallpapers can be deleted"); return; }
    char *deleted_title = g_strdup(app->shader_title ? app->shader_title : "Wallpaper"); GList *children = gtk_container_get_children(GTK_CONTAINER(app->local_list));
    for (GList *l = children; l; l = l->next) { const char *path = g_object_get_data(G_OBJECT(l->data), "shader-path"); if (path && !g_strcmp0(path, app->shader_path)) { gtk_widget_destroy(GTK_WIDGET(l->data)); break; } } g_list_free(children);
    g_remove(app->shader_path); g_free(app->shader_path); app->shader_path = NULL; if (app->program) { gtk_gl_area_make_current(GTK_GL_AREA(app->preview)); glDeleteProgram(app->program); app->program = 0; } g_clear_pointer(&app->shader_code, g_free); g_clear_pointer(&app->shader_title, g_free); g_clear_pointer(&app->shader_id, g_free);
    char *last = data_path("last.frag"), *title = data_path("last.title"), *id = data_path("last.id"), *path = data_path("last.path"); g_remove(last); g_remove(title); g_remove(id); g_remove(path); g_free(last); g_free(title); g_free(id); g_free(path); char *message = g_strdup_printf("Deleted · %s", deleted_title); set_status(app, message); g_free(message); g_free(deleted_title); gtk_gl_area_queue_render(GTK_GL_AREA(app->preview));
}

static void load_pasted_clicked(GtkButton *button, App *app) {
    (void)button; GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(app->code_view)); GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end); char *code = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    if (!code[0]) { set_status(app, "Paste a Shadertoy Image shader first"); g_free(code); return; }
    import_shader(app, code, "Pasted shader", NULL); gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0); g_free(code);
}

#ifdef HAVE_WEBKIT
static const char *browser_import_script =
    "(function(){"
    "if(window.top!==window)return;"
    "function send(x){window.webkit.messageHandlers.glacier.postMessage(JSON.stringify(x));}"
    "function source(){var a=[];"
    "document.querySelectorAll('textarea').forEach(function(e){if(e.value)a.push(e.value);});"
    "document.querySelectorAll('.CodeMirror').forEach(function(e){if(e.CodeMirror)a.push(e.CodeMirror.getValue());});"
    "document.querySelectorAll('[contenteditable=true]').forEach(function(e){if(e.innerText)a.push(e.innerText);});"
    "document.querySelectorAll('pre').forEach(function(e){if(e.innerText)a.push(e.innerText);});"
    "return a.find(function(x){return x.indexOf('mainImage')>=0;});}"
    "function add(){if(document.getElementById('glacier-import'))return;var b=document.createElement('button');"
    "b.id='glacier-import';b.textContent='Import shader into Glacier Mint';b.onclick=function(){var c=source();"
    "if(!c){send({error:'Open the shader Image code editor first, then try again.'});return;}"
    "send({code:c,title:document.title,id:(location.pathname.match(/\\/view\\/([^/?]+)/)||[])[1]||''});};"
    "b.style='position:fixed;z-index:2147483647;right:18px;top:18px;padding:10px 14px;background:#a9e0ef;color:#071014;border:1px solid #071014;font:600 14px sans-serif;cursor:pointer';"
    "document.body.appendChild(b);}"
    "add();new MutationObserver(add).observe(document.documentElement,{childList:true,subtree:true});"
    "})();";

static void browser_message(WebKitUserContentManager *manager, WebKitJavascriptResult *result, App *app) {
    (void)manager; JSCValue *value = webkit_javascript_result_get_js_value(result); char *message = jsc_value_to_string(value); JsonParser *parser = json_parser_new();
    if (json_parser_load_from_data(parser, message, -1, NULL)) { JsonObject *obj = json_node_get_object(json_parser_get_root(parser)); JsonNode *error = json_object_get_member(obj, "error"); JsonNode *code = json_object_get_member(obj, "code");
        if (error && JSON_NODE_HOLDS_VALUE(error)) set_status(app, json_node_get_string(error)); else if (code && JSON_NODE_HOLDS_VALUE(code)) { const char *title = "Imported Shadertoy shader"; const char *id = ""; JsonNode *title_node = json_object_get_member(obj, "title"), *id_node = json_object_get_member(obj, "id"); if (title_node && JSON_NODE_HOLDS_VALUE(title_node)) title = json_node_get_string(title_node); if (id_node && JSON_NODE_HOLDS_VALUE(id_node)) id = json_node_get_string(id_node); import_shader(app, json_node_get_string(code), title, id); gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0); }
    }
    g_object_unref(parser); g_free(message); webkit_javascript_result_unref(result);
}

static void browser_go(App *app) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(app->browser_url)); if (!text[0]) return; char *url = g_strdup(text);
    if (!g_str_has_prefix(url, "http://") && !g_str_has_prefix(url, "https://")) { char *with_scheme = g_strdup_printf("https://%s", url); g_free(url); url = with_scheme; }
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->browser), url); g_free(url);
}

static void browser_back(GtkButton *button, App *app) { (void)button; if (webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(app->browser))) webkit_web_view_go_back(WEBKIT_WEB_VIEW(app->browser)); }
static void browser_forward(GtkButton *button, App *app) { (void)button; if (webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(app->browser))) webkit_web_view_go_forward(WEBKIT_WEB_VIEW(app->browser)); }
static void browser_load(GtkButton *button, App *app) { (void)button; browser_go(app); }
static void browser_load_changed(WebKitWebView *view, WebKitLoadEvent event, App *app) { if (event == WEBKIT_LOAD_COMMITTED) { const char *uri = webkit_web_view_get_uri(view); if (uri) gtk_entry_set_text(GTK_ENTRY(app->browser_url), uri); } }
#endif

static void browser_external_clicked(GtkButton *button, App *app) { (void)button; gtk_show_uri_on_window(GTK_WINDOW(app->window), "https://www.shadertoy.com", GDK_CURRENT_TIME, NULL); }

static GtkWidget *section_label(const char *text) { GtkWidget *label = gtk_label_new(text); gtk_widget_set_halign(label, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(label), "section"); return label; }

static void add_css(void) {
    GtkCssProvider *css = gtk_css_provider_new(); gtk_css_provider_load_from_data(css,
        "* { border-radius: 0; } window, notebook, notebook stack { background: #020304; color: #dce5e8; } notebook header { background: #010203; border-bottom: 1px solid #182126; } notebook tab { padding: 11px 22px; color: #59666d; border: 0; } notebook tab:checked { color: #a9e0ef; border-bottom: 2px solid #a9e0ef; } .brand { font-size: 21px; font-weight: 600; color: #e5f0f2; } .muted, .row-subtitle { color: #596970; } .section { margin-top: 12px; margin-bottom: 5px; font-size: 11px; font-weight: 700; letter-spacing: 1px; color: #8fcbdc; } entry, combobox, button { min-height: 32px; border: 1px solid #202b30; background: #111518; color: #dce5e8; box-shadow: none; } entry:focus, textview:focus { border: 1px solid #a9e0ef; } button:hover { background: #1a2226; border-color: #7899a3; } button.suggested-action { background: #a9e0ef; color: #081013; border-color: #a9e0ef; } button.suggested-action:hover { background: #c6edf5; } .preview, .thumb { border: 1px solid #202b30; } .row-title { font-weight: 600; color: #dce8eb; } flowbox { padding: 2px; } flowboxchild { padding: 4px; border: 1px solid #1b2428; background: #0d1113; } flowboxchild:hover { border-color: #334950; } flowboxchild:selected { border-color: #a9e0ef; background: #111b1f; } textview { background: #050607; color: #d1e1e6; border: 1px solid #202b30; } scrolledwindow { border: 1px solid #172024; }", -1, NULL); gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); g_object_unref(css);
}

static GtkWidget *build_ui(App *app) {
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); app->notebook = gtk_notebook_new(); gtk_notebook_set_tab_pos(GTK_NOTEBOOK(app->notebook), GTK_POS_TOP); gtk_box_pack_start(GTK_BOX(root), app->notebook, TRUE, TRUE, 0);

    GtkWidget *local_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(local_page), 24);
    GtkWidget *brand = gtk_label_new("My wallpapers"); gtk_widget_set_halign(brand, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(brand), "brand"); gtk_box_pack_start(GTK_BOX(local_page), brand, FALSE, FALSE, 0);
    GtkWidget *hint = gtk_label_new("Choose a shader from the grid. Your selected shader previews below."); gtk_widget_set_halign(hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(hint), "muted"); gtk_box_pack_start(GTK_BOX(local_page), hint, FALSE, FALSE, 3);
    GtkWidget *local_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(local_scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC); gtk_widget_set_vexpand(local_scroll, TRUE); app->local_list = gtk_flow_box_new(); gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(app->local_list), GTK_SELECTION_BROWSE); gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(app->local_list), TRUE); gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(app->local_list), 4); gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(app->local_list), 2); gtk_container_add(GTK_CONTAINER(local_scroll), app->local_list); gtk_box_pack_start(GTK_BOX(local_page), local_scroll, TRUE, TRUE, 12); g_signal_connect(app->local_list, "child-activated", G_CALLBACK(local_row_activated), app);
    GtkWidget *preview_label = gtk_label_new("SELECTED"); gtk_widget_set_halign(preview_label, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(preview_label), "section"); gtk_box_pack_start(GTK_BOX(local_page), preview_label, FALSE, FALSE, 0);
    GtkWidget *selected_strip = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12); app->preview = gtk_gl_area_new(); gtk_widget_set_size_request(app->preview, 260, 112); gtk_style_context_add_class(gtk_widget_get_style_context(app->preview), "preview"); gtk_box_pack_start(GTK_BOX(selected_strip), app->preview, FALSE, FALSE, 0); g_signal_connect(app->preview, "realize", G_CALLBACK(preview_realize), app); g_signal_connect(app->preview, "render", G_CALLBACK(preview_render), app);
    GtkWidget *controls = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7); GtkWidget *apply = gtk_button_new_with_label("Set as current wallpaper"); gtk_style_context_add_class(gtk_widget_get_style_context(apply), "suggested-action"); gtk_widget_set_halign(apply, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(controls), apply, FALSE, FALSE, 0); g_signal_connect(apply, "clicked", G_CALLBACK(apply_clicked), app); GtkWidget *stop = gtk_button_new_with_label("Stop wallpaper"); gtk_widget_set_halign(stop, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(controls), stop, FALSE, FALSE, 0); g_signal_connect(stop, "clicked", G_CALLBACK(stop_clicked), app); GtkWidget *delete_button = gtk_button_new_with_label("Delete selected"); gtk_widget_set_halign(delete_button, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(controls), delete_button, FALSE, FALSE, 0); g_signal_connect(delete_button, "clicked", G_CALLBACK(delete_selected_clicked), app); GtkWidget *open = gtk_button_new_with_label("Open on Shadertoy"); gtk_widget_set_halign(open, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(controls), open, FALSE, FALSE, 0); g_signal_connect(open, "clicked", G_CALLBACK(open_clicked), app); gtk_box_pack_start(GTK_BOX(selected_strip), controls, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(local_page), selected_strip, FALSE, FALSE, 5);
    app->status = gtk_label_new("Choose a shader to begin"); gtk_widget_set_halign(app->status, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(app->status), "muted"); gtk_box_pack_start(GTK_BOX(local_page), app->status, FALSE, FALSE, 0); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), local_page, gtk_label_new("Wallpapers"));

    GtkWidget *toy_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(toy_page), 24); GtkWidget *toy_title = gtk_label_new("Shadertoy"); gtk_widget_set_halign(toy_title, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(toy_title), "brand"); gtk_box_pack_start(GTK_BOX(toy_page), toy_title, FALSE, FALSE, 0); GtkWidget *toy_hint = gtk_label_new("Browse Shadertoy directly. Use the injected import button on a shader page."); gtk_widget_set_halign(toy_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(toy_hint), "muted"); gtk_box_pack_start(GTK_BOX(toy_page), toy_hint, FALSE, FALSE, 3);
#ifdef HAVE_WEBKIT
    GtkWidget *browser_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6); GtkWidget *back = gtk_button_new_with_label("‹"); GtkWidget *forward = gtk_button_new_with_label("›"); app->browser_url = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app->browser_url), "https://www.shadertoy.com"); GtkWidget *go = gtk_button_new_with_label("Go"); gtk_box_pack_start(GTK_BOX(browser_bar), back, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), forward, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), app->browser_url, TRUE, TRUE, 0); gtk_box_pack_start(GTK_BOX(browser_bar), go, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(toy_page), browser_bar, FALSE, FALSE, 8);
    WebKitUserContentManager *content_manager = webkit_user_content_manager_new(); g_signal_connect(content_manager, "script-message-received::glacier", G_CALLBACK(browser_message), app); webkit_user_content_manager_register_script_message_handler(content_manager, "glacier"); WebKitUserScript *script = webkit_user_script_new(browser_import_script, WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL); webkit_user_content_manager_add_script(content_manager, script); webkit_user_script_unref(script); app->browser = webkit_web_view_new_with_user_content_manager(content_manager); webkit_web_view_load_uri(WEBKIT_WEB_VIEW(app->browser), "https://www.shadertoy.com"); gtk_widget_set_vexpand(app->browser, TRUE); gtk_box_pack_start(GTK_BOX(toy_page), app->browser, TRUE, TRUE, 8); g_signal_connect(back, "clicked", G_CALLBACK(browser_back), app); g_signal_connect(forward, "clicked", G_CALLBACK(browser_forward), app); g_signal_connect(go, "clicked", G_CALLBACK(browser_load), app); g_signal_connect(app->browser_url, "activate", G_CALLBACK(browser_load), app); g_signal_connect(app->browser, "load-changed", G_CALLBACK(browser_load_changed), app);
#else
    GtkWidget *browser_missing = gtk_label_new("Embedded browser unavailable in this build. Install libwebkit2gtk-4.0-dev, rebuild, and this tab will include an Import shader button on Shadertoy pages."); gtk_label_set_line_wrap(GTK_LABEL(browser_missing), TRUE); gtk_widget_set_halign(browser_missing, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), browser_missing, FALSE, FALSE, 10); GtkWidget *external = gtk_button_new_with_label("Open Shadertoy in browser"); gtk_widget_set_halign(external, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), external, FALSE, FALSE, 5); g_signal_connect(external, "clicked", G_CALLBACK(browser_external_clicked), app);
#endif
    gtk_box_pack_start(GTK_BOX(toy_page), section_label("PASTE SHADER CODE"), FALSE, FALSE, 0); app->code_view = gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(app->code_view), GTK_WRAP_NONE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(app->code_view), TRUE); GtkWidget *code_scroll = gtk_scrolled_window_new(NULL, NULL); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(code_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); gtk_widget_set_size_request(code_scroll, -1, 130); gtk_container_add(GTK_CONTAINER(code_scroll), app->code_view); gtk_box_pack_start(GTK_BOX(toy_page), code_scroll, FALSE, FALSE, 5); GtkWidget *paste_button = gtk_button_new_with_label("Preview pasted shader"); gtk_widget_set_halign(paste_button, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(toy_page), paste_button, FALSE, FALSE, 0); g_signal_connect(paste_button, "clicked", G_CALLBACK(load_pasted_clicked), app); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), toy_page, gtk_label_new("Shadertoy"));

    GtkWidget *settings_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0); gtk_container_set_border_width(GTK_CONTAINER(settings_page), 36); GtkWidget *settings_title = gtk_label_new("Settings"); gtk_widget_set_halign(settings_title, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(settings_title), "brand"); gtk_box_pack_start(GTK_BOX(settings_page), settings_title, FALSE, FALSE, 0); GtkWidget *settings_hint = gtk_label_new("Connection and wallpaper behavior"); gtk_widget_set_halign(settings_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(settings_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), settings_hint, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(settings_page), section_label("OPTIONAL SHADERTOY API KEY"), FALSE, FALSE, 14); app->api_key = gtk_entry_new(); gtk_entry_set_visibility(GTK_ENTRY(app->api_key), FALSE); gtk_entry_set_placeholder_text(GTK_ENTRY(app->api_key), "Paste your Shadertoy app key"); const char *env = g_getenv("SHADERTOY_API_KEY"); if (env) gtk_entry_set_text(GTK_ENTRY(app->api_key), env); gtk_box_pack_start(GTK_BOX(settings_page), app->api_key, FALSE, FALSE, 0); GtkWidget *key_hint = gtk_label_new("The embedded browser imports shaders without this key. The key is only for the legacy REST search."); gtk_widget_set_halign(key_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(key_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), key_hint, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(settings_page), section_label("WALLPAPER FPS"), FALSE, FALSE, 18); app->fps = gtk_combo_box_text_new(); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "24"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "30"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "45"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->fps), "60"); gtk_combo_box_set_active(GTK_COMBO_BOX(app->fps), 1); gtk_widget_set_halign(app->fps, GTK_ALIGN_START); gtk_box_pack_start(GTK_BOX(settings_page), app->fps, FALSE, FALSE, 0); GtkWidget *mint_hint = gtk_label_new("X11 mode · wallpaper is shared across all workspaces"); gtk_widget_set_halign(mint_hint, GTK_ALIGN_START); gtk_style_context_add_class(gtk_widget_get_style_context(mint_hint), "muted"); gtk_box_pack_start(GTK_BOX(settings_page), mint_hint, FALSE, FALSE, 22); gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), settings_page, gtk_label_new("Settings"));
    return root;
}

static volatile sig_atomic_t wallpaper_running = 1;
static void on_signal(int sig) { (void)sig; wallpaper_running = 0; }

static void x11_property(Display *display, Window window, Atom property, Atom type, const unsigned long *values, int count) { XChangeProperty(display, window, property, type, 32, PropModeReplace, (const unsigned char *)values, count); }

typedef struct {
    Display *display; Window root; int width, height;
    Pixmap pixmap; GC gc; XImage *image;
    Atom xroot_atom, esetroot_atom; Pixmap old_xroot, old_esetroot;
} RootMirror;

static Pixmap root_pixmap_property(Display *display, Window root, Atom property) {
    Atom actual_type; int format; unsigned long count, remaining; unsigned char *data = NULL; Pixmap result = 0;
    if (XGetWindowProperty(display, root, property, 0, 1, False, AnyPropertyType, &actual_type, &format, &count, &remaining, &data) == Success && data && format == 32 && count) result = (Pixmap)((unsigned long *)data)[0];
    if (data) XFree(data);
    return result;
}

static gboolean root_mirror_begin(RootMirror *mirror, Display *display, Window root, int width, int height) {
    memset(mirror, 0, sizeof(*mirror)); mirror->display = display; mirror->root = root; mirror->width = width; mirror->height = height;
    mirror->xroot_atom = XInternAtom(display, "_XROOTPMAP_ID", False); mirror->esetroot_atom = XInternAtom(display, "ESETROOT_PMAP_ID", False);
    mirror->old_xroot = root_pixmap_property(display, root, mirror->xroot_atom); mirror->old_esetroot = root_pixmap_property(display, root, mirror->esetroot_atom);
    mirror->pixmap = XCreatePixmap(display, root, (unsigned)width, (unsigned)height, (unsigned)DefaultDepth(display, DefaultScreen(display)));
    mirror->gc = XCreateGC(display, mirror->pixmap, 0, NULL); unsigned char *pixels = g_malloc0((gsize)width * (gsize)height * 4);
    mirror->image = XCreateImage(display, DefaultVisual(display, DefaultScreen(display)), (unsigned)DefaultDepth(display, DefaultScreen(display)), ZPixmap, 0, (char *)pixels, (unsigned)width, (unsigned)height, 32, 0);
    if (!mirror->pixmap || !mirror->gc || !mirror->image) return FALSE;
    unsigned long p = (unsigned long)mirror->pixmap; x11_property(display, root, mirror->xroot_atom, XA_PIXMAP, &p, 1); x11_property(display, root, mirror->esetroot_atom, XA_PIXMAP, &p, 1); XSetWindowBackgroundPixmap(display, root, mirror->pixmap); return TRUE;
}

static void root_mirror_update(RootMirror *mirror) {
    if (!mirror->image) return;
    glReadPixels(0, 0, mirror->width, mirror->height, GL_BGRA, GL_UNSIGNED_BYTE, mirror->image->data);
    int stride = mirror->image->bytes_per_line; char *row = g_malloc((gsize)stride);
    for (int y = 0; y < mirror->height / 2; y++) { char *top = mirror->image->data + y * stride; char *bottom = mirror->image->data + (mirror->height - y - 1) * stride; memcpy(row, top, (gsize)stride); memcpy(top, bottom, (gsize)stride); memcpy(bottom, row, (gsize)stride); }
    g_free(row); XPutImage(mirror->display, mirror->pixmap, mirror->gc, mirror->image, 0, 0, 0, 0, (unsigned)mirror->width, (unsigned)mirror->height); XFlush(mirror->display);
}

static void root_mirror_end(RootMirror *mirror) {
    if (!mirror->display) return;
    if (mirror->old_xroot) { unsigned long p = (unsigned long)mirror->old_xroot; x11_property(mirror->display, mirror->root, mirror->xroot_atom, XA_PIXMAP, &p, 1); XSetWindowBackgroundPixmap(mirror->display, mirror->root, mirror->old_xroot); } else XDeleteProperty(mirror->display, mirror->root, mirror->xroot_atom);
    if (mirror->old_esetroot) { unsigned long p = (unsigned long)mirror->old_esetroot; x11_property(mirror->display, mirror->root, mirror->esetroot_atom, XA_PIXMAP, &p, 1); } else XDeleteProperty(mirror->display, mirror->root, mirror->esetroot_atom);
    if (mirror->image) XDestroyImage(mirror->image);
    if (mirror->gc) XFreeGC(mirror->display, mirror->gc);
    if (mirror->pixmap) XFreePixmap(mirror->display, mirror->pixmap);
    XFlush(mirror->display);
}

static int wallpaper_main(const char *path, int fps) {
    char *source = read_file(path); if (!source) { fprintf(stderr, "glacier-mint: cannot read %s\n", path); return 1; }
    Display *display = XOpenDisplay(NULL); if (!display) { fprintf(stderr, "glacier-mint: DISPLAY is unavailable\n"); g_free(source); return 1; }
    int screen = DefaultScreen(display); Window root = RootWindow(display, screen); int width = DisplayWidth(display, screen), height = DisplayHeight(display, screen);
    int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None }; XVisualInfo *visual = glXChooseVisual(display, screen, attrs);
    if (!visual) { fprintf(stderr, "glacier-mint: no GLX visual\n"); XCloseDisplay(display); g_free(source); return 1; }
    Colormap cmap = XCreateColormap(display, root, visual->visual, AllocNone); XSetWindowAttributes swa = {0}; swa.colormap = cmap; swa.border_pixel = 0; swa.background_pixel = 0;
    Window window = XCreateWindow(display, root, 0, 0, width, height, 0, visual->depth, InputOutput, visual->visual, CWColormap | CWBorderPixel | CWBackPixel, &swa);
    Atom desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False), type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False); x11_property(display, window, type, XA_ATOM, (unsigned long *)&desktop, 1);
    Atom below = XInternAtom(display, "_NET_WM_STATE_BELOW", False), sticky = XInternAtom(display, "_NET_WM_STATE_STICKY", False), skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False), skip_pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False), state = XInternAtom(display, "_NET_WM_STATE", False); unsigned long states[] = {(unsigned long)below, (unsigned long)sticky, (unsigned long)skip_taskbar, (unsigned long)skip_pager}; x11_property(display, window, state, XA_ATOM, states, 4);
    /* EWMH desktop 0xFFFFFFFF means every workspace, not only workspace 1. */
    Atom desktop_number = XInternAtom(display, "_NET_WM_DESKTOP", False); unsigned long all_workspaces = 0xFFFFFFFFUL; x11_property(display, window, desktop_number, XA_CARDINAL, &all_workspaces, 1);
    XStoreName(display, window, "Glacier Mint wallpaper"); XMapWindow(display, window); XLowerWindow(display, window); XFlush(display);
    GLXContext context = glXCreateContext(display, visual, NULL, True); XFree(visual); if (!context || !glXMakeCurrent(display, window, context)) { fprintf(stderr, "glacier-mint: could not create GL context\n"); XDestroyWindow(display, window); XCloseDisplay(display); g_free(source); return 1; }
    char *error = NULL; GLuint program = make_program(source, &error); g_free(source); if (!program) { fprintf(stderr, "glacier-mint: shader error: %s\n", error ? error : "unknown"); g_free(error); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 1; }
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao); RootMirror mirror; gboolean mirror_ready = root_mirror_begin(&mirror, display, root, width, height); signal(SIGTERM, on_signal); signal(SIGINT, on_signal); gint64 started = g_get_monotonic_time(); int frame = 0; double period = 1.0 / MAX(fps, 1); gint64 next = started;
    while (wallpaper_running) { gint64 now = g_get_monotonic_time(); float elapsed = (float)(now - started) / 1000000.0f; glViewport(0, 0, width, height); glUseProgram(program); set_uniforms(program, width, height, elapsed, frame++); glDrawArrays(GL_TRIANGLES, 0, 3); if (mirror_ready && frame % 6 == 0) root_mirror_update(&mirror); glXSwapBuffers(display, window); next += (gint64)(period * 1000000.0); gint64 wait = next - g_get_monotonic_time(); if (wait > 0) g_usleep((gulong)wait); else next = g_get_monotonic_time(); }
    if (mirror_ready) root_mirror_end(&mirror);
    glDeleteProgram(program); glXMakeCurrent(display, None, NULL); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--wallpaper")) { int fps = 30; for (int i = 3; i + 1 < argc; i++) if (!strcmp(argv[i], "--fps")) fps = atoi(argv[i + 1]); return wallpaper_main(argv[2], fps); }
    gtk_init(&argc, &argv); curl_global_init(CURL_GLOBAL_DEFAULT); add_css(); App app = {0}; app.started = g_get_monotonic_time(); app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(app.window), APP_NAME); gtk_window_set_default_size(GTK_WINDOW(app.window), 1120, 960); gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER); g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_container_add(GTK_CONTAINER(app.window), build_ui(&app)); populate_local(&app); g_timeout_add(16, (GSourceFunc)tick_preview, &app); gtk_widget_show_all(app.window); g_idle_add((GSourceFunc)restore_last_shader, &app); gtk_main(); if (app.program) glDeleteProgram(app.program); g_free(app.shader_code); g_free(app.shader_title); g_free(app.shader_id); g_free(app.shader_path); curl_global_cleanup(); return 0;
}
