#include "app.h"

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>

#define FEED_FFT 2048
#define FEED_RATE 44100

/* Live data for widgets. Stats and media update every second; audio runs while an EQ widget exists. */
struct WidgetFeed {
    GMutex lock; GCond wake; gboolean running, want_stats, want_media, want_audio; gint stopping;
    GThread *stats_thread, *audio_thread; GPid audio_pid;
    char *player_filter, *art_url; gint64 position_at;
    WidgetSnapshot data; float history[FEED_METRICS][FEED_HISTORY]; int history_next;
};

/* System stats. */

typedef struct { guint64 idle, total, rx, tx; gint64 at; char *gpu_busy, *cpu_temp, *gpu_temp; gboolean probed; } StatsState;

static gboolean read_number(const char *path, double *value) {
    char *text = path ? read_file(path) : NULL; if (!text) return FALSE;
    *value = g_ascii_strtod(text, NULL); g_free(text); return TRUE;
}

static void probe_sensors(StatsState *s) {
    static const char *cpu_names[] = { "coretemp", "k10temp", "zenpower", "cpu_thermal", NULL }, *gpu_names[] = { "amdgpu", "nouveau", "radeon", NULL };
    GDir *dir = g_dir_open("/sys/class/hwmon", 0, NULL); const char *entry;
    while (dir && (entry = g_dir_read_name(dir))) {
        char *name_path = g_build_filename("/sys/class/hwmon", entry, "name", NULL), *name = read_file(name_path); g_free(name_path); if (!name) continue; g_strstrip(name);
        char *input = g_build_filename("/sys/class/hwmon", entry, "temp1_input", NULL);
        if (!s->cpu_temp && g_strv_contains(cpu_names, name) && g_file_test(input, G_FILE_TEST_EXISTS)) s->cpu_temp = g_strdup(input);
        if (!s->gpu_temp && g_strv_contains(gpu_names, name) && g_file_test(input, G_FILE_TEST_EXISTS)) s->gpu_temp = g_strdup(input);
        g_free(input); g_free(name);
    }
    if (dir) g_dir_close(dir);
    dir = g_dir_open("/sys/class/drm", 0, NULL);
    while (dir && !s->gpu_busy && (entry = g_dir_read_name(dir))) {
        if (!g_str_has_prefix(entry, "card") || strchr(entry, '-')) continue;
        char *path = g_build_filename("/sys/class/drm", entry, "device", "gpu_busy_percent", NULL); if (g_file_test(path, G_FILE_TEST_EXISTS)) s->gpu_busy = path; else g_free(path);
    }
    if (dir) g_dir_close(dir);
    s->probed = TRUE;
}

static void sample_stats(WidgetFeed *feed, StatsState *s) {
    if (!s->probed) probe_sensors(s);
    gint64 now = g_get_monotonic_time(); double seconds = s->at ? (now - s->at) / 1e6 : 0; double cpu = 0, gpu = 0, cpu_temp = 0, gpu_temp = 0, uptime = 0, net_down = 0, net_up = 0;
    guint64 mem_total = 0, mem_available = 0, swap_total = 0, swap_free = 0, rx = 0, tx = 0, disk_used = 0, disk_total = 0; double disk = 0;
    char *text = read_file("/proc/stat"); unsigned long long v[8] = {0};
    if (text && sscanf(text, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) >= 4) {
        guint64 idle = v[3] + v[4], total = 0; for (int i = 0; i < 8; i++) total += v[i];
        if (s->total && total > s->total) cpu = 100.0 * (1.0 - (double)(idle - s->idle) / (double)(total - s->total));
        s->idle = idle; s->total = total;
    }
    g_free(text); text = read_file("/proc/meminfo"); char **lines = text ? g_strsplit(text, "\n", -1) : NULL;
    for (char **l = lines; l && *l; l++) {
        unsigned long long kb = 0;
        if (sscanf(*l, "MemTotal: %llu", &kb) == 1) mem_total = kb * 1024; else if (sscanf(*l, "MemAvailable: %llu", &kb) == 1) mem_available = kb * 1024;
        else if (sscanf(*l, "SwapTotal: %llu", &kb) == 1) swap_total = kb * 1024; else if (sscanf(*l, "SwapFree: %llu", &kb) == 1) swap_free = kb * 1024;
    }
    g_strfreev(lines); g_free(text); text = read_file("/proc/net/dev"); lines = text ? g_strsplit(text, "\n", -1) : NULL;
    for (char **l = lines; l && *l; l++) {
        char name[64]; unsigned long long r = 0, t = 0;
        if (sscanf(*l, " %63[^:]: %llu %*u %*u %*u %*u %*u %*u %*u %llu", name, &r, &t) == 3 && strcmp(name, "lo")) { rx += r; tx += t; }
    }
    g_strfreev(lines); g_free(text);
    if (seconds > 0 && rx >= s->rx && tx >= s->tx) { net_down = (rx - s->rx) / seconds; net_up = (tx - s->tx) / seconds; }
    s->rx = rx; s->tx = tx; s->at = now;
    struct statvfs fs; if (!statvfs("/", &fs)) { disk_total = (guint64)fs.f_blocks * fs.f_frsize; disk_used = (guint64)(fs.f_blocks - fs.f_bfree) * fs.f_frsize; guint64 usable = disk_used + (guint64)fs.f_bavail * fs.f_frsize; disk = usable ? 100.0 * disk_used / usable : 0; }
    gboolean has_gpu = read_number(s->gpu_busy, &gpu), has_cpu_temp = read_number(s->cpu_temp, &cpu_temp), has_gpu_temp = read_number(s->gpu_temp, &gpu_temp);
    read_number("/proc/uptime", &uptime);
    g_mutex_lock(&feed->lock); WidgetSnapshot *d = &feed->data;
    d->cpu = CLAMP(cpu, 0.0, 100.0); d->gpu = CLAMP(gpu, 0.0, 100.0); d->has_gpu = has_gpu; d->has_cpu_temp = has_cpu_temp; d->has_gpu_temp = has_gpu_temp; d->cpu_temp = cpu_temp / 1000.0; d->gpu_temp = gpu_temp / 1000.0;
    d->ram_total = mem_total; d->ram_used = mem_total > mem_available ? mem_total - mem_available : 0; d->ram = mem_total ? 100.0 * d->ram_used / mem_total : 0;
    d->swap_total = swap_total; d->swap_used = swap_total > swap_free ? swap_total - swap_free : 0; d->swap = swap_total ? 100.0 * d->swap_used / swap_total : 0;
    d->disk = disk; d->disk_used = disk_used; d->disk_total = disk_total; d->net_down = net_down; d->net_up = net_up; d->uptime = uptime;
    float values[FEED_METRICS] = { (float)d->cpu, (float)d->gpu, (float)d->ram, (float)(net_down + net_up) };
    for (int m = 0; m < FEED_METRICS; m++) feed->history[m][feed->history_next] = values[m];
    feed->history_next = (feed->history_next + 1) % FEED_HISTORY; d->history_count = MIN(d->history_count + 1, FEED_HISTORY);
    g_mutex_unlock(&feed->lock);
}

/* Now playing, through MPRIS on the session bus. */

static cairo_surface_t *surface_from_pixbuf(GdkPixbuf *pixbuf) {
    int width = gdk_pixbuf_get_width(pixbuf), height = gdk_pixbuf_get_height(pixbuf), channels = gdk_pixbuf_get_n_channels(pixbuf), stride = gdk_pixbuf_get_rowstride(pixbuf); gboolean alpha = gdk_pixbuf_get_has_alpha(pixbuf); const guchar *pixels = gdk_pixbuf_read_pixels(pixbuf);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height); cairo_surface_flush(surface); unsigned char *out = cairo_image_surface_get_data(surface); int out_stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
        const guchar *p = pixels + y * stride + x * channels; guint32 a = alpha ? p[3] : 255, r = p[0] * a / 255, g = p[1] * a / 255, b = p[2] * a / 255;
        *(guint32 *)(out + y * out_stride + x * 4) = (a << 24) | (r << 16) | (g << 8) | b;
    }
    cairo_surface_mark_dirty(surface); return surface;
}

static size_t art_write(void *ptr, size_t size, size_t count, void *data) {
    GByteArray *bytes = data; if (bytes->len + size * count > 16 * 1024 * 1024) return 0;
    g_byte_array_append(bytes, ptr, size * count); return size * count;
}

static int art_progress(void *data, curl_off_t total, curl_off_t now, curl_off_t up_total, curl_off_t up_now) { (void)total; (void)now; (void)up_total; (void)up_now; return g_atomic_int_get(&((WidgetFeed *)data)->stopping); }

static cairo_surface_t *load_art(WidgetFeed *feed, const char *url) {
    GdkPixbuf *pixbuf = NULL;
    if (g_str_has_prefix(url, "file://")) { char *path = g_filename_from_uri(url, NULL, NULL); if (path) pixbuf = gdk_pixbuf_new_from_file_at_scale(path, 512, 512, TRUE, NULL); g_free(path); }
    else if (g_str_has_prefix(url, "https://") || g_str_has_prefix(url, "http://")) {
        CURL *curl = curl_easy_init(); GByteArray *bytes = g_byte_array_new();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, url); curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, art_write); curl_easy_setopt(curl, CURLOPT_WRITEDATA, bytes); curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L); curl_easy_setopt(curl, CURLOPT_USERAGENT, "Glacier-Mint/1.0"); curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, art_progress); curl_easy_setopt(curl, CURLOPT_XFERINFODATA, feed);
            long status = 0; if (curl_easy_perform(curl) == CURLE_OK && (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status), status < 400) && bytes->len) {
                GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
                if (gdk_pixbuf_loader_write(loader, bytes->data, bytes->len, NULL) && gdk_pixbuf_loader_close(loader, NULL)) { GdkPixbuf *full = gdk_pixbuf_loader_get_pixbuf(loader); if (full) { int w = gdk_pixbuf_get_width(full), h = gdk_pixbuf_get_height(full); double scale = MIN(1.0, 512.0 / MAX(w, h)); pixbuf = gdk_pixbuf_scale_simple(full, MAX(1, (int)(w * scale)), MAX(1, (int)(h * scale)), GDK_INTERP_BILINEAR); } }
                else gdk_pixbuf_loader_close(loader, NULL);
                g_object_unref(loader);
            }
            curl_easy_cleanup(curl);
        }
        g_byte_array_unref(bytes);
    }
    if (!pixbuf) return NULL;
    cairo_surface_t *surface = surface_from_pixbuf(pixbuf); g_object_unref(pixbuf); return surface;
}

static char *variant_text(GVariant *dict, const char *key) {
    GVariant *value = g_variant_lookup_value(dict, key, NULL); char *text = NULL; if (!value) return g_strdup("");
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) text = g_variant_dup_string(value, NULL);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) { const char **list = g_variant_get_strv(value, NULL); text = g_strjoinv(", ", (char **)list); g_free(list); }
    g_variant_unref(value); return text ? text : g_strdup("");
}

static gint64 variant_number(GVariant *dict, const char *key) {
    GVariant *value = g_variant_lookup_value(dict, key, NULL); gint64 number = 0; if (!value) return 0;
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT64)) number = g_variant_get_int64(value); else if (g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64)) number = (gint64)g_variant_get_uint64(value);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_INT32)) number = g_variant_get_int32(value); else if (g_variant_is_of_type(value, G_VARIANT_TYPE_DOUBLE)) number = (gint64)g_variant_get_double(value);
    g_variant_unref(value); return number;
}

static void sample_media(WidgetFeed *feed, GDBusConnection *bus, const char *filter) {
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
    GVariant *best = NULL; char *best_name = NULL; int best_rank = -1; char *wanted = g_ascii_strdown(filter ? filter : "", -1);
    if (reply) {
        GVariantIter *names; const char *name; g_variant_get(reply, "(as)", &names);
        while (g_variant_iter_next(names, "&s", &name)) {
            if (!g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) continue;
            char *lower = g_ascii_strdown(name, -1); gboolean match = !wanted[0] || strstr(lower, wanted); g_free(lower); if (!match) continue;
            GVariant *props = g_dbus_connection_call_sync(bus, name, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
            if (!props) continue;
            GVariant *dict = g_variant_get_child_value(props, 0); char *status = variant_text(dict, "PlaybackStatus"); int rank = !strcmp(status, "Playing") ? 2 : !strcmp(status, "Paused") ? 1 : 0; g_free(status); g_variant_unref(props);
            if (rank > best_rank) { if (best) g_variant_unref(best); g_free(best_name); best = dict; best_name = g_strdup(name); best_rank = rank; } else g_variant_unref(dict);
        }
        g_variant_iter_free(names); g_variant_unref(reply);
    }
    g_free(wanted);
    char *title = NULL, *artist = NULL, *album = NULL, *art_url = NULL, *player = NULL; gint64 length = 0, position = 0; gboolean has_media = best_rank >= 1, playing = best_rank == 2;
    if (best) {
        GVariant *metadata = g_variant_lookup_value(best, "Metadata", G_VARIANT_TYPE_VARDICT);
        if (metadata) { title = variant_text(metadata, "xesam:title"); artist = variant_text(metadata, "xesam:artist"); album = variant_text(metadata, "xesam:album"); art_url = variant_text(metadata, "mpris:artUrl"); length = variant_number(metadata, "mpris:length"); g_variant_unref(metadata); }
        position = variant_number(best, "Position"); const char *suffix = best_name + strlen("org.mpris.MediaPlayer2."); player = g_strndup(suffix, strcspn(suffix, "."));
        g_variant_unref(best); if (!title || !title[0]) has_media = FALSE;
    }
    g_free(best_name);
    g_mutex_lock(&feed->lock); gboolean art_changed = g_strcmp0(art_url, feed->art_url) != 0; g_mutex_unlock(&feed->lock);
    cairo_surface_t *art = art_changed && art_url && art_url[0] ? load_art(feed, art_url) : NULL;
    g_mutex_lock(&feed->lock); WidgetSnapshot *d = &feed->data;
    g_free(d->title); g_free(d->artist); g_free(d->album); g_free(d->player); d->title = title; d->artist = artist; d->album = album; d->player = player;
    d->has_media = has_media; d->playing = playing; d->length_us = length; d->position_us = position; feed->position_at = g_get_monotonic_time();
    if (art_changed) { if (d->art) cairo_surface_destroy(d->art); d->art = art; g_free(feed->art_url); feed->art_url = art_url; } else g_free(art_url);
    g_mutex_unlock(&feed->lock);
}

static gpointer stats_thread(gpointer data) {
    WidgetFeed *feed = data; StatsState state = {0}; GDBusConnection *bus = NULL; gint64 last_stats = 0, last_media = 0;
    g_mutex_lock(&feed->lock);
    while (feed->running) {
        gboolean stats = feed->want_stats, media = feed->want_media; char *filter = g_strdup(feed->player_filter); g_mutex_unlock(&feed->lock);
        gint64 now = g_get_monotonic_time();
        if (stats && now - last_stats >= 950000) { sample_stats(feed, &state); last_stats = now; }
        if (media && now - last_media >= 950000) { if (!bus) bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL); if (bus) sample_media(feed, bus, filter); last_media = now; }
        g_free(filter); g_mutex_lock(&feed->lock);
        if (feed->running) g_cond_wait_until(&feed->wake, &feed->lock, g_get_monotonic_time() + 250000);
    }
    g_mutex_unlock(&feed->lock); if (bus) g_object_unref(bus); g_free(state.gpu_busy); g_free(state.cpu_temp); g_free(state.gpu_temp); return NULL;
}

/* Audio: captures what is playing from the default output's monitor and turns it into log-spaced bands. */

static void fft(float *re, float *im, int n) {
    for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (int length = 2; length <= n; length <<= 1) {
        double angle = -2.0 * G_PI / length;
        for (int i = 0; i < n; i += length) for (int k = 0; k < length / 2; k++) {
            float wr = (float)cos(angle * k), wi = (float)sin(angle * k), *a_re = &re[i + k], *a_im = &im[i + k], *b_re = &re[i + k + length / 2], *b_im = &im[i + k + length / 2];
            float vr = *b_re * wr - *b_im * wi, vi = *b_re * wi + *b_im * wr; *b_re = *a_re - vr; *b_im = *a_im - vi; *a_re += vr; *a_im += vi;
        }
    }
}

static void analyze(const float *samples, float *bands) {
    static const double low = 40.0, high = 16000.0; float re[FEED_FFT], im[FEED_FFT];
    for (int i = 0; i < FEED_FFT; i++) { re[i] = samples[i] * (float)(0.5 * (1.0 - cos(2.0 * G_PI * i / (FEED_FFT - 1)))); im[i] = 0; }
    fft(re, im, FEED_FFT);
    for (int b = 0; b < FEED_BANDS; b++) {
        double f0 = low * pow(high / low, (double)b / FEED_BANDS), f1 = low * pow(high / low, (double)(b + 1) / FEED_BANDS), sum = 0;
        int first = (int)floor(f0 * FEED_FFT / FEED_RATE), last = MAX(first + 1, (int)ceil(f1 * FEED_FFT / FEED_RATE));
        for (int k = first; k < last && k < FEED_FFT / 2; k++) sum += (double)re[k] * re[k] + (double)im[k] * im[k];
        double magnitude = sqrt(sum / MAX(1, last - first)) / (FEED_FFT / 4.0), db = 20.0 * log10(magnitude + 1e-9) + 3.0 * log2(sqrt(f0 * f1) / 440.0);
        bands[b] = (float)CLAMP((db + 62.0) / 54.0, 0.0, 1.0);
    }
}

static gboolean audio_wanted(WidgetFeed *feed) { g_mutex_lock(&feed->lock); gboolean wanted = feed->running && feed->want_audio; g_mutex_unlock(&feed->lock); return wanted; }

static void audio_pause(WidgetFeed *feed, gint64 usec) {
    g_mutex_lock(&feed->lock); gint64 until = g_get_monotonic_time() + usec;
    while (feed->running && feed->want_audio && g_get_monotonic_time() < until) g_cond_wait_until(&feed->wake, &feed->lock, until);
    g_mutex_unlock(&feed->lock);
}

static void audio_set_bands(WidgetFeed *feed, const float *levels, gboolean live) {
    g_mutex_lock(&feed->lock);
    for (int b = 0; b < FEED_BANDS; b++) feed->data.bands[b] = levels ? (levels[b] > feed->data.bands[b] ? levels[b] : feed->data.bands[b] * 0.6f + levels[b] * 0.4f) : 0.0f;
    feed->data.audio_live = live; g_mutex_unlock(&feed->lock);
}

static gpointer audio_thread(gpointer data) {
    WidgetFeed *feed = data; float *ring = g_new0(float, FEED_FFT), levels[FEED_BANDS]; char pending[4]; int pending_len = 0;
    while (audio_wanted(feed)) {
        char *argv[] = { "parec", "--device=@DEFAULT_MONITOR@", "--raw", "--format=float32le", "--rate=44100", "--channels=1", "--latency-msec=25", "--client-name=Glacier Mint", NULL }; GPid pid = 0; int out = -1;
        if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &pid, NULL, &out, NULL, NULL)) { audio_set_bands(feed, NULL, FALSE); audio_pause(feed, 5 * G_USEC_PER_SEC); continue; }
        g_mutex_lock(&feed->lock); feed->audio_pid = pid; g_mutex_unlock(&feed->lock);
        int fresh = 0; char buffer[8192];
        while (audio_wanted(feed)) {
            struct pollfd poll_fd = { out, POLLIN, 0 }; if (poll(&poll_fd, 1, 100) <= 0) continue;
            ssize_t n = read(out, buffer + pending_len, sizeof(buffer) - pending_len); if (n <= 0) break;
            memcpy(buffer, pending, pending_len); n += pending_len; int count = (int)(n / 4); pending_len = (int)(n % 4); memcpy(pending, buffer + count * 4, pending_len);
            if (count >= FEED_FFT) { memcpy(ring, buffer + (count - FEED_FFT) * 4, FEED_FFT * 4); } else { memmove(ring, ring + count, (FEED_FFT - count) * 4); memcpy(ring + FEED_FFT - count, buffer, count * 4); }
            fresh += count; if (fresh < FEED_RATE / 60) continue;
            fresh = 0; analyze(ring, levels); audio_set_bands(feed, levels, TRUE);
        }
        kill(pid, SIGTERM); close(out); waitpid(pid, NULL, 0); g_spawn_close_pid(pid); pending_len = 0;
        g_mutex_lock(&feed->lock); feed->audio_pid = 0; g_mutex_unlock(&feed->lock);
        audio_set_bands(feed, NULL, FALSE); audio_pause(feed, 2 * G_USEC_PER_SEC);
    }
    g_free(ring); return NULL;
}

/* Public API. */

WidgetFeed *widget_feed_new(gboolean editing) {
    WidgetFeed *feed = g_new0(WidgetFeed, 1); g_mutex_init(&feed->lock); g_cond_init(&feed->wake); feed->running = TRUE; feed->data.editing = editing;
    feed->stats_thread = g_thread_new("widget-stats", stats_thread, feed);
    return feed;
}

void widget_feed_sync(WidgetFeed *feed, GPtrArray *widgets) {
    if (!feed) return;
    gboolean stats = FALSE, media = FALSE, audio = FALSE; const char *player = "";
    for (guint i = 0; widgets && i < widgets->len; i++) {
        DeskWidget *widget = g_ptr_array_index(widgets, i);
        if (!strcmp(widget->kind, "stats")) stats = TRUE;
        else if (!strcmp(widget->kind, "eq")) audio = TRUE;
        else if (!strcmp(widget->kind, "playback")) { media = TRUE; if (!player[0]) player = desk_widget_get(widget, "player"); }
    }
    g_mutex_lock(&feed->lock);
    feed->want_stats = stats; feed->want_media = media; g_free(feed->player_filter); feed->player_filter = g_strdup(player);
    GThread *stop_audio = !audio && feed->audio_thread ? feed->audio_thread : NULL; gboolean start_audio = audio && !feed->audio_thread;
    feed->want_audio = audio; if (stop_audio) { feed->audio_thread = NULL; if (feed->audio_pid) kill(feed->audio_pid, SIGTERM); }
    g_cond_broadcast(&feed->wake); g_mutex_unlock(&feed->lock);
    if (stop_audio) g_thread_join(stop_audio);
    if (start_audio) feed->audio_thread = g_thread_new("widget-audio", audio_thread, feed);
}

void widget_feed_snapshot(WidgetFeed *feed, WidgetSnapshot *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot)); if (!feed) return;
    g_mutex_lock(&feed->lock); *snapshot = feed->data;
    snapshot->title = g_strdup(feed->data.title ? feed->data.title : ""); snapshot->artist = g_strdup(feed->data.artist ? feed->data.artist : ""); snapshot->album = g_strdup(feed->data.album ? feed->data.album : ""); snapshot->player = g_strdup(feed->data.player ? feed->data.player : "");
    snapshot->art = feed->data.art ? cairo_surface_reference(feed->data.art) : NULL;
    if (feed->data.playing) snapshot->position_us += g_get_monotonic_time() - feed->position_at;
    if (snapshot->length_us > 0) snapshot->position_us = CLAMP(snapshot->position_us, 0, snapshot->length_us);
    for (int m = 0; m < FEED_METRICS; m++) for (int i = 0; i < feed->data.history_count; i++) snapshot->history[m][i] = feed->history[m][(feed->history_next - feed->data.history_count + i + FEED_HISTORY) % FEED_HISTORY];
    g_mutex_unlock(&feed->lock);
}

void widget_snapshot_clear(WidgetSnapshot *snapshot) {
    g_free(snapshot->title); g_free(snapshot->artist); g_free(snapshot->album); g_free(snapshot->player);
    if (snapshot->art) cairo_surface_destroy(snapshot->art);
    memset(snapshot, 0, sizeof(*snapshot));
}

void widget_feed_free(WidgetFeed *feed) {
    if (!feed) return;
    g_atomic_int_set(&feed->stopping, 1); g_mutex_lock(&feed->lock); feed->running = FALSE; feed->want_audio = FALSE;
    if (feed->audio_pid) kill(feed->audio_pid, SIGTERM);
    g_cond_broadcast(&feed->wake); g_mutex_unlock(&feed->lock);
    g_thread_join(feed->stats_thread); if (feed->audio_thread) g_thread_join(feed->audio_thread);
    g_free(feed->data.title); g_free(feed->data.artist); g_free(feed->data.album); g_free(feed->data.player); if (feed->data.art) cairo_surface_destroy(feed->data.art);
    g_free(feed->player_filter); g_free(feed->art_url); g_mutex_clear(&feed->lock); g_cond_clear(&feed->wake); g_free(feed);
}
