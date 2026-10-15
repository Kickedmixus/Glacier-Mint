#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void load_channel_types(const char *shader_path, int types[4]) {
    for (int i = 0; i < 4; i++) types[i] = CHANNEL_2D;
    if (!shader_path) return;
    char *folder = g_path_get_dirname(shader_path); char *manifest = g_build_filename(folder, "channels.json", NULL); JsonParser *parser = json_parser_new();
    if (json_parser_load_from_file(parser, manifest, NULL) && JSON_NODE_HOLDS_ARRAY(json_parser_get_root(parser))) {
        JsonArray *channels = json_node_get_array(json_parser_get_root(parser));
        for (guint i = 0; i < json_array_get_length(channels); i++) {
            JsonObject *channel = json_array_get_object_element(channels, i); if (!channel) continue;
            JsonNode *index_node = json_object_get_member(channel, "channel"), *type_node = json_object_get_member(channel, "type");
            if (!index_node || !type_node || !JSON_NODE_HOLDS_VALUE(index_node) || !JSON_NODE_HOLDS_VALUE(type_node)) continue;
            int index = (int)json_node_get_int(index_node); const char *type = json_node_get_string(type_node);
            if (index >= 0 && index < 4 && type && (!g_ascii_strcasecmp(type, "cubemap") || !g_ascii_strcasecmp(type, "cube"))) types[index] = CHANNEL_CUBE;
        }
    }
    g_object_unref(parser); g_free(manifest); g_free(folder);
}

static GLuint black_texture(GLenum target) {
    GLuint texture = 0; unsigned char pixel[4] = {0, 0, 0, 255};
    glGenTextures(1, &texture); glBindTexture(target, texture);
    glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(target, GL_TEXTURE_WRAP_S, target == GL_TEXTURE_CUBE_MAP ? GL_CLAMP_TO_EDGE : GL_REPEAT); glTexParameteri(target, GL_TEXTURE_WRAP_T, target == GL_TEXTURE_CUBE_MAP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    if (target == GL_TEXTURE_CUBE_MAP) {
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        for (int i = 0; i < 6; i++) glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    } else glTexImage2D(target, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return texture;
}

void free_shader_channels(GLuint textures[4]) {
    for (int i = 0; i < 4; i++) {
        if (textures[i]) glDeleteTextures(1, &textures[i]);
        textures[i] = 0;
    }
}

static void upload_pixbuf(GLenum face, GdkPixbuf *pixbuf) {
    int channels = gdk_pixbuf_get_n_channels(pixbuf); glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(face, 0, channels == 4 ? GL_RGBA : GL_RGB, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), 0, channels == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, gdk_pixbuf_get_pixels(pixbuf));
}

gboolean load_shader_channels(const char *shader_path, GLuint textures[4], int widths[4], int heights[4], int types[4]) {
    for (int i = 0; i < 4; i++) { textures[i] = 0; widths[i] = 1; heights[i] = 1; types[i] = CHANNEL_2D; }
    if (!shader_path) return FALSE;
    load_channel_types(shader_path, types);
    char *folder = g_path_get_dirname(shader_path); gboolean loaded = FALSE;
    for (int i = 0; i < 4; i++) {
        char *filename = g_strdup_printf("channel%d", i); char *path = g_build_filename(folder, filename, NULL); g_free(filename);
        GError *error = NULL; GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error); g_clear_error(&error);
        if (types[i] == CHANNEL_CUBE) {
            if (!pixbuf) { textures[i] = black_texture(GL_TEXTURE_CUBE_MAP); g_free(path); continue; }
            glGenTextures(1, &textures[i]); glBindTexture(GL_TEXTURE_CUBE_MAP, textures[i]); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
            for (int face = 0; face < 6; face++) {
                GdkPixbuf *face_pixbuf = pixbuf;
                if (face > 0) { char *face_name = g_strdup_printf("channel%d_%d", i, face); char *face_path = g_build_filename(folder, face_name, NULL); g_free(face_name); face_pixbuf = gdk_pixbuf_new_from_file(face_path, NULL); g_free(face_path); if (!face_pixbuf) face_pixbuf = pixbuf; }
                upload_pixbuf(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, face_pixbuf); if (face_pixbuf != pixbuf) g_object_unref(face_pixbuf);
            }
            widths[i] = gdk_pixbuf_get_width(pixbuf); heights[i] = gdk_pixbuf_get_height(pixbuf); loaded = TRUE; g_object_unref(pixbuf);
        } else if (!pixbuf) { textures[i] = black_texture(GL_TEXTURE_2D); g_free(path); continue; }
        else { widths[i] = gdk_pixbuf_get_width(pixbuf); heights[i] = gdk_pixbuf_get_height(pixbuf); glGenTextures(1, &textures[i]); glBindTexture(GL_TEXTURE_2D, textures[i]); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT); upload_pixbuf(GL_TEXTURE_2D, pixbuf); loaded = TRUE; g_object_unref(pixbuf); }
        g_free(path);
    }
    g_free(folder); return loaded;
}

void bind_shader_channels(GLuint program, GLuint textures[4], const int widths[4], const int heights[4], const int types[4]) {
    for (int i = 0; i < 4; i++) {
        char name[32]; snprintf(name, sizeof(name), "iChannel%d", i); GLint sampler = glGetUniformLocation(program, name);
        if (sampler >= 0) glUniform1i(sampler, i);
        snprintf(name, sizeof(name), "iChannelResolution[%d]", i); GLint resolution = glGetUniformLocation(program, name);
        if (resolution >= 0) glUniform3f(resolution, (float)widths[i], (float)heights[i], 1.0f);
        glActiveTexture(GL_TEXTURE0 + i); glBindTexture(types[i] == CHANNEL_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, textures[i]);
    }
    glActiveTexture(GL_TEXTURE0);
}

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
    "out vec4 glacierColor;\n";

static const char *fragment_footer =
    "void main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); glacierColor = vec4(c.rgb, 1.0); }\n";

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

static GLuint make_program_with_types(const char *user_source, const int types[4], char **error) {
    char *normalized = normalize_shader(user_source);
    GString *frag = g_string_new(fragment_header);
    for (int i = 0; i < 4; i++) g_string_append_printf(frag, "uniform sampler%s iChannel%d;\n", types[i] == CHANNEL_CUBE ? "Cube" : "2D", i);
    g_string_append(frag, normalized); g_string_append(frag, fragment_footer);
    g_free(normalized);
    GLuint vs = compile_one(GL_VERTEX_SHADER, vertex_shader, error);
    if (!vs) { g_string_free(frag, TRUE); return 0; }
    GLuint fs = compile_one(GL_FRAGMENT_SHADER, frag->str, error);
    g_string_free(frag, TRUE);
    if (!fs) { glDeleteShader(vs); return 0; }
    GLuint program = glCreateProgram(); GLint ok = 0, length = 0;
    glAttachShader(program, vs); glAttachShader(program, fs);
    glLinkProgram(program); glDeleteShader(vs); glDeleteShader(fs);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok) return program;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    char *log = g_malloc0((gsize)MAX(length, 1));
    glGetProgramInfoLog(program, length, NULL, log);
    if (error) *error = log; else g_free(log);
    glDeleteProgram(program); return 0;
}

GLuint make_program(const char *user_source, char **error) {
    int types[4] = {CHANNEL_2D, CHANNEL_2D, CHANNEL_2D, CHANNEL_2D}; return make_program_with_types(user_source, types, error);
}

GLuint make_program_for_shader(const char *user_source, const char *shader_path, char **error) {
    int types[4] = {CHANNEL_2D, CHANNEL_2D, CHANNEL_2D, CHANNEL_2D}; load_channel_types(shader_path, types); return make_program_with_types(user_source, types, error);
}

void set_uniforms(GLuint program, float width, float height, float time_s, int frame) {
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

void preview_realize(GtkGLArea *area, App *app) {
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) return;
    glGenVertexArrays(1, &app->vao); glBindVertexArray(app->vao);
    app->preview_ready = TRUE;
}

gboolean preview_render(GtkGLArea *area, GdkGLContext *context, App *app) {
    (void)context;
    if (!app->preview_ready || !app->program) return TRUE;
    int width = gtk_widget_get_allocated_width(GTK_WIDGET(area));
    int height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    float elapsed = (float)(g_get_monotonic_time() - app->started) / 1000000.0f;
    glViewport(0, 0, width, height); glClearColor(0.025f, 0.035f, 0.06f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(app->program); set_uniforms(app->program, width, height, elapsed, (int)(elapsed * 60)); bind_shader_channels(app->program, app->channel_textures, app->channel_widths, app->channel_heights, app->channel_types);
    glBindVertexArray(app->vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

gboolean tick_preview(App *app) { gtk_gl_area_queue_render(GTK_GL_AREA(app->preview)); return G_SOURCE_CONTINUE; }

gboolean load_preview(App *app, const char *source, const char *title, const char *id) {
    if (!source) return FALSE;
    gtk_gl_area_make_current(GTK_GL_AREA(app->preview));
    if (gtk_gl_area_get_error(GTK_GL_AREA(app->preview))) return FALSE;
    char *error = NULL; GLuint program = make_program_for_shader(source, app->shader_path, &error);
    if (!program) {
        char *short_error = g_strndup(error ? error : "unknown shader error", 220);
        char *message = g_strdup_printf("Shader error: %s", short_error);
        set_status(app, message); g_free(message); g_free(short_error); g_free(error); return FALSE;
    }
    if (app->program) glDeleteProgram(app->program);
    app->program = program; g_free(app->shader_code); app->shader_code = g_strdup(source);
    g_free(app->shader_title); app->shader_title = g_strdup(title ? title : "Untitled shader");
    g_free(app->shader_id); app->shader_id = g_strdup(id ? id : "");
    free_shader_channels(app->channel_textures); load_shader_channels(app->shader_path, app->channel_textures, app->channel_widths, app->channel_heights, app->channel_types);
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

void local_row_activated(GtkFlowBox *box, GtkFlowBoxChild *row, App *app) {
    (void)box;
    const char *path = g_object_get_data(G_OBJECT(row), "shader-path");
    const char *title = gtk_label_get_text(GTK_LABEL(g_object_get_data(G_OBJECT(row), "shader-title")));
    choose_shader_file(app, path, title);
}

static void thumb_free(ShaderThumb *thumb) { g_free(thumb->source); g_free(thumb->path); g_free(thumb); }

static void thumb_realize(GtkGLArea *area, ShaderThumb *thumb) {
    gtk_gl_area_make_current(area); if (gtk_gl_area_get_error(area)) return;
    char *error = NULL; thumb->program = make_program_for_shader(thumb->source, thumb->path, &error); g_free(error); load_shader_channels(thumb->path, thumb->channel_textures, thumb->channel_widths, thumb->channel_heights, thumb->channel_types); if (thumb->program) glGenVertexArrays(1, &thumb->vao); thumb->started = g_get_monotonic_time();
}

static void thumb_unrealize(GtkGLArea *area, ShaderThumb *thumb) {
    gtk_gl_area_make_current(area); if (gtk_gl_area_get_error(area)) return;
    if (thumb->program) glDeleteProgram(thumb->program);
    thumb->program = 0;
    if (thumb->vao) glDeleteVertexArrays(1, &thumb->vao);
    thumb->vao = 0;
    free_shader_channels(thumb->channel_textures);
}

static gboolean thumb_render(GtkGLArea *area, GdkGLContext *context, ShaderThumb *thumb) {
    (void)context; int width = gtk_widget_get_allocated_width(GTK_WIDGET(area)), height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    glViewport(0, 0, width, height); glClearColor(0.035f, 0.045f, 0.055f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    if (!thumb->program) return TRUE;
    float elapsed = (float)(g_get_monotonic_time() - thumb->started) / 1000000.0f; glUseProgram(thumb->program); set_uniforms(thumb->program, width, height, elapsed, (int)(elapsed * 60)); bind_shader_channels(thumb->program, thumb->channel_textures, thumb->channel_widths, thumb->channel_heights, thumb->channel_types); glBindVertexArray(thumb->vao); glDrawArrays(GL_TRIANGLES, 0, 3); return TRUE;
}

static gboolean thumb_tick(GtkGLArea *area) { if (gtk_widget_get_realized(GTK_WIDGET(area))) gtk_gl_area_queue_render(area); return G_SOURCE_CONTINUE; }

GtkWidget *shader_row(const char *title, const char *subtitle, const char *source, const char *shader_path) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    if (source) {
        GtkWidget *thumb_area = gtk_gl_area_new(); ShaderThumb *thumb = g_new0(ShaderThumb, 1); thumb->source = g_strdup(source); thumb->path = g_strdup(shader_path);
        gtk_widget_set_size_request(thumb_area, 175, 82); gtk_style_context_add_class(gtk_widget_get_style_context(thumb_area), "thumb"); g_object_set_data_full(G_OBJECT(thumb_area), "shader-thumb", thumb, (GDestroyNotify)thumb_free);
        g_signal_connect(thumb_area, "realize", G_CALLBACK(thumb_realize), thumb); g_signal_connect(thumb_area, "unrealize", G_CALLBACK(thumb_unrealize), thumb); g_signal_connect(thumb_area, "render", G_CALLBACK(thumb_render), thumb); g_timeout_add(33, (GSourceFunc)thumb_tick, thumb_area); gtk_box_pack_start(GTK_BOX(box), thumb_area, FALSE, FALSE, 0);
    }
    GtkWidget *name = gtk_label_new(title); GtkWidget *sub = gtk_label_new(subtitle ? subtitle : "");
    gtk_widget_set_halign(name, GTK_ALIGN_START); gtk_widget_set_halign(sub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "row-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "row-subtitle");
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(box), "shader-title", name); return box;
}
