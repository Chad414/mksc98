/* Test harness: log, frame hashing, frame dump and the --bench timers. */
#include "port.h"

#include <stdarg.h>
#ifdef _WIN32
#include <io.h>
#endif

PortOptions port_options;

void port_log(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
}

/* ---- phase timers --------------------------------------------------------- */
static uint64_t phase_started[PHASE_COUNT];
static uint64_t phase_frame[PHASE_COUNT];   /* accumulated in the current frame */
static int race_tick_in_frame;              /* the simulation advanced this frame */

void harness_phase_begin(int phase)
{
    if (port_options.bench) phase_started[phase] = backend_counter();
}
void harness_phase_end(int phase)
{
    if (port_options.bench) phase_frame[phase] += backend_counter() - phase_started[phase];
    if (phase == PHASE_LOGIC) race_tick_in_frame = 1;
}

/* Link-time wrappers (see CMakeLists.txt, -Wl,--wrap): upstream is timed
 * without being edited. Each wrapper only brackets the real function. */
struct PortableGpRaceSession; struct PortableRaceScene; struct PortableRacePresentationState;
struct PortableSoftwareFrame; struct RouteAudioHost; struct PortableGpRaceSnapshot;
int __real_portable_gp_race_session_tick(void *session, uint16_t buttons, void *disposition);
int __wrap_portable_gp_race_session_tick(void *session, uint16_t buttons, void *disposition)
{
    harness_phase_begin(PHASE_LOGIC);
    const int result = __real_portable_gp_race_session_tick(session, buttons, disposition);
    harness_phase_end(PHASE_LOGIC);
    return result;
}
int __real_route_audio_render_gp(void *audio, uint16_t buttons, const void *snapshot);
int __wrap_route_audio_render_gp(void *audio, uint16_t buttons, const void *snapshot)
{
    harness_phase_begin(PHASE_AUDIO);
    const int result = __real_route_audio_render_gp(audio, buttons, snapshot);
    harness_phase_end(PHASE_AUDIO);
    return result;
}

/* ---- per-frame bookkeeping -------------------------------------------------- */
static long counted_frames;      /* frames that count towards --frames */
static uint32_t final_hash, running_hash = UINT32_C(2166136261);
static uint64_t frame_started;
static struct { double sum, worst; } bench_total, bench_phase[PHASE_COUNT], bench_other;
static long bench_frames, bench_over_budget;
static struct { double sum, worst; } bench_menu; /* frames with no race tick: menus, transitions, results */
static long bench_menu_frames;

static uint32_t frame_hash(const uint32_t *pixels, int width, int height, int pitch_pixels)
{
    uint32_t hash = UINT32_C(2166136261);
    for (int y = 0; y < height; ++y) {
        const uint32_t *row = pixels + (size_t)y * (size_t)pitch_pixels;
        for (int x = 0; x < width; ++x) {
            hash ^= row[x] & UINT32_C(0x00ffffff); /* alpha is presentation-only */
            hash *= UINT32_C(16777619);
        }
    }
    return hash;
}

int harness_write_bmp(const char *path, const uint32_t *pixels, int width, int height, int pitch_pixels)
{
    FILE *file = fopen(path, "wb");
    const uint32_t row_bytes = ((uint32_t)width * 3u + 3u) & ~3u, size = 54u + row_bytes * (uint32_t)height;
    uint8_t header[54] = {'B', 'M'};
    uint8_t row[2048 * 3 + 4];
    if (file == NULL || width > 2048) { if (file) fclose(file); return 0; }
    header[2] = (uint8_t)size; header[3] = (uint8_t)(size >> 8); header[4] = (uint8_t)(size >> 16); header[5] = (uint8_t)(size >> 24);
    header[10] = 54; header[14] = 40;
    header[18] = (uint8_t)width; header[19] = (uint8_t)(width >> 8);
    header[22] = (uint8_t)height; header[23] = (uint8_t)(height >> 8);
    header[26] = 1; header[28] = 24;
    fwrite(header, 1u, sizeof header, file);
    memset(row, 0, sizeof row);
    for (int y = height - 1; y >= 0; --y) {
        const uint32_t *source = pixels + (size_t)y * (size_t)pitch_pixels;
        for (int x = 0; x < width; ++x) {
            row[x * 3] = (uint8_t)source[x]; row[x * 3 + 1] = (uint8_t)(source[x] >> 8); row[x * 3 + 2] = (uint8_t)(source[x] >> 16);
        }
        fwrite(row, 1u, row_bytes, file);
    }
    return fclose(file) == 0;
}

static void bench_add(double *sum, double *worst, double value)
{
    *sum += value;
    if (value > *worst) *worst = value;
}

/* Every 30 s of real play, one line in the log that is forced onto the disk
 * (not left in Windows' write cache), so that after a hard reset or power
 * loss the log still says how long the game ran and what it was doing. */
static void heartbeat(int in_race)
{
    static uint64_t started, next;
    static unsigned frames, race_frames;
    const uint64_t now = backend_counter(), frequency = backend_frequency();
    ++frames;
    race_frames += in_race ? 1u : 0u;
    if (started == 0u) { started = now; next = now + frequency * 30u; return; }
    if (now < next) return;
    next = now + frequency * 30u;
    port_log("alive: %u s, %u pictures (%u in races), now %s", (unsigned)((now - started) / frequency), frames,
             race_frames, in_race ? "racing" : "in menus");
#ifdef _WIN32
    (void)_commit(_fileno(stdout));
#endif
}

void harness_frame(const uint32_t *pixels, int width, int height, int pitch_pixels)
{
    const int in_race = race_tick_in_frame;
    race_tick_in_frame = 0;
    if (!port_options.headless || port_options.watch) heartbeat(in_race);
    if (port_options.bench) {
        const uint64_t now = backend_counter();
        const double to_ms = 1000.0 / (double)backend_frequency();
        if (in_race && frame_started != 0u) {
            /* A frame runs from the previous present to this one; the blit of
             * the previous frame is therefore charged to this frame. */
            const double total = (double)(now - frame_started) * to_ms;
            double attributed = 0.0;
            bench_add(&bench_total.sum, &bench_total.worst, total);
            for (int phase = 0; phase < PHASE_COUNT; ++phase) {
                const double value = (double)phase_frame[phase] * to_ms;
                bench_add(&bench_phase[phase].sum, &bench_phase[phase].worst, value);
                attributed += value;
            }
            bench_add(&bench_other.sum, &bench_other.worst, total > attributed ? total - attributed : 0.0);
            ++bench_frames;
            if (total > 16.742707) ++bench_over_budget;
        }
        if (!in_race && frame_started != 0u) {
            bench_add(&bench_menu.sum, &bench_menu.worst, (double)(now - frame_started) * to_ms);
            ++bench_menu_frames;
        }
        memset(phase_frame, 0, sizeof phase_frame);
        frame_started = now;
        /* Bench frames are race frames only; menus on the way there are free. */
        if (!in_race) return;
    }
    ++counted_frames;
    if (port_options.hash || port_options.dump_frame != NULL || port_options.screenshot != NULL) {
        const int last = port_options.frames > 0 && counted_frames == port_options.frames;
        if (port_options.hash && (last || counted_frames % 60 == 0)) {
            final_hash = frame_hash(pixels, width, height, pitch_pixels);
            running_hash = (running_hash ^ final_hash) * UINT32_C(16777619);
            port_log("hash: frame %ld %dx%d frame=%08lx running=%08lx", counted_frames, width, height,
                     (unsigned long)final_hash, (unsigned long)running_hash);
        }
        if (last)
            port_log("frame: pixel at picture centre is %06lx",
                     (unsigned long)(pixels[(size_t)(height / 2) * (size_t)pitch_pixels + (size_t)(width / 2)] & 0xffffffu));
        if (last && port_options.dump_frame != NULL)
            port_log("dump-frame: %s %s", port_options.dump_frame,
                     harness_write_bmp(port_options.dump_frame, pixels, width, height, pitch_pixels) ? "written" : "FAILED");
    }
}

int harness_done(void)
{
    return port_options.frames > 0 && counted_frames >= port_options.frames;
}

void harness_report(void)
{
    static const char *const names[PHASE_COUNT] = {"logic", "render", "blit", "audio"};
    if (port_options.hash)
        port_log("hash: FINAL frames=%ld frame=%08lx running=%08lx", counted_frames,
                 (unsigned long)final_hash, (unsigned long)running_hash);
    if (port_options.bench && bench_frames > 0) {
        port_log("bench: %ld race frames, video=%s, budget 16.74 ms/frame", bench_frames, backend_video_name());
        port_log("bench: %-7s avg %7.3f ms  worst %7.3f ms  (%.1f fps average)", "total",
                 bench_total.sum / (double)bench_frames, bench_total.worst,
                 1000.0 * (double)bench_frames / bench_total.sum);
        for (int phase = 0; phase < PHASE_COUNT; ++phase)
            port_log("bench: %-7s avg %7.3f ms  worst %7.3f ms", names[phase],
                     bench_phase[phase].sum / (double)bench_frames, bench_phase[phase].worst);
        port_log("bench: %-7s avg %7.3f ms  worst %7.3f ms  (HUD, sprites outside the scene, host)", "other",
                 bench_other.sum / (double)bench_frames, bench_other.worst);
        port_log("bench: %ld of %ld frames over budget", bench_over_budget, bench_frames);
        if (bench_menu_frames > 0)
            port_log("bench: menus   avg %7.3f ms  worst %7.3f ms  (%ld non-race frames: menus, transitions; not in the totals)",
                     bench_menu.sum / (double)bench_menu_frames, bench_menu.worst, bench_menu_frames);
    } else if (port_options.bench)
        port_log("bench: no race frames were measured");
}
