/* Sound through one looping DirectSound buffer (see backend_dsound.c). */
#ifndef MKSC98_BACKEND_DSOUND_H
#define MKSC98_BACKEND_DSOUND_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

int dsound_available(void);                 /* dsound.dll can be loaded */
int dsound_open(HWND window, int rate);     /* stereo 16-bit at rate; starts playing silence */
void dsound_close(void);
unsigned dsound_queued_frames(void);        /* written and not yet played */
int dsound_write(const int16_t *frames, unsigned count);
void dsound_silence(void);                  /* drop what is queued (plays as silence) */

#endif
