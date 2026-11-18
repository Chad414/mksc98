/* Sound through DirectSound: a single secondary buffer, started once with
 * DSBPLAY_LOOPING and never stopped until the game quits. The game's blocks
 * are written just ahead of the play cursor; everything the card has already
 * played is zeroed again, so if the game falls behind (or the cursor laps
 * during a long stall) the card plays silence rather than old sound, and the
 * driver never has to stop, restart or be handed new buffers.
 * dsound.dll is loaded at run time so the program starts without it. */
#include "port.h"
#include "backend_dsound.h"

#include <mmsystem.h>
#include <dsound.h>

enum { RING_FRAMES = 16384 };               /* 0.74 s at 22050 Hz */
#define RING_BYTES ((DWORD)RING_FRAMES * 4u)

typedef HRESULT (WINAPI *DirectSoundCreateFn)(LPCGUID, LPDIRECTSOUND *, LPUNKNOWN);

static HMODULE library;
static LPDIRECTSOUND sound;
static LPDIRECTSOUNDBUFFER primary, ring;
static DWORD write_pos;                     /* where the next frame goes */
static DWORD last_play;                     /* play cursor at the last poll */
static long ahead;                          /* bytes written and not yet played */
static long committed;                      /* play-to-write-cursor gap: the card has taken that part already */
static DWORD last_poll_ms;
static DWORD bytes_per_second;
static unsigned underruns, stalls, dropped, written;  /* for the log at close */

static int load(void)
{
    if (library == NULL) library = LoadLibraryA("dsound.dll");
    return library != NULL;
}

int dsound_available(void) { return load(); }

static int lock_region(DWORD start, DWORD bytes, void **p1, DWORD *l1, void **p2, DWORD *l2)
{
    HRESULT result = IDirectSoundBuffer_Lock(ring, start, bytes, p1, l1, p2, l2, 0);
    if (result == DSERR_BUFFERLOST) {
        /* Another program took the card; get the buffer back and keep looping. */
        if (IDirectSoundBuffer_Restore(ring) != DS_OK) return 0;
        IDirectSoundBuffer_Play(ring, 0, 0, DSBPLAY_LOOPING);
        result = IDirectSoundBuffer_Lock(ring, start, bytes, p1, l1, p2, l2, 0);
    }
    return result == DS_OK;
}

static void zero_region(DWORD start, DWORD bytes)
{
    void *p1, *p2;
    DWORD l1, l2;
    if (bytes == 0u || !lock_region(start % RING_BYTES, bytes, &p1, &l1, &p2, &l2)) return;
    memset(p1, 0, l1);
    if (p2 != NULL) memset(p2, 0, l2);
    IDirectSoundBuffer_Unlock(ring, p1, l1, p2, l2);
}

/* Start writing again at the card's write cursor (the first safe place). */
static void resync(void)
{
    DWORD play = 0u, write = 0u;
    IDirectSoundBuffer_GetCurrentPosition(ring, &play, &write);
    write_pos = write % RING_BYTES;
    last_play = play % RING_BYTES;
    ahead = (long)((write_pos + RING_BYTES - last_play) % RING_BYTES);
    committed = ahead;
    last_poll_ms = timeGetTime();
}

/* Follow the play cursor and clear what it has played. */
static void poll(void)
{
    DWORD play = 0u, write = 0u, played;
    const DWORD now = timeGetTime();
    if (IDirectSoundBuffer_GetCurrentPosition(ring, &play, &write) != DS_OK) return;
    play %= RING_BYTES;
    if ((now - last_poll_ms) * (bytes_per_second / 1000u) > RING_BYTES / 2u) {
        /* Not polled for a long time (loading): the cursor may have gone round.
         * The whole ring may hold played sound; clear it and start afresh. */
        zero_region(0u, RING_BYTES);
        resync();
        ++stalls;
        return;
    }
    last_poll_ms = now;
    played = (play + RING_BYTES - last_play) % RING_BYTES;
    zero_region(last_play, played);
    last_play = play;
    ahead -= (long)played;
    committed = (long)(((write % RING_BYTES) + RING_BYTES - play) % RING_BYTES);
    /* Fallen behind the write cursor: the card is already past what we would
     * write next (and plays zeroes); carry on from its cursor. */
    if (ahead < committed) { resync(); ++underruns; }
}

int dsound_open(HWND window, int rate)
{
    DirectSoundCreateFn create;
    DSBUFFERDESC description;
    WAVEFORMATEX format;
    HRESULT result;
    if (!load()) return 0;
    create = (DirectSoundCreateFn)(void *)GetProcAddress(library, "DirectSoundCreate");
    if (create == NULL || create(NULL, &sound, NULL) != DS_OK) { sound = NULL; port_log("audio: DirectSound not available"); return 0; }
    if ((result = IDirectSound_SetCooperativeLevel(sound, window, DSSCL_PRIORITY)) != DS_OK) {
        port_log("audio: DirectSound cooperative level failed (%08lx)", (unsigned long)result);
        dsound_close();
        return 0;
    }
    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = (DWORD)rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = (DWORD)rate * 4u;
    bytes_per_second = format.nAvgBytesPerSec;
    /* The card's own output format: otherwise DirectSound mixes at its
     * default 22 kHz 8-bit. Failing this only costs quality. */
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    description.dwFlags = DSBCAPS_PRIMARYBUFFER;
    if (IDirectSound_CreateSoundBuffer(sound, &description, &primary, NULL) == DS_OK) {
        if (IDirectSoundBuffer_SetFormat(primary, &format) != DS_OK) port_log("audio: DirectSound kept its own mixing format");
    } else primary = NULL;
    memset(&description, 0, sizeof description);
    description.dwSize = sizeof description;
    description.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS;
    description.dwBufferBytes = RING_BYTES;
    description.lpwfxFormat = &format;
    result = IDirectSound_CreateSoundBuffer(sound, &description, &ring, NULL);
    if (result != DS_OK) {
        port_log("audio: DirectSound buffer failed (%08lx)", (unsigned long)result);
        ring = NULL;
        dsound_close();
        return 0;
    }
    zero_region(0u, RING_BYTES);
    if ((result = IDirectSoundBuffer_Play(ring, 0, 0, DSBPLAY_LOOPING)) != DS_OK) {
        port_log("audio: DirectSound play failed (%08lx)", (unsigned long)result);
        dsound_close();
        return 0;
    }
    resync();
    port_log("audio: DirectSound write cursor %ld ms ahead of the play cursor", ahead * 1000L / (long)bytes_per_second);
    return 1;
}

void dsound_close(void)
{
    if (ring != NULL)
        port_log("audio: DirectSound wrote %u blocks; %u ran dry, %u long stalls, %u dropped as too far ahead",
                 written, underruns, stalls, dropped);
    if (ring != NULL) { IDirectSoundBuffer_Stop(ring); IDirectSoundBuffer_Release(ring); ring = NULL; }
    if (primary != NULL) { IDirectSoundBuffer_Release(primary); primary = NULL; }
    if (sound != NULL) { IDirectSound_Release(sound); sound = NULL; }
}

unsigned dsound_queued_frames(void)
{
    if (ring == NULL) return 0u;
    poll();
    /* Only what can still be replaced counts towards the game's latency limit. */
    return (unsigned)((ahead - committed) / 4);
}

int dsound_write(const int16_t *frames, unsigned count)
{
    void *p1, *p2;
    DWORD l1, l2, bytes;
    if (ring == NULL) return 0;
    poll();
    /* Never write over sound that has not been played yet. */
    if ((DWORD)ahead + count * 4u > RING_BYTES - RING_BYTES / 4u) { ++dropped; return 1; } /* full: drop */
    bytes = count * 4u;
    ++written;
    if (bytes == 0u || !lock_region(write_pos, bytes, &p1, &l1, &p2, &l2)) return 1;
    memcpy(p1, frames, l1);
    if (p2 != NULL) memcpy(p2, (const uint8_t *)frames + l1, l2);
    IDirectSoundBuffer_Unlock(ring, p1, l1, p2, l2);
    write_pos = (write_pos + bytes) % RING_BYTES;
    ahead += (long)bytes;
    return 1;
}

void dsound_silence(void)
{
    DWORD play = 0u, write = 0u;
    if (ring == NULL) return;
    poll();
    /* Clear what was written beyond the card's write cursor and continue from there. */
    IDirectSoundBuffer_GetCurrentPosition(ring, &play, &write);
    write %= RING_BYTES;
    zero_region(write, (write_pos + RING_BYTES - write) % RING_BYTES);
    resync();
}
