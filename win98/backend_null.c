/* Headless backend for the reference build on the development host: no
 * window and no sound device. Frames still pass through the harness, so
 * --hash and --dump-frame produce the reference the Win98 build is compared
 * against. */
#include "port.h"

#include <time.h>
#include <unistd.h>

static char base_directory[1024];
static int output_width = 240;

int backend_init(void)
{
    const char *directory = getenv("MKSC98_DIR");
    char buffer[1000];
    if (directory == NULL && getcwd(buffer, sizeof buffer) != NULL) directory = buffer;
    if (directory == NULL) directory = ".";
    snprintf(base_directory, sizeof base_directory, "%s/", directory);
    return 1;
}
void backend_shutdown(void) {}
uint64_t backend_counter(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}
uint64_t backend_frequency(void) { return UINT64_C(1000000000); }
void backend_sleep(unsigned milliseconds) { usleep(milliseconds * 1000u); }
const char *backend_base_directory(void) { return base_directory; }
int backend_window_create(const char *title, int width, int height, int hidden, int fullscreen)
{
    (void)title; (void)height; (void)hidden; (void)fullscreen;
    output_width = width;
    return 1;
}
void backend_window_destroy(void) {}
void backend_window_title(const char *title) { (void)title; }
void backend_window_size(int width, int height) { (void)width; (void)height; }
void backend_window_fullscreen(int fullscreen) { (void)fullscreen; }
/* Tall enough for the extra sky rows, so the reference build composes them too. */
void backend_output_size(int *width, int *height)
{
    *width = output_width;
    *height = 160 + tall_rows_now();
}
/* The reference build pretends to have the target's 1280x1024 display. */
void backend_screen_size(int *width, int *height) { *width = 1280; *height = 1024; }
int backend_is_fullscreen(void) { return 1; }
void backend_pump(void) {}
int backend_present(const uint32_t *pixels, int width, int height, int pitch_pixels,
                    int x, int y, int output_width, int output_height)
{
    (void)pixels; (void)width; (void)height; (void)pitch_pixels;
    (void)x; (void)y; (void)output_width; (void)output_height;
    return 1;
}
const char *backend_video_name(void) { return "null"; }
int backend_screenshot(const char *path) { (void)path; return 0; }
int backend_audio_open(int rate) { (void)rate; return 1; }
void backend_audio_close(void) {}
unsigned backend_audio_queued(void) { return 0u; }
int backend_audio_write(const int16_t *samples, unsigned bytes) { (void)samples; (void)bytes; return 1; }
void backend_audio_pause(int paused) { (void)paused; }
void backend_audio_clear(void) {}
void backend_audio_gain(float gain) { (void)gain; }
