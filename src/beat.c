#include "app.h"

#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Tempo and beat-phase tracking for music-reactive shaders (iBeat, iBPM, iAudioLevel, iAudioBass).
   A port of the BeatTracker in shader-wallpaper.py, so shaders written for it behave the same here:
   a low-pass filter and an onset envelope per 16 ms frame, an autocorrelation tempo search twice a
   second, and a tempo that is locked per song once there is ~10 s of evidence. A new song is told
   apart by the media player's track (MPRIS) or by a silent gap. */

#define BEAT_RATE 8000
#define BEAT_HOP 128
#define BEAT_FPS ((double)BEAT_RATE / BEAT_HOP)
#define BEAT_HISTORY 500
#define BEAT_LONG_HISTORY 1500
#define BEAT_MIN_BPM 70.0
#define BEAT_MAX_BPM 180.0
#define BEAT_MIN_CONFIDENCE 0.15
#define BEAT_PRIOR_WIDTH 1.0
#define BEAT_PHASE_MIX 0.1
#define BEAT_SILENCE 1e-6
#define BEAT_HOLD 3.0
#define BEAT_GAP 1.5
#define BEAT_LOCK_AFTER 20
#define BEAT_RELOCK_AFTER 20.0
#define BEAT_IDLE_BPM 60.0
#define BEAT_LAGS 64
#define BEAT_WRAP 512.0

typedef struct { double *data; int capacity, count, start; } Ring;

struct BeatTracker {
    GMutex lock; GCond wake; gboolean running; GThread *capture_thread, *player_thread; GPid pid; BeatState state; char *player_key;
    double lp_a, hp_a, lp1, lp2, hp, refs[3], prev[3], prev_low, level, bass;
    Ring onsets, kicks, longer; long frame;
    double tempi[4]; int tempi_count, tempi_next;
    double bpm, evidence[BEAT_LAGS]; int evidence_count; double locked; gboolean disagreeing; double disagree_since; int phase_votes;
    double confidence, last_confident, last_sound, beat_time; char *song_key;
};

static void ring_init(Ring *ring, int capacity) { ring->data = g_new0(double, capacity); ring->capacity = capacity; ring->count = ring->start = 0; }
static void ring_push(Ring *ring, double value) {
    if (ring->count < ring->capacity) ring->data[(ring->start + ring->count++) % ring->capacity] = value;
    else { ring->data[ring->start] = value; ring->start = (ring->start + 1) % ring->capacity; }
}
static double ring_last(const Ring *ring) { return ring->data[(ring->start + ring->count - 1) % ring->capacity]; }
/* The newest `count` values, oldest first. */
static double *ring_tail(const Ring *ring, int count) {
    count = MIN(count, ring->count); double *out = g_new(double, MAX(count, 1));
    for (int i = 0; i < count; i++) out[i] = ring->data[(ring->start + ring->count - count + i) % ring->capacity];
    return out;
}

static double wrap_one(double value) { return value - floor(value); }

static double fold_octave(double bpm, double anchor) {
    while (bpm > anchor * 1.414) bpm /= 2.0;
    while (bpm < anchor / 1.414) bpm *= 2.0;
    return bpm;
}

static int compare_doubles(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median(const double *values, int count) {
    double sorted[8]; memcpy(sorted, values, count * sizeof(double)); qsort(sorted, count, sizeof(double), compare_doubles);
    return count % 2 ? sorted[count / 2] : (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0;
}

/* Sub-step position of the peak at index i (parabolic interpolation). */
static double peak(const double *values, int i) {
    double y0 = values[i - 1], y1 = values[i], y2 = values[i + 1], curve = y0 - 2.0 * y1 + y2;
    return i + (curve < 0 ? 0.5 * (y0 - y2) / curve : 0.0);
}

static double *demeaned(const Ring *ring, int *count) {
    double *x = ring_tail(ring, ring->count), mean = 0; *count = ring->count;
    for (int i = 0; i < *count; i++) mean += x[i];
    mean /= MAX(1, *count); for (int i = 0; i < *count; i++) x[i] -= mean;
    return x;
}

static double autocorrelation(const double *x, int n, int lag) {
    double sum = 0; for (int i = 0; i + lag < n; i++) sum += x[i + lag] * x[i];
    return sum / (n - lag);
}

static void beat_init(BeatTracker *b) {
    b->lp_a = 1.0 - exp(-2.0 * G_PI * 150.0 / BEAT_RATE); b->hp_a = 1.0 - exp(-2.0 * G_PI * 1500.0 / BEAT_RATE);
    b->refs[0] = BEAT_SILENCE; b->refs[1] = BEAT_SILENCE * 0.1; b->refs[2] = BEAT_SILENCE * 0.01;
    ring_init(&b->onsets, BEAT_HISTORY); ring_init(&b->kicks, BEAT_HISTORY); ring_init(&b->longer, BEAT_LONG_HISTORY);
    b->bpm = BEAT_IDLE_BPM; b->last_confident = b->last_sound = -1e9; b->state.bpm = BEAT_IDLE_BPM;
}

static void new_song(BeatTracker *b) {
    b->onsets.count = b->kicks.count = b->longer.count = 0; b->tempi_count = b->tempi_next = 0;
    memset(b->evidence, 0, sizeof(b->evidence)); b->evidence_count = 0; b->locked = 0; b->disagreeing = FALSE;
}

/* Time of a recent beat: the offset whose beat grid best lines up with the kicks. */
static double beat_phase(BeatTracker *b, double bpm, double t, double seconds, double decay) {
    double period = 60.0 * BEAT_FPS / bpm; int n = MIN((int)(seconds * BEAT_FPS), b->kicks.count), steps = MAX(1, (int)period); double *k = ring_tail(&b->kicks, n), *scores = g_new0(double, steps);
    for (int offset = 0; offset < steps; offset++) {
        double total = 0, weight = 1, pos = offset;
        while (pos < n - 1) { double i = n - 1 - pos; int i0 = MIN((int)i, n - 2); total += weight * (k[i0] + (k[i0 + 1] - k[i0]) * (i - i0)); weight *= decay; pos += period; }
        scores[offset] = total;
    }
    int best = 0; for (int o = 1; o < steps; o++) if (scores[o] > scores[best]) best = o;
    double y0 = scores[(best - 1 + steps) % steps], y1 = scores[best], y2 = scores[(best + 1) % steps], curve = y0 - 2.0 * y1 + y2;
    double offset = best + (curve < 0 ? 0.5 * (y0 - y2) / curve : 0.0);
    g_free(k); g_free(scores); return t - offset / BEAT_FPS;
}

/* Sharpen a beat period (in frames) with the song's longer history: the autocorrelation 4, 8 and 16 beats away pins it down. */
static double refine_period(BeatTracker *b, double period) {
    static const int multiples[] = { 4, 8, 16 }; int n; double *x = demeaned(&b->longer, &n);
    for (guint m = 0; m < G_N_ELEMENTS(multiples); m++) {
        int centre = (int)lround(multiples[m] * period); if (centre + 3 > n * 0.6) break;
        double ac[7]; for (int k = 0; k < 7; k++) ac[k] = autocorrelation(x, n, centre - 3 + k);
        int best = 1; for (int k = 2; k <= 5; k++) if (ac[k] > ac[best]) best = k;
        double refined = (centre - 3 + peak(ac, best)) / multiples[m];
        if (fabs(refined / period - 1.0) > 0.02) break;
        period = refined;
    }
    g_free(x); return period;
}

static void lock_tempo(BeatTracker *b, double t, int lo, int hi) {
    int best = lo; for (int lag = lo + 1; lag <= hi; lag++) if (b->evidence[lag] > b->evidence[best]) best = lag;
    double bpm = 60.0 * BEAT_FPS / refine_period(b, peak(b->evidence, best));
    if (fabs(bpm - round(bpm)) < 0.2) bpm = round(bpm);
    b->locked = b->bpm = bpm; b->beat_time = beat_phase(b, bpm, t, 4.0, 0.85); b->last_confident = t;
}

/* With the tempo locked, keep the beat phase aligned and notice a steadily different tempo (a DJ mix moving on). */
static void follow_locked(BeatTracker *b, double t, const double *ac, int top, double estimate) {
    double period = 60.0 * BEAT_FPS / b->locked; int i = (int)period; if (i + 1 >= top) return;
    double here = ac[i] + (ac[i + 1] - ac[i]) * (period - i); b->confidence = MAX(0.0, here / ac[0]);
    if (b->confidence >= BEAT_MIN_CONFIDENCE) {
        b->last_confident = t; double beat_time = beat_phase(b, b->locked, t, 8.0, 0.93), shift = wrap_one((beat_time - b->beat_time) * b->locked / 60.0 + 0.5) - 0.5;
        if (fabs(shift) < 0.15 || b->phase_votes >= 3) { b->beat_time = beat_time; b->phase_votes = 0; } else b->phase_votes++;
    }
    if (isnan(estimate)) return;
    double ratio = fold_octave(estimate, b->locked) / b->locked;
    if (fabs(ratio - 1.0) < 0.04 || fabs(ratio / (4.0 / 3.0) - 1.0) < 0.04 || fabs(ratio / 0.75 - 1.0) < 0.04) b->disagreeing = FALSE;
    else if (!b->disagreeing) { b->disagreeing = TRUE; b->disagree_since = t; }
    else if (t - b->disagree_since > BEAT_RELOCK_AFTER) new_song(b);
}

static double tempo_score(const double *ac, int top, int lag) {
    static const int beats[] = { 1, 2, 3, 4 }; static const double weights[] = { 1.0, 0.5, 0.33, 1.0 }; double total = 0;
    for (int h = 0; h < 4; h++) {
        int c = beats[h] * lag, r = (beats[h] + 1) / 2; if (c + r >= top) continue;
        double best = ac[c - r]; for (int k = c - r + 1; k <= c + r; k++) best = MAX(best, ac[k]);
        total += weights[h] * best;
    }
    double octaves = log2(60.0 * BEAT_FPS / lag / 120.0) / BEAT_PRIOR_WIDTH; return total * exp(-0.5 * octaves * octaves);
}

/* Tempo from the autocorrelation of the onset envelope. */
static void estimate_tempo(BeatTracker *b, double t) {
    int n; double *x = demeaned(&b->onsets, &n); int lo = (int)(60.0 * BEAT_FPS / BEAT_MAX_BPM), hi = (int)ceil(60.0 * BEAT_FPS / BEAT_MIN_BPM), top = MIN(n - n / 4, 4 * (hi + 1) + 3);
    double *ac = g_new0(double, top); for (int lag = 0; lag < top; lag++) ac[lag] = autocorrelation(x, n, lag);
    g_free(x);
    if (ac[0] <= 0.0 || t - b->last_sound > 1.0) { b->confidence = 0; g_free(ac); return; }
    double scores[BEAT_LAGS] = { 0 }; for (int lag = lo - 1; lag <= hi + 1; lag++) scores[lag] = tempo_score(ac, top, lag);
    int best = lo; for (int lag = lo + 1; lag <= hi; lag++) if (scores[lag] > scores[best]) best = lag;
    double estimate = 60.0 * BEAT_FPS / peak(scores, best), confidence = MAX(0.0, ac[best] / ac[0]); gboolean confident = confidence >= BEAT_MIN_CONFIDENCE;
    if (b->locked > 0) { follow_locked(b, t, ac, top, confident ? estimate : NAN); g_free(ac); return; }
    b->confidence = confidence; b->tempi[b->tempi_next] = confident ? estimate : NAN; b->tempi_next = (b->tempi_next + 1) % 4; b->tempi_count = MIN(b->tempi_count + 1, 4);
    if (confident) { for (int lag = lo - 1; lag <= hi + 1; lag++) b->evidence[lag] += MAX(0.0, scores[lag]) / ac[0]; b->evidence_count++; }
    g_free(ac);
    /* Provisional tempo: one that several estimates in a row agree on (double/half time counts as the same). */
    double recent[4], agreeing[4]; int count = 0, agree = 0;
    for (int i = 0; i < b->tempi_count; i++) { double value = b->tempi[(b->tempi_next - b->tempi_count + i + 4) % 4]; if (!isnan(value)) recent[count++] = value; }
    if (count < 3) return;
    double anchor = t - b->last_confident < BEAT_HOLD ? b->bpm : median(recent, count);
    for (int i = 0; i < count; i++) recent[i] = fold_octave(recent[i], anchor);
    double middle = median(recent, count); for (int i = 0; i < count; i++) if (fabs(recent[i] / middle - 1.0) < 0.04) agreeing[agree++] = recent[i];
    if (agree < 3) return;
    b->bpm = median(agreeing, agree); b->last_confident = t; b->beat_time = beat_phase(b, b->bpm, t, 4.0, 0.85);
    if (b->evidence_count >= BEAT_LOCK_AFTER) lock_tempo(b, t, lo, hi);
}

static void check_song(BeatTracker *b, double t) {
    g_mutex_lock(&b->lock); char *key = g_strdup(b->player_key); g_mutex_unlock(&b->lock);
    if (g_strcmp0(key, b->song_key)) { g_free(b->song_key); b->song_key = key; if (key) { new_song(b); return; } }
    else g_free(key);
    if (!b->song_key && t - b->last_sound > BEAT_GAP && (b->locked > 0 || b->evidence_count)) new_song(b);
}

/* Analyse one block of samples whose last sample played at time t (seconds). */
static void beat_process(BeatTracker *b, const gint16 *samples, int count, double t) {
    double a = b->lp_a, ah = b->hp_a, l1 = b->lp1, l2 = b->lp2, lh = b->hp, full = 0, low = 0, high = 0;
    for (int i = 0; i < count; i++) { double x = samples[i]; l1 += a * (x - l1); l2 += a * (l1 - l2); lh += ah * (x - lh); full += x * x; low += l2 * l2; high += (x - lh) * (x - lh); }
    b->lp1 = l1; b->lp2 = l2; b->hp = lh;
    double norm = 1.0 / (count * 32768.0 * 32768.0), energy[3] = { full * norm, low * norm, high * norm }, floors[3] = { BEAT_SILENCE, BEAT_SILENCE * 0.1, BEAT_SILENCE * 0.01 }, level = 0, bass = 0;
    /* Levels relative to the recent peak, so quiet and loud songs both react. */
    for (int k = 0; k < 3; k++) b->refs[k] = MAX(energy[k], MAX(b->refs[k] * 0.998, floors[k]));
    if (energy[0] >= BEAT_SILENCE) { b->last_sound = t; level = sqrt(energy[0] / b->refs[0]); bass = sqrt(energy[1] / b->refs[1]); }
    b->level += (level - b->level) * (level > b->level ? 0.5 : 0.05); b->bass += (bass - b->bass) * (bass > b->bass ? 0.7 : 0.15);
    /* Onsets: how much each band's compressed energy just rose. The phase signal favours the kick. */
    double onset = 0; for (int k = 0; k < 3; k++) { double compressed = log1p(100.0 * energy[k] / b->refs[k]); onset += MAX(0.0, compressed - b->prev[k]); b->prev[k] = compressed; }
    ring_push(&b->onsets, onset); ring_push(&b->kicks, MAX(0.0, energy[1] - b->prev_low) / b->refs[1] + BEAT_PHASE_MIX * ring_last(&b->onsets)); ring_push(&b->longer, onset); b->prev_low = energy[1];
    if (++b->frame % (int)(BEAT_FPS / 2) == 0) { check_song(b, t); if (b->onsets.count >= 4 * BEAT_FPS) estimate_tempo(b, t); }
    BeatState state = { FALSE, BEAT_IDLE_BPM, b->beat_time, b->level, b->bass };
    /* A locked tempo is kept through quiet parts and breakdowns, and rests while paused. */
    if (b->locked > 0) { state.active = t - b->last_sound < BEAT_GAP; if (state.active) state.bpm = b->locked; }
    else { state.active = t - b->last_confident < BEAT_HOLD; if (state.active) state.bpm = b->bpm; }
    g_mutex_lock(&b->lock); b->state = state; g_mutex_unlock(&b->lock);
}

static char *default_sink(void) {
    char *argv[] = { "pactl", "get-default-sink", NULL }, *out = NULL;
    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &out, NULL, NULL, NULL)) return g_strdup("");
    return g_strstrip(out);
}

static void beat_pause(BeatTracker *b, gint64 usec) {
    g_mutex_lock(&b->lock); gint64 until = g_get_monotonic_time() + usec;
    while (b->running && g_get_monotonic_time() < until) g_cond_wait_until(&b->wake, &b->lock, until);
    g_mutex_unlock(&b->lock);
}

static gboolean beat_running(BeatTracker *b) { g_mutex_lock(&b->lock); gboolean running = b->running; g_mutex_unlock(&b->lock); return running; }

static gpointer capture_thread(gpointer data) {
    BeatTracker *b = data; guint8 block[BEAT_HOP * 2]; gsize filled = 0;
    while (beat_running(b)) {
        char *sink = default_sink(), *argv[] = { "parec", "--device=@DEFAULT_MONITOR@", "--raw", "--format=s16le", "--rate=8000", "--channels=1", "--latency-msec=20", "--client-name=Glacier Mint", "--stream-name=beat detection", NULL }; GPid pid = 0; int out = -1;
        if (!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, &pid, NULL, &out, NULL, NULL)) {
            fprintf(stderr, "glacier-mint: no audio capture (parec), animating at %g BPM\n", BEAT_IDLE_BPM); g_free(sink); return NULL;
        }
        g_mutex_lock(&b->lock); b->pid = pid; g_mutex_unlock(&b->lock);
        double recheck = g_get_monotonic_time() / 1e6 + 5.0; filled = 0;
        while (beat_running(b)) {
            struct pollfd poll_fd = { out, POLLIN, 0 }; if (poll(&poll_fd, 1, 200) <= 0) continue;
            ssize_t n = read(out, block + filled, sizeof(block) - filled); if (n <= 0) break;
            if ((filled += n) < sizeof(block)) continue;
            filled = 0; double now = g_get_monotonic_time() / 1e6; gint16 samples[BEAT_HOP]; memcpy(samples, block, sizeof(samples)); beat_process(b, samples, BEAT_HOP, now);
            if (now >= recheck) { recheck = now + 5.0; char *current = default_sink(); gboolean changed = strcmp(current, sink) != 0; g_free(current); if (changed) break; }
        }
        kill(pid, SIGTERM); close(out); waitpid(pid, NULL, 0); g_spawn_close_pid(pid); g_free(sink);
        g_mutex_lock(&b->lock); b->pid = 0; g_mutex_unlock(&b->lock);
        beat_pause(b, G_USEC_PER_SEC);
    }
    return NULL;
}

static char *variant_string(GVariant *dict, const char *key) {
    GVariant *value = g_variant_lookup_value(dict, key, NULL); char *text = NULL; if (!value) return g_strdup("");
    if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH)) text = g_variant_dup_string(value, NULL);
    else if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY)) { const char **list = g_variant_get_strv(value, NULL); text = g_strjoinv(", ", (char **)list); g_free(list); }
    g_variant_unref(value); return text ? text : g_strdup("");
}

/* Which track the media players are playing. The key keeps its value while paused and is NULL when no player reports anything. */
static gpointer player_thread(gpointer data) {
    BeatTracker *b = data; GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    while (bus && beat_running(b)) {
        beat_pause(b, G_USEC_PER_SEC); if (!beat_running(b)) break;
        GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", NULL, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
        if (!reply) continue;
        GVariantIter *names; const char *name; int players = 0; GPtrArray *playing = g_ptr_array_new_with_free_func(g_free); g_variant_get(reply, "(as)", &names);
        while (g_variant_iter_next(names, "&s", &name)) {
            if (!g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) continue;
            GVariant *props = g_dbus_connection_call_sync(bus, name, "/org/mpris/MediaPlayer2", "org.freedesktop.DBus.Properties", "GetAll", g_variant_new("(s)", "org.mpris.MediaPlayer2.Player"), G_VARIANT_TYPE("(a{sv})"), G_DBUS_CALL_FLAGS_NONE, 1000, NULL, NULL);
            if (!props) continue;
            GVariant *dict = g_variant_get_child_value(props, 0), *metadata = g_variant_lookup_value(dict, "Metadata", G_VARIANT_TYPE_VARDICT); char *status = variant_string(dict, "PlaybackStatus"); players++;
            if (!strcmp(status, "Playing")) {
                char *id = metadata ? variant_string(metadata, "mpris:trackid") : g_strdup(""), *title = metadata ? variant_string(metadata, "xesam:title") : g_strdup(""), *artist = metadata ? variant_string(metadata, "xesam:artist") : g_strdup("");
                g_ptr_array_add(playing, g_strjoin("\x1f", name, id, title, artist, NULL)); g_free(id); g_free(title); g_free(artist);
            }
            g_free(status); if (metadata) g_variant_unref(metadata); g_variant_unref(dict); g_variant_unref(props);
        }
        g_variant_iter_free(names); g_variant_unref(reply);
        if (playing->len) {
            g_ptr_array_sort(playing, (GCompareFunc)g_strcmp0); g_ptr_array_add(playing, NULL); char *key = g_strjoinv("\x1e", (char **)playing->pdata);
            g_mutex_lock(&b->lock); g_free(b->player_key); b->player_key = key; g_mutex_unlock(&b->lock);
        } else if (!players) { g_mutex_lock(&b->lock); g_clear_pointer(&b->player_key, g_free); g_mutex_unlock(&b->lock); }
        g_ptr_array_free(playing, TRUE);
    }
    if (bus) g_object_unref(bus);
    return NULL;
}

gboolean shader_uses_audio(const char *source) {
    static const char *names[] = { "iBeat", "iBPM", "iAudioLevel", "iAudioBass" };
    for (guint i = 0; source && i < G_N_ELEMENTS(names); i++) if (strstr(source, names[i])) return TRUE;
    return FALSE;
}

BeatTracker *beat_tracker_new(void) {
    BeatTracker *b = g_new0(BeatTracker, 1); g_mutex_init(&b->lock); g_cond_init(&b->wake); beat_init(b); b->running = TRUE;
    b->capture_thread = g_thread_new("beat-capture", capture_thread, b); b->player_thread = g_thread_new("beat-player", player_thread, b);
    return b;
}

void beat_tracker_state(BeatTracker *tracker, BeatState *state) { g_mutex_lock(&tracker->lock); *state = tracker->state; g_mutex_unlock(&tracker->lock); }

void beat_tracker_free(BeatTracker *b) {
    if (!b) return;
    g_mutex_lock(&b->lock); b->running = FALSE; if (b->pid) kill(b->pid, SIGTERM); g_cond_broadcast(&b->wake); g_mutex_unlock(&b->lock);
    if (b->capture_thread) g_thread_join(b->capture_thread);
    if (b->player_thread) g_thread_join(b->player_thread);
    g_free(b->onsets.data); g_free(b->kicks.data); g_free(b->longer.data); g_free(b->player_key); g_free(b->song_key); g_mutex_clear(&b->lock); g_cond_clear(&b->wake); g_free(b);
}

/* Smooth, never-reversing beat counter: tempo changes glide and the phase is pulled toward the detected beats gradually. */
void beat_clock_update(BeatClock *clock, double dt, double now, const BeatState *state) {
    clock->bpm += (state->bpm - clock->bpm) * MIN(1.0, dt * 3.0);
    double step = dt * clock->bpm / 60.0;
    if (state->active) {
        double want = wrap_one((now - state->beat_time) * state->bpm / 60.0), error = wrap_one(want - wrap_one(clock->beat) + 0.5) - 0.5;
        step = MAX(0.25 * step, step + error * MIN(1.0, dt * 4.0));
    }
    clock->beat = fmod(clock->beat + step, BEAT_WRAP);
}
