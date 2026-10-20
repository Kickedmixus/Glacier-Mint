#include "app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { App *app; char *type, *name, *default_value; GtkWidget *entry; } ShaderConstant;

static gboolean constants_update_timeout(gpointer data);
static void constant_value_changed(GtkEditable *editable, App *app);
static void reset_one_constant(GtkButton *button, ShaderConstant *constant);
static void load_widget_program(App *app);

static void shader_constant_free(ShaderConstant *constant) {
    g_free(constant->type); g_free(constant->name); g_free(constant->default_value); g_free(constant);
}

static ShaderConstant *find_shader_constant(App *app, const char *name) {
    if (!app->constants) return NULL;
    for (guint i = 0; i < app->constants->len; i++) { ShaderConstant *constant = g_ptr_array_index(app->constants, i); if (!strcmp(constant->name, name)) return constant; }
    return NULL;
}

static void clear_constant_controls(App *app) {
    if (app->constants_update_id) { g_source_remove(app->constants_update_id); app->constants_update_id = 0; }
    if (app->constants_box) { GList *children = gtk_container_get_children(GTK_CONTAINER(app->constants_box)); for (GList *l = children; l; l = l->next) gtk_widget_destroy(l->data); g_list_free(children); }
    if (!app->constants) app->constants = g_ptr_array_new_with_free_func((GDestroyNotify)shader_constant_free); else g_ptr_array_set_size(app->constants, 0);
}

static void rebuild_constant_controls(App *app, const char *source) {
    clear_constant_controls(app); g_free(app->constants_source); app->constants_source = g_strdup(source);
    GRegex *regex = g_regex_new("^[[:space:]]*const[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*([^;\\n]+)", G_REGEX_MULTILINE, 0, NULL); GMatchInfo *matches = NULL; g_regex_match(regex, source, 0, &matches);
    while (g_match_info_matches(matches)) {
        char *type = g_match_info_fetch(matches, 1), *name = g_match_info_fetch(matches, 2), *value = g_match_info_fetch(matches, 3); g_strstrip(value);
        if (!find_shader_constant(app, name)) {
            ShaderConstant *constant = g_new0(ShaderConstant, 1); constant->app = app; constant->type = type; constant->name = name; constant->default_value = value; g_ptr_array_add(app->constants, constant);
            if (app->constants_box) { GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0); GtkWidget *label = gtk_label_new(NULL); char *label_text = g_strdup_printf("%s (%s)", name, type); gtk_label_set_text(GTK_LABEL(label), label_text); g_free(label_text); gtk_widget_set_halign(label, GTK_ALIGN_START); gtk_widget_set_hexpand(row, TRUE); gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0); constant->entry = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(constant->entry), value); gtk_entry_set_width_chars(GTK_ENTRY(constant->entry), 12); g_signal_connect(constant->entry, "changed", G_CALLBACK(constant_value_changed), app); gtk_box_pack_start(GTK_BOX(row), constant->entry, FALSE, FALSE, 0); GtkWidget *reset = gtk_button_new_with_label("Reset"); gtk_widget_set_tooltip_text(reset, "Reset this value"); g_signal_connect(reset, "clicked", G_CALLBACK(reset_one_constant), constant); gtk_box_pack_start(GTK_BOX(row), reset, FALSE, FALSE, 0); guint index = app->constants->len - 1; gtk_grid_attach(GTK_GRID(app->constants_box), row, index % 2, index / 2, 1, 1); }
        } else { g_free(type); g_free(name); g_free(value); }
        g_match_info_next(matches, NULL);
    }
    g_match_info_free(matches); g_regex_unref(regex);
    if (app->constants_box && app->constants->len == 0) { GtkWidget *empty = gtk_label_new("No editable const values"); gtk_style_context_add_class(gtk_widget_get_style_context(empty), "muted"); gtk_widget_set_halign(empty, GTK_ALIGN_START); gtk_grid_attach(GTK_GRID(app->constants_box), empty, 0, 0, 2, 1); }
    if (app->constants_box) gtk_widget_show_all(app->constants_box);
}

static char *constant_source_with_values(App *app) {
    if (!app->constants_source) return NULL;
    GRegex *regex = g_regex_new("^[[:space:]]*const[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*=[[:space:]]*([^;\\n]+)", G_REGEX_MULTILINE, 0, NULL); GString *result = g_string_new(NULL); char **lines = g_strsplit(app->constants_source, "\\n", -1);
    for (gsize i = 0; lines[i]; i++) { GMatchInfo *matches = NULL; if (g_regex_match(regex, lines[i], 0, &matches)) { char *name = g_match_info_fetch(matches, 2); ShaderConstant *constant = find_shader_constant(app, name); if (constant && constant->entry) { const char *equals = strchr(lines[i], '='); const char *semicolon = strrchr(lines[i], ';'); if (equals && semicolon && semicolon > equals) { g_string_append_len(result, lines[i], (gssize)(equals - lines[i] + 1)); g_string_append(result, gtk_entry_get_text(GTK_ENTRY(constant->entry))); g_string_append(result, semicolon); } else g_string_append(result, lines[i]); } else g_string_append(result, lines[i]); g_free(name); } else g_string_append(result, lines[i]); if (lines[i + 1]) g_string_append_c(result, '\n'); if (matches) g_match_info_free(matches); }
    g_strfreev(lines); g_regex_unref(regex); return g_string_free(result, FALSE);
}

gboolean update_shader_source(App *app, const char *source) {
    if (!app->preview || !gtk_widget_get_realized(app->preview)) return FALSE;
    gtk_gl_area_make_current(GTK_GL_AREA(app->preview)); if (gtk_gl_area_get_error(GTK_GL_AREA(app->preview))) return FALSE;
    char *error = NULL; GLuint program = make_program_for_shader(source, app->shader_path, &error); if (!program) { char *message = g_strdup_printf("Shader error: %s", error ? error : "unknown"); set_status(app, message); g_free(message); g_free(error); return FALSE; }
    if (app->shader_path && g_file_test(app->shader_path, G_FILE_TEST_IS_REGULAR)) { GError *write_error = NULL; if (!g_file_set_contents(app->shader_path, source, -1, &write_error)) { set_status(app, write_error->message); g_error_free(write_error); glDeleteProgram(program); return FALSE; } }
    if (app->program) glDeleteProgram(app->program);
    app->program = program; shader_uniforms_init(&app->uniforms, app->program);
    g_free(app->shader_code); app->shader_code = g_strdup(source);
    char *last_shader = data_path("last.frag"); g_file_set_contents(last_shader, source, -1, NULL); g_free(last_shader); char *message = g_strdup_printf("Updated · %s", app->shader_title ? app->shader_title : "Shader"); set_status(app, message); g_free(message); gtk_gl_area_queue_render(GTK_GL_AREA(app->preview)); load_widget_program(app); return TRUE;
}

void apply_constants_clicked(GtkButton *button, App *app) {
    (void)button; if (app->constants_update_id) { g_source_remove(app->constants_update_id); app->constants_update_id = 0; } char *source = constant_source_with_values(app); if (source) { update_shader_source(app, source); g_free(source); }
}

void reset_constants_clicked(GtkButton *button, App *app) {
    (void)button; if (!app->constants) return; for (guint i = 0; i < app->constants->len; i++) { ShaderConstant *constant = g_ptr_array_index(app->constants, i); if (constant->entry) gtk_entry_set_text(GTK_ENTRY(constant->entry), constant->default_value); } apply_constants_clicked(NULL, app);
}

static gboolean constants_update_timeout(gpointer data) { App *app = data; app->constants_update_id = 0; apply_constants_clicked(NULL, app); return G_SOURCE_REMOVE; }

static void constant_value_changed(GtkEditable *editable, App *app) {
    (void)editable; if (app->constants_update_id) g_source_remove(app->constants_update_id); app->constants_update_id = g_timeout_add(300, constants_update_timeout, app);
}

static void reset_one_constant(GtkButton *button, ShaderConstant *constant) {
    (void)button; gtk_entry_set_text(GTK_ENTRY(constant->entry), constant->default_value); apply_constants_clicked(NULL, constant->app);
}

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

static gboolean shader_dsa_available(void) {
    return epoxy_gl_version() >= 45 || epoxy_has_gl_extension("GL_ARB_direct_state_access");
}

static void uniform1i(const ShaderUniforms *uniforms, GLint location, GLint value) {
    if (location < 0) return;
    if (uniforms->use_dsa) glProgramUniform1i(uniforms->program, location, value); else glUniform1i(location, value);
}

static void uniform1f(const ShaderUniforms *uniforms, GLint location, GLfloat value) {
    if (location < 0) return;
    if (uniforms->use_dsa) glProgramUniform1f(uniforms->program, location, value); else glUniform1f(location, value);
}

static void uniform3f(const ShaderUniforms *uniforms, GLint location, GLfloat x, GLfloat y, GLfloat z) {
    if (location < 0) return;
    if (uniforms->use_dsa) glProgramUniform3f(uniforms->program, location, x, y, z); else glUniform3f(location, x, y, z);
}

static void uniform2f(const ShaderUniforms *uniforms, GLint location, GLfloat x, GLfloat y) {
    if (location < 0) return;
    if (uniforms->use_dsa) glProgramUniform2f(uniforms->program, location, x, y); else glUniform2f(location, x, y);
}

static void uniform4f(const ShaderUniforms *uniforms, GLint location, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (location < 0) return;
    if (uniforms->use_dsa) glProgramUniform4f(uniforms->program, location, x, y, z, w); else glUniform4f(location, x, y, z, w);
}

void shader_uniforms_init(ShaderUniforms *uniforms, GLuint program) {
    memset(uniforms, 0, sizeof(*uniforms)); uniforms->program = program; uniforms->use_dsa = shader_dsa_available();
    uniforms->resolution = glGetUniformLocation(program, "iResolution"); uniforms->time = glGetUniformLocation(program, "iTime");
    uniforms->time_delta = glGetUniformLocation(program, "iTimeDelta"); uniforms->frame_rate = glGetUniformLocation(program, "iFrameRate");
    uniforms->frame = glGetUniformLocation(program, "iFrame"); uniforms->beat = glGetUniformLocation(program, "iBeat");
    uniforms->bpm = glGetUniformLocation(program, "iBPM"); uniforms->audio_level = glGetUniformLocation(program, "iAudioLevel");
    uniforms->audio_bass = glGetUniformLocation(program, "iAudioBass"); uniforms->sample_rate = glGetUniformLocation(program, "iSampleRate");
    uniforms->mouse = glGetUniformLocation(program, "iMouse"); uniforms->chroma_key = glGetUniformLocation(program, "iChromaKey");
    uniforms->chroma_smooth = glGetUniformLocation(program, "iChromaSmooth"); uniforms->chroma_opacity = glGetUniformLocation(program, "iChromaOpacity"); uniforms->chroma_preview = glGetUniformLocation(program, "iChromaPreview");
    uniforms->background_sampler = glGetUniformLocation(program, "glacierBackground"); uniforms->background_resolution = glGetUniformLocation(program, "glacierBackgroundResolution"); uniforms->background_enabled = glGetUniformLocation(program, "glacierBackgroundEnabled");
    for (int i = 0; i < 4; i++) {
        char name[40]; g_snprintf(name, sizeof(name), "iChannel%d", i); uniforms->channel[i] = glGetUniformLocation(program, name);
        g_snprintf(name, sizeof(name), "iChannelResolution[%d]", i); uniforms->channel_resolution[i] = glGetUniformLocation(program, name);
        uniforms->channel_widths[i] = uniforms->channel_heights[i] = -1;
    }
}

void shader_set_effects(ShaderUniforms *uniforms, gboolean chroma_enabled, const float key[3], float smoothing, float opacity) {
    float values[4] = { key[0], key[1], key[2], chroma_enabled ? 1.0f : 0.0f };
    if (!uniforms->use_dsa) glUseProgram(uniforms->program);
    if (!uniforms->values_valid || memcmp(uniforms->chroma_key_value, values, sizeof(values)) != 0) {
        uniform4f(uniforms, uniforms->chroma_key, values[0], values[1], values[2], values[3]);
        memcpy(uniforms->chroma_key_value, values, sizeof(values));
    }
    if (!uniforms->values_valid || uniforms->chroma_smooth_value != smoothing) {
        uniform1f(uniforms, uniforms->chroma_smooth, smoothing); uniforms->chroma_smooth_value = smoothing;
    }
    if (!uniforms->values_valid || uniforms->chroma_opacity_value != opacity) {
        uniform1f(uniforms, uniforms->chroma_opacity, opacity); uniforms->chroma_opacity_value = opacity;
    }
}

void shader_set_preview_background(ShaderUniforms *uniforms, gboolean enabled) {
    if (!uniforms->use_dsa) glUseProgram(uniforms->program);
    if (!uniforms->chroma_preview_valid || uniforms->chroma_preview_value != enabled) {
        uniform1f(uniforms, uniforms->chroma_preview, enabled ? 1.0f : 0.0f);
        uniforms->chroma_preview_value = enabled; uniforms->chroma_preview_valid = TRUE;
    }
}

void shader_set_background(ShaderUniforms *uniforms, GLuint texture, int width, int height, gboolean enabled) {
    if (!uniforms->use_dsa) glUseProgram(uniforms->program);
    if (!uniforms->background_valid || uniforms->background_texture != texture || uniforms->background_width != width || uniforms->background_height != height) {
        uniform1i(uniforms, uniforms->background_sampler, 4); uniform2f(uniforms, uniforms->background_resolution, (float)width, (float)height);
        uniforms->background_texture = texture; uniforms->background_width = width; uniforms->background_height = height;
    }
    if (!uniforms->background_valid || uniforms->background_value != enabled) { uniform1f(uniforms, uniforms->background_enabled, enabled ? 1.0f : 0.0f); uniforms->background_value = enabled; }
    if (uniforms->use_dsa) glBindTextureUnit(4, enabled ? texture : 0);
    else { glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, enabled ? texture : 0); glActiveTexture(GL_TEXTURE0); }
    uniforms->background_valid = TRUE;
}

static void app_effect_values(App *app, gboolean *enabled, float key[3], float *smoothing, float *opacity) {
    GdkRGBA color = { 0.0, 1.0, 0.0, 1.0 };
    *enabled = app->chroma_enable && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chroma_enable));
    if (app->chroma_color) gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(app->chroma_color), &color);
    key[0] = (float)color.red; key[1] = (float)color.green; key[2] = (float)color.blue;
    *smoothing = app->chroma_smoothing ? (float)gtk_range_get_value(GTK_RANGE(app->chroma_smoothing)) : 0.06f;
    *opacity = app->chroma_opacity ? (float)gtk_range_get_value(GTK_RANGE(app->chroma_opacity)) / 100.0f : 1.0f;
}

static gboolean preview_pick_color(GtkWidget *widget, GdkEventButton *event, App *app) {
    if (event->button != 1 || !app->preview || !app->chroma_color) return FALSE;
    gtk_gl_area_make_current(GTK_GL_AREA(widget));
    if (gtk_gl_area_get_error(GTK_GL_AREA(widget))) return FALSE;
    int width = gtk_widget_get_allocated_width(widget), height = gtk_widget_get_allocated_height(widget);
    int x = CLAMP((int)event->x, 0, MAX(0, width - 1)), y = CLAMP(height - 1 - (int)event->y, 0, MAX(0, height - 1));
    float pixel[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; glReadPixels(x, y, 1, 1, GL_RGBA, GL_FLOAT, pixel);
    GdkRGBA color = { CLAMP(pixel[0], 0.0f, 1.0f), CLAMP(pixel[1], 0.0f, 1.0f), CLAMP(pixel[2], 0.0f, 1.0f), 1.0 };
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(app->chroma_color), &color);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chroma_enable), TRUE);
    gtk_gl_area_queue_render(GTK_GL_AREA(widget));
    return TRUE;
}

void shader_set_uniforms(ShaderUniforms *uniforms, float width, float height, float time_s, int frame) {
    gboolean all = !uniforms->values_valid;
    if (!uniforms->use_dsa) glUseProgram(uniforms->program);
    if (all || uniforms->resolution_value[0] != width || uniforms->resolution_value[1] != height) {
        uniform3f(uniforms, uniforms->resolution, width, height, 1.0f); uniforms->resolution_value[0] = width; uniforms->resolution_value[1] = height;
    }
    if (all || uniforms->time_value != time_s) {
        uniform1f(uniforms, uniforms->time, time_s); uniforms->time_value = time_s;
        /* iBeat defaults to the shader clock; audio uniforms override it below. */
        uniforms->beat_value = time_s; uniform1f(uniforms, uniforms->beat, time_s);
    }
    if (all) {
        uniform1f(uniforms, uniforms->time_delta, 1.0f / 60.0f); uniform1f(uniforms, uniforms->frame_rate, 60.0f);
        uniform1f(uniforms, uniforms->sample_rate, 44100.0f); uniform4f(uniforms, uniforms->mouse, 0, 0, 0, 0);
        uniforms->beat_value = time_s; uniform1f(uniforms, uniforms->beat, time_s); uniform1f(uniforms, uniforms->bpm, 60.0f);
        uniforms->audio_level_value = uniforms->audio_bass_value = 0.0f; uniform1f(uniforms, uniforms->audio_level, 0.0f); uniform1f(uniforms, uniforms->audio_bass, 0.0f);
    }
    if (all || uniforms->frame_value != frame) { uniform1i(uniforms, uniforms->frame, frame); uniforms->frame_value = frame; }
    uniforms->values_valid = TRUE;
}

void shader_set_audio_uniforms(ShaderUniforms *uniforms, const BeatClock *clock, const BeatState *state) {
    if (!uniforms->use_dsa) glUseProgram(uniforms->program);
    if (!uniforms->audio_valid || uniforms->beat_value != (float)clock->beat) { uniforms->beat_value = (float)clock->beat; uniform1f(uniforms, uniforms->beat, uniforms->beat_value); }
    if (!uniforms->audio_valid || uniforms->bpm_value != (float)clock->bpm) { uniforms->bpm_value = (float)clock->bpm; uniform1f(uniforms, uniforms->bpm, uniforms->bpm_value); }
    if (!uniforms->audio_valid || uniforms->audio_level_value != (float)state->level) { uniforms->audio_level_value = (float)state->level; uniform1f(uniforms, uniforms->audio_level, uniforms->audio_level_value); }
    if (!uniforms->audio_valid || uniforms->audio_bass_value != (float)state->bass) { uniforms->audio_bass_value = (float)state->bass; uniform1f(uniforms, uniforms->audio_bass, uniforms->audio_bass_value); }
    uniforms->audio_valid = TRUE;
}

void bind_shader_channels(ShaderUniforms *uniforms, GLuint textures[4], const int widths[4], const int heights[4], const int types[4]) {
    if (!uniforms->use_dsa) glUseProgram(uniforms->program);
    for (int i = 0; i < 4; i++) {
        if (!uniforms->channels_valid) uniform1i(uniforms, uniforms->channel[i], i);
        if (!uniforms->channels_valid || uniforms->channel_widths[i] != widths[i] || uniforms->channel_heights[i] != heights[i]) {
            uniform3f(uniforms, uniforms->channel_resolution[i], (float)widths[i], (float)heights[i], 1.0f);
            uniforms->channel_widths[i] = widths[i]; uniforms->channel_heights[i] = heights[i];
        }
        if (uniforms->use_dsa) glBindTextureUnit((GLuint)i, textures[i]);
        else { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(types[i] == CHANNEL_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, textures[i]); }
    }
    uniforms->channels_valid = TRUE;
    if (!uniforms->use_dsa) glActiveTexture(GL_TEXTURE0);
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
    "uniform vec4 iChromaKey; uniform float iChromaSmooth; uniform float iChromaOpacity; uniform float iChromaPreview;\n"
    "uniform sampler2D glacierBackground; uniform vec2 glacierBackgroundResolution; uniform float glacierBackgroundEnabled;\n"
    "uniform float iBeat; uniform float iBPM; uniform float iAudioLevel; uniform float iAudioBass;\n"
    "out vec4 glacierColor;\n";

static const char *fragment_footer =
    "void main() { vec4 c = vec4(0.0); mainImage(c, gl_FragCoord.xy); float alpha = 1.0; vec3 renderedColor = c.rgb; if (iChromaKey.a > 0.5) { float d = distance(c.rgb, iChromaKey.rgb); float edge = max(iChromaSmooth, 0.0001); alpha = smoothstep(edge, edge + 0.035, d) * clamp(iChromaOpacity, 0.0, 1.0); if (iChromaPreview > 0.5) { vec2 cell = floor(gl_FragCoord.xy / 28.0); float checker = mod(cell.x + cell.y, 2.0); vec3 grid = checker < 0.5 ? vec3(0.01, 0.0, 0.02) : vec3(0.30, 0.02, 0.36); renderedColor = mix(grid, c.rgb, alpha); alpha = 1.0; } else if (glacierBackgroundEnabled > 0.5) { vec2 uv = vec2(gl_FragCoord.x / max(iResolution.x, 1.0), 1.0 - gl_FragCoord.y / max(iResolution.y, 1.0)); vec3 background = texture(glacierBackground, uv).rgb; renderedColor = mix(background, c.rgb, alpha); alpha = 1.0; } } glacierColor = vec4(renderedColor, alpha); }\n";

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

void preview_realize(GtkGLArea *area, App *app) {
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) return;
    glGenVertexArrays(1, &app->vao); glBindVertexArray(app->vao);
    gtk_widget_add_events(GTK_WIDGET(area), GDK_BUTTON_PRESS_MASK); g_signal_connect(area, "button-press-event", G_CALLBACK(preview_pick_color), app);
    app->preview_ready = TRUE;
    if (app->pending_preview_source) {
        char *source = app->pending_preview_source, *title = app->pending_preview_title, *id = app->pending_preview_id;
        app->pending_preview_source = NULL; app->pending_preview_title = NULL; app->pending_preview_id = NULL;
        load_preview(app, source, title, id); g_free(source); g_free(title); g_free(id);
    }
}

static void load_widget_program(App *app) {
    if (!app->widget_preview_ready || !app->shader_code) return;
    gtk_gl_area_make_current(GTK_GL_AREA(app->widget_preview));
    if (gtk_gl_area_get_error(GTK_GL_AREA(app->widget_preview))) return;
    char *error = NULL; GLuint program = make_program_for_shader(app->shader_code, app->shader_path, &error);
    g_free(error);
    if (!program) return;
    if (app->widget_program) glDeleteProgram(app->widget_program);
    app->widget_program = program; shader_uniforms_init(&app->widget_uniforms, app->widget_program);
    free_shader_channels(app->widget_channel_textures);
    load_shader_channels(app->shader_path, app->widget_channel_textures, app->widget_channel_widths, app->widget_channel_heights, app->widget_channel_types);
    gtk_gl_area_queue_render(GTK_GL_AREA(app->widget_preview));
}

void widget_preview_realize(GtkGLArea *area, App *app) {
    gtk_gl_area_make_current(area);
    if (gtk_gl_area_get_error(area)) return;
    glGenVertexArrays(1, &app->widget_vao); glBindVertexArray(app->widget_vao);
    app->widget_preview_ready = TRUE;
    load_widget_program(app);
}

void clear_widget_preview(App *app) {
    if (!app->widget_preview_ready || !app->widget_preview) return;
    gtk_gl_area_make_current(GTK_GL_AREA(app->widget_preview));
    if (gtk_gl_area_get_error(GTK_GL_AREA(app->widget_preview))) return;
    if (app->widget_program) glDeleteProgram(app->widget_program);
    app->widget_program = 0;
    free_shader_channels(app->widget_channel_textures);
    gtk_gl_area_queue_render(GTK_GL_AREA(app->widget_preview));
}

void widget_preview_unrealize(GtkGLArea *area, App *app) {
    (void)area;
    clear_widget_preview(app);
    if (app->widget_vao) glDeleteVertexArrays(1, &app->widget_vao);
    app->widget_preview_ready = FALSE;
    app->widget_vao = 0;
}

gboolean preview_render(GtkGLArea *area, GdkGLContext *context, App *app) {
    (void)context;
    if (!app->preview_ready || !app->program) return TRUE;
    int width = gtk_widget_get_allocated_width(GTK_WIDGET(area));
    int height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    float elapsed = (float)(g_get_monotonic_time() - app->started) / 1000000.0f; int frame = (int)(elapsed * 60.0f); float speed = app->speed ? (float)gtk_range_get_value(GTK_RANGE(app->speed)) / 100.0f : 1.0f; gboolean chroma; float key[3], smoothing, opacity; app_effect_values(app, &chroma, key, &smoothing, &opacity); elapsed *= speed;
    glViewport(0, 0, width, height); glClearColor(0.025f, 0.035f, 0.06f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(app->program); shader_set_uniforms(&app->uniforms, width, height, elapsed, frame); shader_set_effects(&app->uniforms, chroma, key, smoothing, opacity); shader_set_preview_background(&app->uniforms, TRUE); shader_set_background(&app->uniforms, 0, 0, 0, FALSE); bind_shader_channels(&app->uniforms, app->channel_textures, app->channel_widths, app->channel_heights, app->channel_types);
    glBindVertexArray(app->vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

gboolean widget_preview_render(GtkGLArea *area, GdkGLContext *context, App *app) {
    (void)context;
    int width = gtk_widget_get_allocated_width(GTK_WIDGET(area));
    int height = gtk_widget_get_allocated_height(GTK_WIDGET(area));
    glViewport(0, 0, width, height); glClearColor(0.025f, 0.035f, 0.06f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
    if (!app->widget_preview_ready || !app->widget_program) return TRUE;
    float elapsed = (float)(g_get_monotonic_time() - app->started) / 1000000.0f; int frame = (int)(elapsed * 60.0f); float speed = app->speed ? (float)gtk_range_get_value(GTK_RANGE(app->speed)) / 100.0f : 1.0f; gboolean chroma; float key[3], smoothing, opacity; app_effect_values(app, &chroma, key, &smoothing, &opacity); elapsed *= speed;
    glUseProgram(app->widget_program); shader_set_uniforms(&app->widget_uniforms, width, height, elapsed, frame); shader_set_effects(&app->widget_uniforms, chroma, key, smoothing, opacity); shader_set_preview_background(&app->widget_uniforms, TRUE); shader_set_background(&app->widget_uniforms, 0, 0, 0, FALSE); bind_shader_channels(&app->widget_uniforms, app->widget_channel_textures, app->widget_channel_widths, app->widget_channel_heights, app->widget_channel_types);
    glBindVertexArray(app->widget_vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    return TRUE;
}

gboolean tick_preview(App *app) {
    if (app->preview) gtk_gl_area_queue_render(GTK_GL_AREA(app->preview));
    if (app->widget_preview) gtk_gl_area_queue_render(GTK_GL_AREA(app->widget_preview));
    return G_SOURCE_CONTINUE;
}

gboolean load_preview(App *app, const char *source, const char *title, const char *id) {
    if (!source) return FALSE;
    if (!app->preview || !gtk_widget_get_realized(app->preview)) {
        g_free(app->pending_preview_source); g_free(app->pending_preview_title); g_free(app->pending_preview_id);
        app->pending_preview_source = g_strdup(source); app->pending_preview_title = g_strdup(title ? title : "Untitled shader"); app->pending_preview_id = g_strdup(id ? id : "");
        return TRUE;
    }
    gtk_gl_area_make_current(GTK_GL_AREA(app->preview));
    if (gtk_gl_area_get_error(GTK_GL_AREA(app->preview))) return FALSE;
    char *error = NULL; GLuint program = make_program_for_shader(source, app->shader_path, &error);
    if (!program) {
        char *short_error = g_strndup(error ? error : "unknown shader error", 220);
        char *message = g_strdup_printf("Shader error: %s", short_error);
        set_status(app, message); g_free(message); g_free(short_error); g_free(error); return FALSE;
    }
    if (app->program) glDeleteProgram(app->program);
    app->program = program; shader_uniforms_init(&app->uniforms, app->program); g_free(app->shader_code); app->shader_code = g_strdup(source);
    g_free(app->shader_title); app->shader_title = g_strdup(title ? title : "Untitled shader");
    g_free(app->shader_id); app->shader_id = g_strdup(id ? id : "");
    rebuild_constant_controls(app, source);
    free_shader_channels(app->channel_textures); load_shader_channels(app->shader_path, app->channel_textures, app->channel_widths, app->channel_heights, app->channel_types);
    char *last_shader = data_path("last.frag"); char *last_title = data_path("last.title"); char *last_id = data_path("last.id"); char *last_path = data_path("last.path");
    g_file_set_contents(last_shader, source, -1, NULL); g_file_set_contents(last_title, app->shader_title, -1, NULL); g_file_set_contents(last_id, app->shader_id, -1, NULL);
    g_file_set_contents(last_path, app->shader_path ? app->shader_path : "", -1, NULL); g_free(last_shader); g_free(last_title); g_free(last_id); g_free(last_path);
    char *message = g_strdup_printf("Ready · %s", app->shader_title); set_status(app, message); g_free(message);
    gtk_gl_area_queue_render(GTK_GL_AREA(app->preview));
    load_widget_program(app);
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
    char *error = NULL; thumb->program = make_program_for_shader(thumb->source, thumb->path, &error); g_free(error); if (thumb->program) shader_uniforms_init(&thumb->uniforms, thumb->program); load_shader_channels(thumb->path, thumb->channel_textures, thumb->channel_widths, thumb->channel_heights, thumb->channel_types); if (thumb->program) glGenVertexArrays(1, &thumb->vao); thumb->started = g_get_monotonic_time();
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
    float elapsed = (float)(g_get_monotonic_time() - thumb->started) / 1000000.0f; float key[3] = { 0.0f, 1.0f, 0.0f }; glUseProgram(thumb->program); shader_set_uniforms(&thumb->uniforms, width, height, elapsed, (int)(elapsed * 60)); shader_set_effects(&thumb->uniforms, FALSE, key, 0.06f, 1.0f); shader_set_preview_background(&thumb->uniforms, FALSE); shader_set_background(&thumb->uniforms, 0, 0, 0, FALSE); bind_shader_channels(&thumb->uniforms, thumb->channel_textures, thumb->channel_widths, thumb->channel_heights, thumb->channel_types); glBindVertexArray(thumb->vao); glDrawArrays(GL_TRIANGLES, 0, 3); return TRUE;
}

static gboolean thumb_tick(GtkGLArea *area) { if (gtk_widget_get_realized(GTK_WIDGET(area))) gtk_gl_area_queue_render(area); return G_SOURCE_CONTINUE; }

static void thumb_destroy(GtkWidget *widget, gpointer data) {
    (void)data; guint source = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(widget), "thumb-timer"));
    if (source) { g_source_remove(source); g_object_set_data(G_OBJECT(widget), "thumb-timer", NULL); }
}

GtkWidget *shader_row(const char *title, const char *subtitle, const char *source, const char *shader_path) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    if (source) {
        GtkWidget *thumb_area = gtk_gl_area_new(); ShaderThumb *thumb = g_new0(ShaderThumb, 1); thumb->source = g_strdup(source); thumb->path = g_strdup(shader_path);
        gtk_widget_set_size_request(thumb_area, 175, 82); gtk_style_context_add_class(gtk_widget_get_style_context(thumb_area), "thumb"); g_object_set_data_full(G_OBJECT(thumb_area), "shader-thumb", thumb, (GDestroyNotify)thumb_free);
        g_signal_connect(thumb_area, "realize", G_CALLBACK(thumb_realize), thumb); g_signal_connect(thumb_area, "unrealize", G_CALLBACK(thumb_unrealize), thumb); g_signal_connect(thumb_area, "render", G_CALLBACK(thumb_render), thumb); g_signal_connect(thumb_area, "destroy", G_CALLBACK(thumb_destroy), NULL); guint timer = g_timeout_add(33, (GSourceFunc)thumb_tick, thumb_area); g_object_set_data(G_OBJECT(thumb_area), "thumb-timer", GUINT_TO_POINTER(timer)); gtk_box_pack_start(GTK_BOX(box), thumb_area, FALSE, FALSE, 0);
    }
    GtkWidget *name = gtk_label_new(title); GtkWidget *sub = gtk_label_new(subtitle ? subtitle : "");
    gtk_widget_set_halign(name, GTK_ALIGN_START); gtk_widget_set_halign(sub, GTK_ALIGN_START);
    gtk_style_context_add_class(gtk_widget_get_style_context(name), "row-title");
    gtk_style_context_add_class(gtk_widget_get_style_context(sub), "row-subtitle");
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0); gtk_box_pack_start(GTK_BOX(box), sub, FALSE, FALSE, 0);
    g_object_set_data(G_OBJECT(box), "shader-title", name); return box;
}
