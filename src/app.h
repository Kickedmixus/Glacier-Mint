#ifndef GLACIER_MINT_APP_H
#define GLACIER_MINT_APP_H

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#ifdef HAVE_WEBKIT
#include <webkit2/webkit2.h>
#endif
#include <epoxy/gl.h>
#include <json-glib/json-glib.h>
#include <curl/curl.h>
#include <signal.h>
#include <sys/types.h>

#define APP_NAME "Glacier Mint"
#define APP_ID "glacier-mint"
#define LOGO_FILE "logo1600x.png"
#define API_ROOT "https://www.shadertoy.com/api/v1"
#define CHANNEL_2D 0
#define CHANNEL_CUBE 1

typedef struct { char *id, *title, *author; } ShaderResult;
typedef struct { char *code, *title, *id; } ShaderLoad;

typedef struct {
    GtkWidget *window, *notebook, *preview, *results, *local_list, *status;
    GtkWidget *search, *api_key, *fps, *scale, *code_view, *browser, *browser_url, *constants_box;
    char *shader_code, *shader_title, *shader_id, *shader_path, *constants_source;
    GPtrArray *constants;
    guint constants_update_id;
    GLuint channel_textures[4];
    int channel_widths[4], channel_heights[4], channel_types[4];
    GLuint program, vao;
    gint64 started;
    gboolean preview_ready;
} App;

typedef struct { App *app; char *query; char *key; } SearchJob;
typedef struct { App *app; GPtrArray *items; char *error; } SearchDone;
typedef struct { App *app; char *id; char *key; } LoadJob;
typedef struct { App *app; char *id; char *code; char *title; char *channels_json; char *error; } LoadDone;
typedef struct { char *source, *path; GLuint program, vao, channel_textures[4]; int channel_widths[4], channel_heights[4], channel_types[4]; gint64 started; } ShaderThumb;

void set_status(App *app, const char *text);
char *read_file(const char *path);

GLuint make_program(const char *user_source, char **error);
GLuint make_program_for_shader(const char *user_source, const char *shader_path, char **error);
void set_uniforms(GLuint program, float width, float height, float time_s, int frame);
gboolean load_preview(App *app, const char *source, const char *title, const char *id);
gboolean update_shader_source(App *app, const char *source);
void apply_constants_clicked(GtkButton *button, App *app);
void reset_constants_clicked(GtkButton *button, App *app);
GtkWidget *shader_row(const char *title, const char *subtitle, const char *source, const char *shader_path);
void free_shader_channels(GLuint textures[4]);
gboolean load_shader_channels(const char *shader_path, GLuint textures[4], int widths[4], int heights[4], int types[4]);
void bind_shader_channels(GLuint program, GLuint textures[4], const int widths[4], const int heights[4], const int types[4]);
void preview_realize(GtkGLArea *area, App *app);
gboolean preview_render(GtkGLArea *area, GdkGLContext *context, App *app);
gboolean tick_preview(App *app);
void local_row_activated(GtkFlowBox *box, GtkFlowBoxChild *row, App *app);

char *data_path(const char *name);
char *wallpapers_dir(void);
char *self_executable(void);
char *logo_path(void);
GtkFlowBoxChild *add_wallpaper_card(App *app, const char *path);
void populate_local(App *app);
void import_shader(App *app, const char *source, const char *title, const char *id);
void import_shader_with_channels(App *app, const char *source, const char *title, const char *id, const char *channels_json);
void remove_shader_storage(const char *shader_path);
gboolean restore_last_shader(App *app);

void stop_wallpaper(void);
void apply_clicked(GtkButton *button, App *app);
void stop_clicked(GtkButton *button, App *app);
void open_clicked(GtkButton *button, App *app);
void delete_selected_clicked(GtkButton *button, App *app);
void load_pasted_clicked(GtkButton *button, App *app);

#ifdef HAVE_WEBKIT
extern const char *browser_import_script;
void browser_message(WebKitUserContentManager *manager, WebKitJavascriptResult *result, App *app);
void browser_back(GtkButton *button, App *app);
void browser_forward(GtkButton *button, App *app);
void browser_load(GtkButton *button, App *app);
void browser_load_changed(WebKitWebView *view, WebKitLoadEvent event, App *app);
#endif
void browser_external_clicked(GtkButton *button, App *app);

void add_css(void);
GtkWidget *build_ui(App *app);
int wallpaper_main(const char *path, int fps);

#endif
