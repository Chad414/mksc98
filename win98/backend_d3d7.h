/* Direct3D 7 point-sampled presenter (backend_d3d7.c), used by backend_win32.c. */
#ifndef MKSC_WIN98_BACKEND_D3D7_H
#define MKSC_WIN98_BACKEND_D3D7_H
#include <stdint.h>
#include <windows.h>

/* own_screen: exclusive fullscreen in the desktop's mode with page flipping;
 * otherwise windowed (the picture is copied to the screen). */
int d3d7_open(HWND window, int own_screen);
int d3d7_owns_screen(void);
void d3d7_pump(void);
void d3d7_report(void);
void d3d7_close(void);
/* Draws the width x height XRGB picture into the client rectangle
 * (x, y, output_width, output_height) with nearest-pixel scaling. 0 = failed. */
int d3d7_present(HWND window, const uint32_t *pixels, int width, int height, int pitch_pixels,
                 int x, int y, int output_width, int output_height);
const char *d3d7_name(void);
#endif
