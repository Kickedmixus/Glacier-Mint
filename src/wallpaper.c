#include "app.h"

#include <GL/glx.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>

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
    char *error = NULL; GLuint program = make_program(source, &error); g_free(source); if (!program) { fprintf(stderr, "glacier-mint: shader error: %s\n", error ? error : "unknown"); g_free(error); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 1; }
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao); RootMirror mirror; gboolean mirror_ready = root_mirror_begin(&mirror, display, root, width, height); signal(SIGTERM, on_signal); signal(SIGINT, on_signal); gint64 started = g_get_monotonic_time(); int frame = 0; double period = 1.0 / MAX(fps, 1); gint64 next = started;
    while (wallpaper_running) { gint64 now = g_get_monotonic_time(); float elapsed = (float)(now - started) / 1000000.0f; glViewport(0, 0, width, height); glUseProgram(program); set_uniforms(program, width, height, elapsed, frame++); glDrawArrays(GL_TRIANGLES, 0, 3); if (mirror_ready && frame % 6 == 0) root_mirror_update(&mirror); glXSwapBuffers(display, window); next += (gint64)(period * 1000000.0); gint64 wait = next - g_get_monotonic_time(); if (wait > 0) g_usleep((gulong)wait); else next = g_get_monotonic_time(); }
    if (mirror_ready) root_mirror_end(&mirror);
    glDeleteProgram(program); glXMakeCurrent(display, None, NULL); glXDestroyContext(display, context); XDestroyWindow(display, window); XCloseDisplay(display); return 0;
}
