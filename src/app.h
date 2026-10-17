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

#define FEED_BANDS 64
#define FEED_HISTORY 60
#define WIDGET_MAX_BARS 128
enum { FEED_CPU, FEED_GPU, FEED_RAM, FEED_NET, FEED_METRICS };

typedef struct { char *id, *title, *author; } ShaderResult;
typedef struct { char *code, *title, *id; } ShaderLoad;

typedef enum { WIDGET_OPT_BOOL, WIDGET_OPT_INT, WIDGET_OPT_CHOICE, WIDGET_OPT_TEXT } WidgetOptionType;
typedef struct { const char *kind, *key, *label; WidgetOptionType type; const char *fallback; int min, max; const char *choices; } WidgetOption;
typedef struct { const char *kind, *label; } WidgetKind;

/* One desktop widget as saved in widgets.ini; options holds the kind-specific settings. */
typedef struct {
    char kind[16], *font; double x, y;
    int size, bold, italic, spacing, stretch_x, stretch_y, width, scale, gradient, gradient_angle, panel, radius;
    float text[4], gradient_color[4], accent[4], secondary[4], panel_color[4];
    GHashTable *options;
    float levels[WIDGET_MAX_BARS], peaks[WIDGET_MAX_BARS]; gint64 levels_at;
} DeskWidget;

typedef struct WidgetFeed WidgetFeed;
typedef struct BeatTracker BeatTracker;
typedef struct { gboolean active; double bpm, beat_time, level, bass; } BeatState;
typedef struct { double beat, bpm; } BeatClock;
typedef struct {
    gboolean editing, has_gpu, has_cpu_temp, has_gpu_temp, has_media, playing, audio_live;
    double cpu, gpu, ram, swap, disk, cpu_temp, gpu_temp, net_down, net_up, uptime;
    guint64 ram_used, ram_total, swap_used, swap_total, disk_used, disk_total;
    float history[FEED_METRICS][FEED_HISTORY]; int history_count;
    float bands[FEED_BANDS];
    char *title, *artist, *album, *player; gint64 length_us, position_us; cairo_surface_t *art;
    GHashTable *outputs;
} WidgetSnapshot;

typedef struct {
    GtkWidget *window, *notebook, *preview, *widget_canvas, *widget_preview, *widget_grid, *widget_settings_panel, *widget_settings_name, *widget_settings_font, *widget_settings_size, *widget_settings_width, *widget_settings_bold, *widget_settings_italic, *widget_settings_gradient, *widget_settings_color, *widget_settings_gradient_color, *widget_settings_box, *widget_settings_box_color, *widget_settings_border, *widget_settings_radius, *widget_settings_spacing, *widget_settings_stretch_x, *widget_settings_stretch_y, *results, *local_list, *status;
    GtkWidget *search, *api_key, *fps, *scale, *code_view, *browser, *browser_url, *constants_box, *toy_status;
    GtkWidget *widget_settings_scale, *widget_settings_accent, *widget_settings_secondary, *widget_settings_options, *vsync, *settings_status;
    int monitor_refresh;
    WidgetFeed *widget_feed;
    WidgetSnapshot widget_snapshot;
    char *shader_code, *shader_title, *shader_id, *shader_path, *constants_source;
    GPtrArray *constants;
    GPtrArray *wallpaper_widgets;
    guint constants_update_id;
    guint widget_clock_id;
    GLuint channel_textures[4];
    int channel_widths[4], channel_heights[4], channel_types[4];
    GLuint program, vao;
    GLuint widget_program, widget_vao, widget_channel_textures[4];
    int widget_channel_widths[4], widget_channel_heights[4], widget_channel_types[4];
    gint64 started;
    gboolean preview_ready, widget_preview_ready, widget_settings_updating;
    GtkWidget *selected_widget;
    int widget_screen_width, widget_screen_height;
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
void widget_preview_realize(GtkGLArea *area, App *app);
void widget_preview_unrealize(GtkGLArea *area, App *app);
gboolean widget_preview_render(GtkGLArea *area, GdkGLContext *context, App *app);
void clear_widget_preview(App *app);
gboolean tick_preview(App *app);
void local_row_activated(GtkFlowBox *box, GtkFlowBoxChild *row, App *app);

char *data_path(const char *name);
char *wallpapers_dir(void);
char *self_executable(void);
char *logo_path(void);
GtkFlowBoxChild *add_wallpaper_card(App *app, const char *path);
void populate_local(App *app);
gboolean import_shader(App *app, const char *source, const char *title, const char *id);
gboolean import_shader_with_channels(App *app, const char *source, const char *title, const char *id, const char *channels_json);
void remove_shader_storage(const char *shader_path);
gboolean restore_last_shader(App *app);

void stop_wallpaper(void);
int restore_wallpaper(void);
void load_wallpaper_settings(App *app);
void wallpaper_settings_changed(GtkWidget *control, App *app);
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

const WidgetKind *desk_widget_kinds(guint *count);
const char *desk_widget_kind_label(const char *kind);
const WidgetOption *desk_widget_options(guint *count);
DeskWidget *desk_widget_new(const char *kind);
void desk_widget_free(DeskWidget *widget);
const char *desk_widget_get(const DeskWidget *widget, const char *key);
int desk_widget_int(const DeskWidget *widget, const char *key);
void desk_widget_set(DeskWidget *widget, const char *key, const char *value);
float *desk_widget_color(DeskWidget *widget, const char *key);
int desk_widget_refresh_ms(const DeskWidget *widget);
void desk_widget_measure(DeskWidget *widget, const WidgetSnapshot *data, int *width, int *height);
void desk_widget_draw(cairo_t *cr, DeskWidget *widget, const WidgetSnapshot *data);
char *desk_widgets_read(void);
GPtrArray *desk_widgets_parse(const char *contents);
void desk_widgets_save(GPtrArray *widgets);

WidgetFeed *widget_feed_new(gboolean editing);
void widget_feed_sync(WidgetFeed *feed, GPtrArray *widgets);
void widget_feed_snapshot(WidgetFeed *feed, WidgetSnapshot *snapshot);
void widget_snapshot_clear(WidgetSnapshot *snapshot);
void widget_feed_free(WidgetFeed *feed);

gboolean shader_uses_audio(const char *source);
BeatTracker *beat_tracker_new(void);
void beat_tracker_state(BeatTracker *tracker, BeatState *state);
void beat_tracker_free(BeatTracker *tracker);
void beat_clock_update(BeatClock *clock, double dt, double now, const BeatState *state);

void add_css(void);
GtkWidget *build_ui(App *app);
int wallpaper_main(const char *path, int fps, gboolean vsync);

#endif
