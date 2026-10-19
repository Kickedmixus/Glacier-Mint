#include "app.h"

#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <cairo/cairo.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t wallpaper_running = 1;
static void on_signal(int sig) { (void)sig; wallpaper_running = 0; }

static void x11_property(Display *display, Window window, Atom property, Atom type, const unsigned long *values, int count) { XChangeProperty(display, window, property, type, 32, PropModeReplace, (const unsigned char *)values, count); }

/* Frames are copied into the root window's background pixmap a few times a second so Cinnamon's workspace
   overview shows the shader. The readback goes through a pixel buffer so it never stalls rendering, and the
   image reaches the X server through shared memory when it can. */
typedef struct {
    Display *display; Window root; int width, height;
    Pixmap pixmap; GC gc; XImage *image; XShmSegmentInfo shm; gboolean use_shm; GLuint pbo; gboolean pending;
    Atom xroot_atom, esetroot_atom; Pixmap old_xroot, old_esetroot;
} RootMirror;

static int trapped_error;
static int trap_error(Display *display, XErrorEvent *event) { (void)display; trapped_error = event->error_code; return 0; }

/* Runs an X request that may fail without letting Xlib's default handler exit the process. */
static gboolean x11_try(Display *display, gboolean (*request)(Display *, gpointer), gpointer data) {
    XSync(display, False); trapped_error = 0; int (*previous)(Display *, XErrorEvent *) = XSetErrorHandler(trap_error);
    gboolean ok = request(display, data); XSync(display, False); XSetErrorHandler(previous); return ok && !trapped_error;
}

static gboolean query_pixmap(Display *display, gpointer data) { Window root; int x, y; unsigned width, height, border, depth; return XGetGeometry(display, *(Pixmap *)data, &root, &x, &y, &width, &height, &border, &depth) != 0; }
static gboolean query_window(Display *display, gpointer data) { XWindowAttributes attributes; return XGetWindowAttributes(display, *(Window *)data, &attributes) != 0; }
/* A background left by an earlier wallpaper process can carry an id the server has since handed to this process,
   because a closed client's id range is reused. Xlib numbers a client's ids upward from its base and bases are at
   least 0x40000 apart, so an id this close to one of ours is stale. Windows are not pixmaps either. */
static gboolean pixmap_exists(Display *display, Pixmap pixmap, XID own) {
    if (!pixmap || (pixmap > own ? pixmap - own : own - pixmap) < 0x10000) return FALSE;
    return x11_try(display, query_pixmap, &pixmap) && !x11_try(display, query_window, &pixmap);
}
static gboolean attach_shm(Display *display, gpointer data) { return XShmAttach(display, data) != 0; }

typedef struct { DeskWidget *widget; GLuint texture; int width, height; gint64 bucket; } WallpaperItem;
typedef struct { GPtrArray *widgets; GArray *items; char *config_text; gint64 last_poll; GLuint program, vao; GLint screen_uniform, image_uniform, rect_uniform; WidgetFeed *feed; } WallpaperWidgets;

static void wallpaper_widgets_clear(WallpaperWidgets *widgets) {
    for (guint i = 0; widgets->items && i < widgets->items->len; i++) { WallpaperItem *item = &g_array_index(widgets->items, WallpaperItem, i); if (item->texture) glDeleteTextures(1, &item->texture); }
    if (widgets->items) g_array_set_size(widgets->items, 0);
    if (widgets->widgets) { g_ptr_array_unref(widgets->widgets); widgets->widgets = NULL; }
}

static void wallpaper_widgets_reload(WallpaperWidgets *widgets, const char *contents) {
    wallpaper_widgets_clear(widgets); g_free(widgets->config_text); widgets->config_text = g_strdup(contents ? contents : "");
    widgets->widgets = desk_widgets_parse(contents);
    for (guint i = 0; i < widgets->widgets->len; i++) { WallpaperItem item = { g_ptr_array_index(widgets->widgets, i), 0, 0, 0, -1 }; g_array_append_val(widgets->items, item); }
    widget_feed_sync(widgets->feed, widgets->widgets);
}

static void wallpaper_widgets_poll(WallpaperWidgets *widgets) {
    gint64 now = g_get_monotonic_time(); if (now - widgets->last_poll < 250000) return; widgets->last_poll = now;
    char *contents = desk_widgets_read();
    if (g_strcmp0(contents ? contents : "", widgets->config_text)) wallpaper_widgets_reload(widgets, contents);
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


static void wallpaper_widget_texture(WallpaperItem *item, const WidgetSnapshot *data) {
    int width, height; desk_widget_measure(item->widget, data, &width, &height);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height); cairo_t *cr = cairo_create(surface); desk_widget_draw(cr, item->widget, data); cairo_destroy(cr); cairo_surface_flush(surface);
    if (!item->texture) { glGenTextures(1, &item->texture); glBindTexture(GL_TEXTURE_2D, item->texture); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); }
    glBindTexture(GL_TEXTURE_2D, item->texture); glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glPixelStorei(GL_UNPACK_ROW_LENGTH, cairo_image_surface_get_stride(surface) / 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, cairo_image_surface_get_data(surface)); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    item->width = width; item->height = height; cairo_surface_destroy(surface);
}

static void wallpaper_widgets_render(WallpaperWidgets *widgets, int width, int height) {
    if (!widgets->program || !widgets->items->len) return;
    gint64 now_ms = g_get_real_time() / 1000; WidgetSnapshot data; gboolean have_data = FALSE;
    for (guint i = 0; i < widgets->items->len; i++) {
        WallpaperItem *item = &g_array_index(widgets->items, WallpaperItem, i); gint64 bucket = now_ms / desk_widget_refresh_ms(item->widget); if (bucket == item->bucket) continue;
        if (!have_data) { widget_feed_snapshot(widgets->feed, &data); have_data = TRUE; }
        wallpaper_widget_texture(item, &data); item->bucket = bucket;
    }
    if (have_data) widget_snapshot_clear(&data);
    /* Cairo gives premultiplied alpha. */
    glUseProgram(widgets->program); glUniform2f(widgets->screen_uniform, (float)width, (float)height); glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); glBindVertexArray(widgets->vao);
    for (guint i = 0; i < widgets->items->len; i++) { WallpaperItem *item = &g_array_index(widgets->items, WallpaperItem, i); if (!item->texture) continue; double x = item->widget->x, y = item->widget->y; desk_widget_clamp_position(width, height, item->width, item->height, &x, &y); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, item->texture); glUniform4f(widgets->rect_uniform, (float)(x * width), (float)(y * height), (float)item->width, (float)item->height); glDrawArrays(GL_TRIANGLES, 0, 6); }
    glDisable(GL_BLEND);
}

static void set_audio_uniforms(ShaderUniforms *uniforms, const BeatClock *clock, const BeatState *state) {
    shader_set_audio_uniforms(uniforms, clock, state);
}

typedef void (*SwapIntervalExt)(Display *, GLXDrawable, int);
typedef int (*SwapIntervalMesa)(unsigned int);

/* V-sync on waits for the monitor's refresh before each frame is shown; off lets the FPS limit alone pace frames. */
static void set_vsync(Display *display, int screen, Window window, gboolean vsync) {
    const char *extensions = glXQueryExtensionsString(display, screen); if (!extensions) return;
    if (strstr(extensions, "GLX_EXT_swap_control")) { SwapIntervalExt swap = (SwapIntervalExt)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalEXT"); if (swap) { swap(display, window, vsync ? 1 : 0); return; } }
    if (strstr(extensions, "GLX_MESA_swap_control")) { SwapIntervalMesa swap = (SwapIntervalMesa)glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA"); if (swap) swap(vsync ? 1 : 0); }
}

static Pixmap root_pixmap_property(Display *display, Window root, Atom property) {
    Atom actual_type; int format; unsigned long count, remaining; unsigned char *data = NULL; Pixmap result = 0;
    if (XGetWindowProperty(display, root, property, 0, 1, False, AnyPropertyType, &actual_type, &format, &count, &remaining, &data) == Success && data && format == 32 && count) result = (Pixmap)((unsigned long *)data)[0];
    if (data) XFree(data);
    return result;
}

typedef struct { Display *display; Drawable drawable; unsigned width, height; XImage *image; } RootImageRequest;
static gboolean get_root_image(Display *display, gpointer data) {
    RootImageRequest *request = data; request->image = XGetImage(display, request->drawable, 0, 0, request->width, request->height, AllPlanes, ZPixmap); return request->image != NULL;
}

typedef struct { Drawable drawable; int x, y; unsigned width, height, border, depth; } DrawableGeometry;
static gboolean get_drawable_geometry(Display *display, gpointer data) {
    DrawableGeometry *geometry = data;
    Window root;
    return XGetGeometry(display, geometry->drawable, &root, &geometry->x, &geometry->y,
                        &geometry->width, &geometry->height, &geometry->border, &geometry->depth) != 0;
}

static unsigned char root_component(unsigned long pixel, unsigned long mask) {
    if (!mask) return 0;
    unsigned shift = 0; while (!(mask & 1u)) { mask >>= 1; shift++; }
    unsigned long max = mask; unsigned long value = (pixel >> shift) & max; return (unsigned char)((value * 255u + max / 2u) / max);
}

/* Captures the desktop pixmap once. Chroma mode composites against this image
   directly, so it works even when the window manager ignores per-pixel alpha. */
static GLuint root_background_texture(Display *display, Window root, Pixmap saved_pixmap, int width, int height, int *texture_width, int *texture_height) {
    Atom xroot = XInternAtom(display, "_XROOTPMAP_ID", False), esetroot = XInternAtom(display, "ESETROOT_PMAP_ID", False);
    Pixmap candidates[3] = { saved_pixmap, root_pixmap_property(display, root, xroot), root_pixmap_property(display, root, esetroot) };
    for (int candidate = 0; candidate <= 3; candidate++) {
        Drawable drawable = candidate < 3 && candidates[candidate] ? candidates[candidate] : root;
        gboolean duplicate = FALSE;
        for (int previous = 0; previous < candidate && previous < 3; previous++) if (candidates[previous] && drawable == candidates[previous]) duplicate = TRUE;
        /* Do not pre-reject the property. Resource ids can be close to this
           client's ids; the guarded X request below is the authoritative test. */
        /* A broken desktop property can contain the root window id itself.
           Still try the root drawable as the final, valid fallback. */
        if (drawable != root && duplicate) continue;
        int capture_width = width, capture_height = height;
        if (drawable != root) {
            DrawableGeometry geometry = { drawable, 0, 0, 0, 0, 0, 0 };
            if (!x11_try(display, get_drawable_geometry, &geometry) || !geometry.width || !geometry.height) continue;
            capture_width = (int)geometry.width; capture_height = (int)geometry.height;
        }
        RootImageRequest request = { display, drawable, (unsigned)capture_width, (unsigned)capture_height, NULL };
        if (!x11_try(display, get_root_image, &request) || !request.image) continue;
        XImage *image = request.image; gsize size = (gsize)capture_width * (gsize)capture_height * 4; guchar *pixels = g_malloc(size);
        for (int y = 0; y < capture_height; y++) for (int x = 0; x < capture_width; x++) { unsigned long pixel = XGetPixel(image, x, y); gsize offset = ((gsize)y * capture_width + x) * 4; pixels[offset + 0] = root_component(pixel, image->red_mask); pixels[offset + 1] = root_component(pixel, image->green_mask); pixels[offset + 2] = root_component(pixel, image->blue_mask); pixels[offset + 3] = 255; }
        XDestroyImage(image); GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, capture_width, capture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels); glBindTexture(GL_TEXTURE_2D, 0); g_free(pixels); if (texture_width) *texture_width = capture_width; if (texture_height) *texture_height = capture_height; return texture;
    }
    return 0;
}

static GLuint image_background_texture(const char *path, int *texture_width, int *texture_height) {
    if (!path || !path[0]) return 0;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, NULL);
    if (!pixbuf) return 0;
    int width = gdk_pixbuf_get_width(pixbuf), height = gdk_pixbuf_get_height(pixbuf), channels = gdk_pixbuf_get_n_channels(pixbuf), rowstride = gdk_pixbuf_get_rowstride(pixbuf);
    GLenum format = channels == 4 ? GL_RGBA : GL_RGB;
    GLuint texture = 0; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glPixelStorei(GL_UNPACK_ROW_LENGTH, rowstride / channels); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, format, GL_UNSIGNED_BYTE, gdk_pixbuf_get_pixels(pixbuf)); glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glBindTexture(GL_TEXTURE_2D, 0); g_object_unref(pixbuf); if (texture_width) *texture_width = width; if (texture_height) *texture_height = height; return texture;
}

static GLuint cinnamon_background_texture(int *texture_width, int *texture_height) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default(); GSettingsSchema *schema = source ? g_settings_schema_source_lookup(source, "org.cinnamon.desktop.background", TRUE) : NULL;
    if (!schema) return 0;
    GSettings *settings = g_settings_new_full(schema, NULL, NULL); char *uri = NULL;
    if (g_settings_schema_has_key(schema, "picture-uri")) uri = g_settings_get_string(settings, "picture-uri");
    if ((!uri || !uri[0]) && g_settings_schema_has_key(schema, "picture-filename")) { g_free(uri); uri = g_settings_get_string(settings, "picture-filename"); }
    char *path = NULL;
    if (uri && g_str_has_prefix(uri, "file:")) { GFile *file = g_file_new_for_uri(uri); path = g_file_get_path(file); g_object_unref(file); }
    else if (uri) path = g_strdup(uri);
    GLuint texture = image_background_texture(path, texture_width, texture_height); g_free(path); g_free(uri); g_object_unref(settings); return texture;
}

static gboolean root_mirror_begin(RootMirror *mirror, Display *display, Window root, int width, int height) {
    memset(mirror, 0, sizeof(*mirror)); mirror->display = display; mirror->root = root; mirror->width = width; mirror->height = height; mirror->shm.shmid = -1;
    int screen = DefaultScreen(display); Visual *visual = DefaultVisual(display, screen); unsigned depth = (unsigned)DefaultDepth(display, screen);
    mirror->xroot_atom = XInternAtom(display, "_XROOTPMAP_ID", False); mirror->esetroot_atom = XInternAtom(display, "ESETROOT_PMAP_ID", False);
    mirror->old_xroot = root_pixmap_property(display, root, mirror->xroot_atom); mirror->old_esetroot = root_pixmap_property(display, root, mirror->esetroot_atom);
    mirror->pixmap = XCreatePixmap(display, root, (unsigned)width, (unsigned)height, depth); mirror->gc = XCreateGC(display, mirror->pixmap, 0, NULL);
    if (XShmQueryExtension(display)) {
        mirror->image = XShmCreateImage(display, visual, depth, ZPixmap, NULL, &mirror->shm, (unsigned)width, (unsigned)height);
        if (mirror->image) mirror->shm.shmid = shmget(IPC_PRIVATE, (size_t)mirror->image->bytes_per_line * (size_t)height, IPC_CREAT | 0600);
        if (mirror->shm.shmid >= 0) {
            mirror->shm.shmaddr = mirror->image->data = shmat(mirror->shm.shmid, NULL, 0); mirror->shm.readOnly = False;
            mirror->use_shm = mirror->shm.shmaddr != (char *)-1 && x11_try(display, attach_shm, &mirror->shm);
            shmctl(mirror->shm.shmid, IPC_RMID, NULL);
            if (!mirror->use_shm && mirror->shm.shmaddr != (char *)-1) shmdt(mirror->shm.shmaddr);
        }
        if (!mirror->use_shm && mirror->image) { mirror->image->data = NULL; XDestroyImage(mirror->image); mirror->image = NULL; }
    }
    if (!mirror->image) { unsigned char *pixels = g_malloc0((gsize)width * (gsize)height * 4); mirror->image = XCreateImage(display, visual, depth, ZPixmap, 0, (char *)pixels, (unsigned)width, (unsigned)height, 32, 0); }
    if (!mirror->pixmap || !mirror->gc || !mirror->image) return FALSE;
    glGenBuffers(1, &mirror->pbo); glBindBuffer(GL_PIXEL_PACK_BUFFER, mirror->pbo); glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)width * height * 4, NULL, GL_STREAM_READ); glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    unsigned long p = (unsigned long)mirror->pixmap; x11_property(display, root, mirror->xroot_atom, XA_PIXMAP, &p, 1); x11_property(display, root, mirror->esetroot_atom, XA_PIXMAP, &p, 1); XSetWindowBackgroundPixmap(display, root, mirror->pixmap); return TRUE;
}

/* Shows the frame read back last time (long finished by now), then starts reading the current one. */
static void root_mirror_update(RootMirror *mirror) {
    if (!mirror->image) return;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, mirror->pbo);
    if (mirror->pending) {
        const char *pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)mirror->width * mirror->height * 4, GL_MAP_READ_BIT);
        if (pixels) {
            gsize row = (gsize)mirror->width * 4; int stride = mirror->image->bytes_per_line;
            for (int y = 0; y < mirror->height; y++) memcpy(mirror->image->data + (gsize)y * stride, pixels + (gsize)(mirror->height - 1 - y) * row, row);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            if (mirror->use_shm) XShmPutImage(mirror->display, mirror->pixmap, mirror->gc, mirror->image, 0, 0, 0, 0, (unsigned)mirror->width, (unsigned)mirror->height, False);
            else XPutImage(mirror->display, mirror->pixmap, mirror->gc, mirror->image, 0, 0, 0, 0, (unsigned)mirror->width, (unsigned)mirror->height);
            /* Updating a pixmap does not repaint pixels already stored in the root. */
            XClearArea(mirror->display, mirror->root, 0, 0, (unsigned)mirror->width, (unsigned)mirror->height, False);
            XFlush(mirror->display);
        }
    }
    glReadPixels(0, 0, mirror->width, mirror->height, GL_BGRA, GL_UNSIGNED_BYTE, NULL); mirror->pending = TRUE;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

/* Puts back the background that was there before, unless its owner has exited and taken it with it. */
static void root_mirror_end(RootMirror *mirror) {
    if (!mirror->display) return;
    Display *display = mirror->display; gboolean old_xroot = pixmap_exists(display, mirror->old_xroot, mirror->pixmap), old_esetroot = pixmap_exists(display, mirror->old_esetroot, mirror->pixmap);
    if (old_xroot) { unsigned long p = (unsigned long)mirror->old_xroot; x11_property(display, mirror->root, mirror->xroot_atom, XA_PIXMAP, &p, 1); XSetWindowBackgroundPixmap(display, mirror->root, mirror->old_xroot); } else XDeleteProperty(display, mirror->root, mirror->xroot_atom);
    if (old_esetroot) { unsigned long p = (unsigned long)mirror->old_esetroot; x11_property(display, mirror->root, mirror->esetroot_atom, XA_PIXMAP, &p, 1); } else XDeleteProperty(display, mirror->root, mirror->esetroot_atom);
    if (mirror->pbo) glDeleteBuffers(1, &mirror->pbo);
    if (mirror->use_shm) { XShmDetach(display, &mirror->shm); XSync(display, False); mirror->image->data = NULL; XDestroyImage(mirror->image); shmdt(mirror->shm.shmaddr); }
    else if (mirror->image) XDestroyImage(mirror->image);
    if (mirror->gc) XFreeGC(display, mirror->gc);
    if (mirror->pixmap) XFreePixmap(display, mirror->pixmap);
    XFlush(display);
}

int wallpaper_main(const char *path, int fps, gboolean vsync, int speed_percent, gboolean chroma_enabled, const float key[3], float smoothing, int opacity_percent) {
    char *source = read_file(path); if (!source) { fprintf(stderr, "glacier-mint: cannot read %s\n", path); return 1; }
    curl_global_init(CURL_GLOBAL_DEFAULT);
    Display *display = XOpenDisplay(NULL); if (!display) { fprintf(stderr, "glacier-mint: DISPLAY is unavailable\n"); g_free(source); return 1; }
    int screen = DefaultScreen(display); Window root = RootWindow(display, screen); int width = DisplayWidth(display, screen), height = DisplayHeight(display, screen);
    int attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_ALPHA_SIZE, 8, None }; XVisualInfo *visual = glXChooseVisual(display, screen, attrs); gboolean alpha_visual = FALSE;
    if (!visual) { int fallback_attrs[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None }; visual = glXChooseVisual(display, screen, fallback_attrs); }
    if (!visual) { fprintf(stderr, "glacier-mint: no GLX visual\n"); XCloseDisplay(display); g_free(source); return 1; }
    int alpha_bits = 0; glXGetConfig(display, visual, GLX_ALPHA_SIZE, &alpha_bits); alpha_visual = alpha_bits >= 8;
    Colormap cmap = XCreateColormap(display, root, visual->visual, AllocNone); XSetWindowAttributes swa = {0}; swa.colormap = cmap; swa.border_pixel = 0; swa.background_pixmap = None;
    /* Keep an ARGB window available as a fallback. The captured desktop image
       is composited directly when available, which also works on WMs that do
       not honor per-pixel alpha for desktop windows. */
    Window window = XCreateWindow(display, root, 0, 0, width, height, 0, visual->depth, InputOutput, visual->visual, CWColormap | CWBorderPixel | CWBackPixmap, &swa);
    Atom desktop = XInternAtom(display, "_NET_WM_WINDOW_TYPE_DESKTOP", False), type = XInternAtom(display, "_NET_WM_WINDOW_TYPE", False); x11_property(display, window, type, XA_ATOM, (unsigned long *)&desktop, 1);
    Atom below = XInternAtom(display, "_NET_WM_STATE_BELOW", False), sticky = XInternAtom(display, "_NET_WM_STATE_STICKY", False), skip_taskbar = XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False), skip_pager = XInternAtom(display, "_NET_WM_STATE_SKIP_PAGER", False), state = XInternAtom(display, "_NET_WM_STATE", False); unsigned long states[] = {(unsigned long)below, (unsigned long)sticky, (unsigned long)skip_taskbar, (unsigned long)skip_pager}; x11_property(display, window, state, XA_ATOM, states, 4);
    Atom desktop_number = XInternAtom(display, "_NET_WM_DESKTOP", False); unsigned long all_workspaces = 0xFFFFFFFFUL; x11_property(display, window, desktop_number, XA_CARDINAL, &all_workspaces, 1);
    Pixmap saved_background = root_pixmap_property(display, root, XInternAtom(display, "_XROOTPMAP_ID", False)); if (!saved_background) saved_background = root_pixmap_property(display, root, XInternAtom(display, "ESETROOT_PMAP_ID", False)); XStoreName(display, window, "Glacier Mint wallpaper");
    GLXContext context = glXCreateContext(display, visual, NULL, True); XFree(visual); if (!context || !glXMakeCurrent(display, window, context)) { fprintf(stderr, "glacier-mint: could not create GL context\n"); XDestroyWindow(display, window); XCloseDisplay(display); g_free(source); return 1; }
    set_vsync(display, screen, window, vsync);
    int background_width = 0, background_height = 0; GLuint background_texture = chroma_enabled ? root_background_texture(display, root, saved_background, width, height, &background_width, &background_height) : 0;
    if (chroma_enabled && !background_texture) background_texture = cinnamon_background_texture(&background_width, &background_height);
    if (chroma_enabled && !background_texture) fprintf(stderr, "glacier-mint: could not capture the desktop background for chroma key%s\n", alpha_visual ? "" : " (no ARGB visual either)");
    XMapWindow(display, window); XLowerWindow(display, window); XFlush(display);
    /* Music-reactive shaders get a beat clock that follows whatever is playing; others keep iBeat at 60 BPM. */
    BeatTracker *beats = shader_uses_audio(source) ? beat_tracker_new() : NULL; BeatClock clock = { 0.0, 60.0 }; BeatState beat_state = { FALSE, 60.0, 0.0, 0.0, 0.0 };
    char *error = NULL; GLuint program = make_program_for_shader(source, path, &error); g_free(source); if (!program) { fprintf(stderr, "glacier-mint: shader error: %s\n", error ? error : "unknown"); g_free(error); if (background_texture) glDeleteTextures(1, &background_texture); beat_tracker_free(beats); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 1; }
    GLuint channels[4]; int channel_widths[4], channel_heights[4], channel_types[4]; load_shader_channels(path, channels, channel_widths, channel_heights, channel_types); ShaderUniforms uniforms; shader_uniforms_init(&uniforms, program);
    gboolean use_background_texture = chroma_enabled && background_texture;
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao); WallpaperWidgets widgets = {0}; widgets.items = g_array_new(FALSE, TRUE, sizeof(WallpaperItem)); widgets.feed = widget_feed_new(FALSE); widgets.program = wallpaper_widget_program(); if (widgets.program) { widgets.screen_uniform = glGetUniformLocation(widgets.program, "screen"); widgets.image_uniform = glGetUniformLocation(widgets.program, "image"); widgets.rect_uniform = glGetUniformLocation(widgets.program, "rect"); glUseProgram(widgets.program); glUniform1i(widgets.image_uniform, 0); } glGenVertexArrays(1, &widgets.vao); glBindVertexArray(widgets.vao); wallpaper_widgets_poll(&widgets); RootMirror mirror; gboolean mirror_ready = !chroma_enabled && root_mirror_begin(&mirror, display, root, width, height); signal(SIGTERM, on_signal); signal(SIGINT, on_signal); gint64 started = g_get_monotonic_time(); int frame = 0; double period = 1.0 / MAX(fps, 1); gint64 next = started;
    gint64 last = started, mirrored = 0, counted_at = started; int counted = 0; gint64 mirror_spent = 0; gboolean report_fps = g_getenv("GLACIER_MINT_DEBUG_FPS") != NULL;
    while (wallpaper_running) { gint64 now = g_get_monotonic_time(); float elapsed = (float)(now - started) / 1000000.0f * CLAMP(speed_percent, 0, 200) / 100.0f; float opacity = CLAMP(opacity_percent, 0, 100) / 100.0f; gboolean chroma_active = chroma_enabled && (alpha_visual || background_texture); wallpaper_widgets_poll(&widgets); glViewport(0, 0, width, height); glClearColor(0.0f, 0.0f, 0.0f, 0.0f); glClear(GL_COLOR_BUFFER_BIT); glUseProgram(program); shader_set_uniforms(&uniforms, width, height, elapsed, frame++); shader_set_effects(&uniforms, chroma_active, key, smoothing, opacity); shader_set_preview_background(&uniforms, FALSE); shader_set_background(&uniforms, background_texture, background_width, background_height, use_background_texture);
        if (beats) { beat_tracker_state(beats, &beat_state); beat_clock_update(&clock, (now - last) / 1e6, now / 1e6, &beat_state); set_audio_uniforms(&uniforms, &clock, &beat_state); } last = now;
        bind_shader_channels(&uniforms, channels, channel_widths, channel_heights, channel_types); glDrawArrays(GL_TRIANGLES, 0, 3); wallpaper_widgets_render(&widgets, width, height); if (mirror_ready && now - mirrored >= 200000) { gint64 before = g_get_monotonic_time(); root_mirror_update(&mirror); mirrored = now; mirror_spent += g_get_monotonic_time() - before; } glXSwapBuffers(display, window);
        if (report_fps && ++counted && now - counted_at >= G_USEC_PER_SEC) { fprintf(stderr, "glacier-mint: %.1f fps, %.0f ms/s copying frames to the desktop background\n", counted * 1e6 / (now - counted_at), mirror_spent / 1000.0); counted = 0; counted_at = now; mirror_spent = 0; }
        /* Keep the average at the limit: a late frame is made up, and only a long stall resets the schedule. */
        gint64 frame_us = (gint64)(period * 1000000.0); next += frame_us; gint64 wait = next - g_get_monotonic_time(); if (wait > 0) g_usleep((gulong)wait); else if (wait < -frame_us) next = g_get_monotonic_time(); }
    beat_tracker_free(beats);
    if (mirror_ready) root_mirror_end(&mirror);
    wallpaper_widgets_clear(&widgets); widget_feed_free(widgets.feed); g_free(widgets.config_text); if (widgets.program) glDeleteProgram(widgets.program); if (widgets.vao) glDeleteVertexArrays(1, &widgets.vao); g_array_free(widgets.items, TRUE); if (background_texture) glDeleteTextures(1, &background_texture); glDeleteProgram(program); free_shader_channels(channels); glXMakeCurrent(display, None, NULL); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 0;
}
