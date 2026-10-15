#include "app.h"

#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cairo/cairo.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t wallpaper_running = 1;
static void on_signal(int sig) { (void)sig; wallpaper_running = 0; }

static void x11_property(Display *display, Window window, Atom property, Atom type, const unsigned long *values, int count) { XChangeProperty(display, window, property, type, 32, PropModeReplace, (const unsigned char *)values, count); }

typedef struct {
    Display *display; Window root; int width, height;
    Pixmap pixmap; GC gc; XImage *image;
    Atom xroot_atom, esetroot_atom; Pixmap old_xroot, old_esetroot;
} RootMirror;

typedef struct { char kind[8], format[8], font[32]; float x, y, text_color[4], gradient_color[4], box_color[4], glow_color[4]; int size, bold, italic, gradient, box, border, radius, glow, glow_blur, glow_opacity, gradient_angle, widget_width, spacing, stretch_x, stretch_y; GLuint texture; int width, height; } WallpaperWidget;
typedef struct { GPtrArray *items; char *config_text; gint64 last_poll, last_second; GLuint program, vao; } WallpaperWidgets;

static void wallpaper_widgets_clear(WallpaperWidgets *widgets) {
    if (!widgets->items) return;
    for (guint i = 0; i < widgets->items->len; i++) { WallpaperWidget *widget = g_ptr_array_index(widgets->items, i); if (widget->texture) glDeleteTextures(1, &widget->texture); }
    g_ptr_array_set_size(widgets->items, 0);
}

static void wallpaper_widget_defaults(WallpaperWidget *widget) {
    g_strlcpy(widget->font, "Sans", sizeof(widget->font)); widget->size = !strcmp(widget->kind, "time") ? 48 : 26; widget->bold = TRUE; widget->text_color[0] = 0.90f; widget->text_color[1] = 0.96f; widget->text_color[2] = 0.98f; widget->text_color[3] = 1.0f; widget->gradient_color[0] = 0.45f; widget->gradient_color[1] = 0.80f; widget->gradient_color[2] = 1.0f; widget->gradient_color[3] = 1.0f; widget->glow_color[0] = 0.45f; widget->glow_color[1] = 0.80f; widget->glow_color[2] = 1.0f; widget->glow_color[3] = 1.0f; widget->box = FALSE; widget->border = 0; widget->radius = 0; widget->glow_blur = 12; widget->glow_opacity = 55; widget->stretch_x = 100; widget->stretch_y = 100;
}

static gboolean wallpaper_widget_parse(WallpaperWidget *widget, const char *line) {
    char kind[8] = "", format[8] = ""; double x = 0, y = 0; char font[32] = "Sans"; int size = 32, bold = TRUE, italic = FALSE, gradient = FALSE, box = FALSE, border = 0, radius = 0, glow = FALSE, glow_blur = 12, widget_width = 0, gradient_angle = 0, glow_opacity = 55, spacing = 0, stretch_x = 100, stretch_y = 100; float text[4] = {0.90f, 0.96f, 0.98f, 1.0f}, gradient_color[4] = {0.45f, 0.80f, 1.0f, 1.0f}, box_color[4] = {0.01f, 0.03f, 0.05f, 0.0f}, glow_color[4] = {0.45f, 0.80f, 1.0f, 1.0f};
    int fields = sscanf(line, "%7s %lf %lf %7s %31s %d %d %d %f %f %f %f %d %f %f %f %f %d %f %f %f %f %d %d %d %d %d %d %d %d %d %d %f %f %f %f", kind, &x, &y, format, font, &size, &bold, &italic, &text[0], &text[1], &text[2], &text[3], &gradient, &gradient_color[0], &gradient_color[1], &gradient_color[2], &gradient_color[3], &box, &box_color[0], &box_color[1], &box_color[2], &box_color[3], &border, &radius, &glow, &glow_blur, &widget_width, &gradient_angle, &glow_opacity, &spacing, &stretch_x, &stretch_y, &glow_color[0], &glow_color[1], &glow_color[2], &glow_color[3]);
    if (fields < 3 || (strcmp(kind, "time") && strcmp(kind, "date"))) return FALSE;
    memset(widget, 0, sizeof(*widget)); g_strlcpy(widget->kind, kind, sizeof(widget->kind)); g_strlcpy(widget->format, fields >= 4 ? format : (!strcmp(kind, "time") ? "24" : "long"), sizeof(widget->format)); widget->x = CLAMP((float)x, 0.0f, 1.0f); widget->y = CLAMP((float)y, 0.0f, 1.0f); wallpaper_widget_defaults(widget);
    if (fields >= 5) { for (char *p = font; *p; p++) if (*p == '_') *p = ' '; g_strlcpy(widget->font, font, sizeof(widget->font)); }
    if (fields >= 6) widget->size = CLAMP(size, 8, 160);
    if (fields >= 7) widget->bold = !!bold;
    if (fields >= 8) widget->italic = !!italic;
    if (fields >= 12) memcpy(widget->text_color, text, sizeof(text));
    if (fields >= 17) { widget->gradient = !!gradient; memcpy(widget->gradient_color, gradient_color, sizeof(gradient_color)); }
    if (fields >= 22) { widget->box = FALSE; memcpy(widget->box_color, box_color, sizeof(box_color)); }
    if (fields >= 23) widget->border = 0;
    if (fields >= 24) widget->radius = 0;
    if (fields >= 26) { widget->glow = !!glow; widget->glow_blur = CLAMP(glow_blur, 1, 80); }
    if (fields >= 27) widget->widget_width = CLAMP(widget_width, 0, 1600);
    if (fields >= 28) widget->gradient_angle = fields == 28 && gradient_angle <= 4 ? 0 : ((gradient_angle % 360) + 360) % 360;
    if (fields >= 29) widget->glow_opacity = CLAMP(glow_opacity, 0, 100);
    if (fields >= 30) widget->spacing = CLAMP(spacing, 0, 40);
    if (fields >= 31) widget->stretch_x = CLAMP(stretch_x, 50, 200);
    if (fields >= 32) widget->stretch_y = CLAMP(stretch_y, 50, 200);
    if (fields >= 36) memcpy(widget->glow_color, glow_color, sizeof(glow_color));
    return TRUE;
}

static void wallpaper_widgets_reload(WallpaperWidgets *widgets, const char *contents) {
    wallpaper_widgets_clear(widgets); g_free(widgets->config_text); widgets->config_text = g_strdup(contents ? contents : "");
    if (!contents) return;
    char **lines = g_strsplit(contents, "\n", -1);
    for (guint i = 0; lines[i]; i++) { WallpaperWidget *widget = g_new0(WallpaperWidget, 1); if (wallpaper_widget_parse(widget, lines[i])) g_ptr_array_add(widgets->items, widget); else g_free(widget); }
    g_strfreev(lines); widgets->last_second = -1;
}

static void wallpaper_widgets_poll(WallpaperWidgets *widgets) {
    gint64 now = g_get_monotonic_time(); if (now - widgets->last_poll < 250000) return; widgets->last_poll = now;
    char *path = data_path("widgets.conf"); char *contents = read_file(path); g_free(path);
    if (g_strcmp0(contents, widgets->config_text)) wallpaper_widgets_reload(widgets, contents);
    g_free(contents);
}

static GLuint widget_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type); glShaderSource(shader, 1, &source, NULL); glCompileShader(shader); GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok); if (!ok) { glDeleteShader(shader); return 0; } return shader;
}

static GLuint wallpaper_widget_program(void) {
    static const char *vertex_source = "#version 330 core\nconst vec2 p[6] = vec2[6](vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,0), vec2(1,1), vec2(0,1)); out vec2 uv; uniform vec2 screen; uniform vec4 rect; void main() { uv = p[gl_VertexID]; vec2 pixel = rect.xy + p[gl_VertexID] * rect.zw; gl_Position = vec4(pixel.x / screen.x * 2.0 - 1.0, 1.0 - pixel.y / screen.y * 2.0, 0.0, 1.0); }";
    static const char *fragment_source = "#version 330 core\nin vec2 uv; uniform sampler2D image; out vec4 color; void main() { color = texture(image, uv); }";
    GLuint vs = widget_shader(GL_VERTEX_SHADER, vertex_source), fs = widget_shader(GL_FRAGMENT_SHADER, fragment_source); if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return 0; }
    GLuint program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs); glLinkProgram(program); glDeleteShader(vs); glDeleteShader(fs); GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok); if (!ok) { glDeleteProgram(program); return 0; } return program;
}

static int wallpaper_character_count(const char *text) { return MAX(1, (int)g_utf8_strlen(text ? text : "", -1)); }

static double wallpaper_text_advance(cairo_t *cr, const char *text, int spacing) {
    cairo_text_extents_t ext; cairo_text_extents(cr, text, &ext); return ext.x_advance + MAX(0, wallpaper_character_count(text) - 1) * spacing;
}

static void wallpaper_text_path(cairo_t *cr, const char *text, int spacing) {
    double x = 0; const char *cursor = text;
    while (cursor && *cursor) {
        gunichar character = g_utf8_get_char(cursor); char part[8]; int length = g_unichar_to_utf8(character, part); part[length] = '\0'; cairo_move_to(cr, x, 0); cairo_text_path(cr, part);
        cairo_text_extents_t ext; cairo_text_extents(cr, part, &ext); x += ext.x_advance + spacing; cursor = g_utf8_next_char(cursor);
    }
}

static void wallpaper_widget_texture(WallpaperWidget *widget) {
    time_t now = time(NULL); struct tm local_time; localtime_r(&now, &local_time); char text[96]; gboolean time_widget = !strcmp(widget->kind, "time"); const char *format = time_widget ? (!strcmp(widget->format, "12") ? "%I:%M %p" : "%H:%M") : (!strcmp(widget->format, "short") ? "%Y-%m-%d" : "%A, %B %d"); strftime(text, sizeof(text), format, &local_time);
    double padding = 8.0; cairo_surface_t *measure_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1); cairo_t *measure = cairo_create(measure_surface); cairo_select_font_face(measure, widget->font, widget->italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, widget->bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(measure, widget->size); cairo_text_extents_t ext; cairo_font_extents_t font; cairo_text_extents(measure, text, &ext); cairo_font_extents(measure, &font); double text_width = wallpaper_text_advance(measure, text, widget->spacing); cairo_destroy(measure); cairo_surface_destroy(measure_surface);
    int logical_width = MAX(widget->widget_width, (int)ceil(text_width + padding * 2.0)), logical_height = (int)ceil(font.height + padding * 2.0); double stretch_x = widget->stretch_x / 100.0, stretch_y = widget->stretch_y / 100.0; int width = MAX(1, (int)ceil(logical_width * stretch_x)), height = MAX(1, (int)ceil(logical_height * stretch_y)); cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height); cairo_t *cr = cairo_create(surface); cairo_scale(cr, stretch_x, stretch_y);
    cairo_select_font_face(cr, widget->font, widget->italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL, widget->bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(cr, widget->size); double x = padding, y = padding + (logical_height - padding * 2.0 - font.height) / 2.0 + font.height;
    cairo_save(cr); cairo_translate(cr, x, y); if (widget->gradient) { double angle = widget->gradient_angle * G_PI / 180.0, cx = logical_width / 2.0 - x, cy = logical_height / 2.0 - y, dx = cos(angle) * logical_width / 2.0, dy = sin(angle) * logical_height / 2.0; wallpaper_text_path(cr, text, widget->spacing); cairo_pattern_t *gradient = cairo_pattern_create_linear(cx - dx, cy - dy, cx + dx, cy + dy); cairo_pattern_add_color_stop_rgba(gradient, 0, widget->text_color[0], widget->text_color[1], widget->text_color[2], widget->text_color[3]); cairo_pattern_add_color_stop_rgba(gradient, 1, widget->gradient_color[0], widget->gradient_color[1], widget->gradient_color[2], widget->gradient_color[3]); cairo_set_source(cr, gradient); cairo_fill(cr); cairo_pattern_destroy(gradient); } else { wallpaper_text_path(cr, text, widget->spacing); cairo_set_source_rgba(cr, widget->text_color[0], widget->text_color[1], widget->text_color[2], widget->text_color[3]); cairo_fill(cr); } cairo_restore(cr); cairo_surface_flush(surface);
    if (widget->texture) glDeleteTextures(1, &widget->texture);
    glGenTextures(1, &widget->texture); glBindTexture(GL_TEXTURE_2D, widget->texture); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surface)); widget->width = width; widget->height = height; cairo_destroy(cr); cairo_surface_destroy(surface);
}

static void wallpaper_widgets_render(WallpaperWidgets *widgets, int width, int height) {
    if (!widgets->program || !widgets->items->len) return;
    gint64 second = g_get_real_time() / G_USEC_PER_SEC; if (second != widgets->last_second) { for (guint i = 0; i < widgets->items->len; i++) wallpaper_widget_texture(g_ptr_array_index(widgets->items, i)); widgets->last_second = second; }
    glUseProgram(widgets->program); glUniform2f(glGetUniformLocation(widgets->program, "screen"), (float)width, (float)height); glUniform1i(glGetUniformLocation(widgets->program, "image"), 0); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glBindVertexArray(widgets->vao);
    for (guint i = 0; i < widgets->items->len; i++) { WallpaperWidget *widget = g_ptr_array_index(widgets->items, i); float x = MIN(widget->x * width, MAX(0, width - widget->width)), y = MIN(widget->y * height, MAX(0, height - widget->height)); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, widget->texture); glUniform4f(glGetUniformLocation(widgets->program, "rect"), x, y, (float)widget->width, (float)widget->height); glDrawArrays(GL_TRIANGLES, 0, 6); }
    glDisable(GL_BLEND);
}

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
    g_free(row);
    XPutImage(mirror->display, mirror->pixmap, mirror->gc, mirror->image, 0, 0, 0, 0, (unsigned)mirror->width, (unsigned)mirror->height);
    /* Updating a pixmap does not repaint pixels already stored in the root. */
    XClearArea(mirror->display, mirror->root, 0, 0, (unsigned)mirror->width, (unsigned)mirror->height, False);
    XFlush(mirror->display);
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

int wallpaper_main(const char *path, int fps) {
    char *source = read_file(path); if (!source) { fprintf(stderr, "glacier-mint: cannot read %s\n", path); return 1; }
    Display *display = XOpenDisplay(NULL); if (!display) { fprintf(stderr, "glacier-mint: DISPLAY is unavailable\n"); g_free(source); return 1; }
    int screen = DefaultScreen(display); Window root = RootWindow(display, screen); int width = DisplayWidth(display, screen), height = DisplayHeight(display, screen);
    int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None }; XVisualInfo *visual = glXChooseVisual(display, screen, attrs);
    if (!visual) { fprintf(stderr, "glacier-mint: no GLX visual\n"); XCloseDisplay(display); g_free(source); return 1; }
    Colormap cmap = XCreateColormap(display, root, visual->visual, AllocNone); XSetWindowAttributes swa = {0}; swa.colormap = cmap; swa.border_pixel = 0; swa.background_pixel = 0;
    Window window = XCreateWindow(display, root, 0, 0, width, height, 0, visual->depth, InputOutput, visual->visual, CWColormap | CWBorderPixel | CWBackPixel, &swa);
    Atom desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False), type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False); x11_property(display, window, type, XA_ATOM, (unsigned long *)&desktop, 1);
    Atom below = XInternAtom(display, "_NET_WM_STATE_BELOW", False), sticky = XInternAtom(display, "_NET_WM_STATE_STICKY", False), skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False), skip_pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False), state = XInternAtom(display, "_NET_WM_STATE", False); unsigned long states[] = {(unsigned long)below, (unsigned long)sticky, (unsigned long)skip_taskbar, (unsigned long)skip_pager}; x11_property(display, window, state, XA_ATOM, states, 4);
    Atom desktop_number = XInternAtom(display, "_NET_WM_DESKTOP", False); unsigned long all_workspaces = 0xFFFFFFFFUL; x11_property(display, window, desktop_number, XA_CARDINAL, &all_workspaces, 1);
    XStoreName(display, window, "Glacier Mint wallpaper"); XMapWindow(display, window); XLowerWindow(display, window); XFlush(display);
    GLXContext context = glXCreateContext(display, visual, NULL, True); XFree(visual); if (!context || !glXMakeCurrent(display, window, context)) { fprintf(stderr, "glacier-mint: could not create GL context\n"); XDestroyWindow(display, window); XCloseDisplay(display); g_free(source); return 1; }
    char *error = NULL; GLuint program = make_program_for_shader(source, path, &error); g_free(source); if (!program) { fprintf(stderr, "glacier-mint: shader error: %s\n", error ? error : "unknown"); g_free(error); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 1; }
    GLuint channels[4]; int channel_widths[4], channel_heights[4], channel_types[4]; load_shader_channels(path, channels, channel_widths, channel_heights, channel_types);
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao); WallpaperWidgets widgets = {0}; widgets.items = g_ptr_array_new_with_free_func(g_free); widgets.last_second = -1; widgets.program = wallpaper_widget_program(); glGenVertexArrays(1, &widgets.vao); glBindVertexArray(widgets.vao); wallpaper_widgets_poll(&widgets); RootMirror mirror; gboolean mirror_ready = root_mirror_begin(&mirror, display, root, width, height); signal(SIGTERM, on_signal); signal(SIGINT, on_signal); gint64 started = g_get_monotonic_time(); int frame = 0; double period = 1.0 / MAX(fps, 1); gint64 next = started;
    while (wallpaper_running) { gint64 now = g_get_monotonic_time(); float elapsed = (float)(now - started) / 1000000.0f; wallpaper_widgets_poll(&widgets); glViewport(0, 0, width, height); glUseProgram(program); set_uniforms(program, width, height, elapsed, frame++); bind_shader_channels(program, channels, channel_widths, channel_heights, channel_types); glDrawArrays(GL_TRIANGLES, 0, 3); wallpaper_widgets_render(&widgets, width, height); if (mirror_ready && frame % 6 == 0) root_mirror_update(&mirror); glXSwapBuffers(display, window); next += (gint64)(period * 1000000.0); gint64 wait = next - g_get_monotonic_time(); if (wait > 0) g_usleep((gulong)wait); else next = g_get_monotonic_time(); }
    if (mirror_ready) root_mirror_end(&mirror);
    wallpaper_widgets_clear(&widgets); g_free(widgets.config_text); if (widgets.program) glDeleteProgram(widgets.program); if (widgets.vao) glDeleteVertexArrays(1, &widgets.vao); g_ptr_array_free(widgets.items, TRUE); glDeleteProgram(program); free_shader_channels(channels); glXMakeCurrent(display, None, NULL); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 0;
}
