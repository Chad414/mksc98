/* Internal interfaces of the Windows 98 port (win98/*.c only). */
#ifndef MKSC_WIN98_PORT_H
#define MKSC_WIN98_PORT_H

#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>

/* ---- options and harness (harness.c) ---------------------------------- */
typedef struct {
    /* presentation */
    int scale;          /* integer window scale 1..8; 0 = use the saved setting */
    int fullscreen;     /* 1 desktop-resolution fullscreen, -1 force a window, 0 = saved setting */
    int fullscreen640;  /* 640x480x16 exclusive mode */
    int aspect_width;   /* picture width 240..1024 for this launch; -1 = Fill; 0 = the saved Options setting */
    int force_gdi;      /* skip Direct3D and DirectDraw */
    int no_flip;        /* fullscreen Direct3D copies to the screen instead of page-flipping */
    int sync_flip;      /* wait in the flip call for the card to finish drawing */
    int force_ddraw;    /* skip Direct3D: DirectDraw stretch blit (may be smoothed by the driver) */
    int soft_scale;     /* scale on the CPU, then blit 1:1 (for measurement) */
    int no_audio;
    int waveout;        /* sound through waveOut instead of DirectSound */
    int show_fps;
    /* test harness */
    long frames;        /* stop after N presented frames; 0 = run until quit */
    int hash;           /* log a hash of every 60th frame and of the final one */
    const char *dump_frame; /* write the final frame as a .bmp */
    const char *screenshot; /* after the final frame, save what the desktop shows around the window */
    int bench;          /* no vsync or sleep; report per-phase timings */
    int route;          /* scripted Grand Prix route (implied by hash/bench) */
    int route_cup, route_character, route_class;
    int route_mode;     /* 0 Grand Prix, 1 Quick Run, 2 Battle, 3 menu shell (Time Trial, results, GP) */
    int watch;          /* show a scripted route in the window, paced to real time, with sound */
    int tall_rows;      /* extra rows of sky above the race picture regardless of the aspect setting (tall.c) */
    int headless;       /* derived: scripted run on upstream's virtual clock */
} PortOptions;
extern PortOptions port_options;

void port_log(const char *format, ...);
/* Called by the shim with the 32-bit frame about to be shown. */
void harness_frame(const uint32_t *pixels, int width, int height, int pitch_pixels);
int harness_done(void);
int harness_write_bmp(const char *path, const uint32_t *pixels, int width, int height, int pitch_pixels);
void harness_report(void);

/* Bench phase timers. Wrappers installed with the linker's --wrap attribute
 * time to a phase; time not attributed to any phase is reported as "other". */
enum { PHASE_LOGIC, PHASE_RENDER, PHASE_BLIT, PHASE_AUDIO, PHASE_COUNT };
void harness_phase_begin(int phase);
void harness_phase_end(int phase);

/* ---- backend (backend_win32.c or backend_null.c) ---------------------- */
int backend_init(void);
void backend_shutdown(void);
uint64_t backend_counter(void);
uint64_t backend_frequency(void);
void backend_sleep(unsigned milliseconds);
/* Directory holding the .exe, with a trailing separator. Static storage. */
const char *backend_base_directory(void);
int backend_window_create(const char *title, int width, int height, int hidden, int fullscreen);
void backend_window_destroy(void);
void backend_window_title(const char *title);
void backend_window_size(int width, int height);
/* Enter or leave fullscreen at the desktop's own resolution. */
void backend_window_fullscreen(int fullscreen);
void backend_output_size(int *width, int *height);
/* Size of the whole display in its current mode. */
void backend_screen_size(int *width, int *height);
/* Does the game cover the whole display right now? */
int backend_is_fullscreen(void);
/* Translates pending OS messages into shim_post_key / shim_post_simple. */
void backend_pump(void);
/* Shows a width x height XRGB picture scaled into the output rectangle. */
int backend_present(const uint32_t *pixels, int width, int height, int pitch_pixels,
                    int x, int y, int output_width, int output_height);
const char *backend_video_name(void);
/* Saves the part of the real screen that holds the window as a 24-bit BMP. */
int backend_screenshot(const char *path);
int backend_audio_open(int rate);
void backend_audio_close(void);
/* Stereo 16-bit source bytes accepted but not yet played. */
unsigned backend_audio_queued(void);
int backend_audio_write(const int16_t *samples, unsigned bytes);
void backend_audio_pause(int paused);
void backend_audio_clear(void);
void backend_audio_gain(float gain);

/* ---- shim entry points for the backend (shim.c) ------------------------ */
void shim_post_key(SDL_Scancode scancode, int down, int repeat);
void shim_post_simple(SDL_EventType type);
int shim_frame_count(void);

/* ---- taller race pictures (tall.c) ----------------------------------- */
const uint32_t *tall_take_band(int width, int *rows);
/* Extra sky rows in effect: --tall N, or what the Fill setting gives on this screen. */
int tall_rows_now(void);

#endif
