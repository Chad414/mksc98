/* Replaces upstream's platform/sdl_audio_queue.c: the same queue contract
 * (fixed 176-frame stereo S16 blocks at 10512 Hz, bounded latency, drop when
 * full) on top of the backend's sound device instead of an SDL audio stream. */
#include "sdl_audio_queue.h"
#include "port.h"

struct SDL_AudioStream { int open; };
static struct SDL_AudioStream the_stream;

PortableSdlAudioStatus portable_sdl_audio_queue_open(PortableSdlAudioQueue *queue)
{
    if (queue == NULL) return PORTABLE_SDL_AUDIO_INVALID_ARGUMENT;
    memset(queue, 0, sizeof *queue);
    /* A missing or busy sound card must not stop the game: run silent. */
    /* Scripted runs are not real time; the mix is still computed and timed. */
    the_stream.open = !port_options.no_audio && (!port_options.headless || port_options.watch) &&
                      backend_audio_open(PORTABLE_SDL_AUDIO_SOURCE_RATE);
    if (!the_stream.open && !port_options.no_audio && !port_options.headless)
        port_log("audio: no usable sound device; continuing without sound");
    queue->stream = &the_stream;
    queue->device = 1u;
    queue->obtained.format = SDL_AUDIO_S16;
    queue->obtained.channels = 2;
    queue->obtained.freq = PORTABLE_SDL_AUDIO_SOURCE_RATE;
    queue->max_queued_bytes = PORTABLE_SDL_AUDIO_SOURCE_RATE * 2u * sizeof(int16_t) *
                              PORTABLE_SDL_AUDIO_MAX_LATENCY_MS / 1000u;
    queue->source_block_bytes = PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u * sizeof(int16_t);
    return PORTABLE_SDL_AUDIO_OK;
}

PortableSdlAudioStatus portable_sdl_audio_queue_submit(PortableSdlAudioQueue *queue,
    const int16_t samples[PORTABLE_AUDIO_PCM_FRAME_SAMPLES * 2u])
{
    if (queue == NULL || samples == NULL || queue->stream == NULL) return PORTABLE_SDL_AUDIO_INVALID_ARGUMENT;
    if (queue->paused) return PORTABLE_SDL_AUDIO_PAUSED;
    if (!the_stream.open) { queue->submitted_blocks++; return PORTABLE_SDL_AUDIO_OK; }
    if (backend_audio_queued() > queue->max_queued_bytes - queue->source_block_bytes) {
        queue->dropped_blocks++;
        return PORTABLE_SDL_AUDIO_DROPPED;
    }
    if (!backend_audio_write(samples, queue->source_block_bytes)) return PORTABLE_SDL_AUDIO_QUEUE_FAILED;
    queue->submitted_blocks++;
    return PORTABLE_SDL_AUDIO_OK;
}

void portable_sdl_audio_queue_pause(PortableSdlAudioQueue *queue, int paused)
{
    if (queue == NULL || queue->stream == NULL) return;
    queue->paused = paused ? 1u : 0u;
    if (!the_stream.open) return;
    backend_audio_pause(queue->paused);
    if (queue->paused) backend_audio_clear();
}

void portable_sdl_audio_queue_close(PortableSdlAudioQueue *queue)
{
    if (queue == NULL) return;
    if (the_stream.open)
        port_log("audio: game submitted %u blocks, dropped %u (queue at its %u ms limit)", queue->submitted_blocks,
                 queue->dropped_blocks, (unsigned)PORTABLE_SDL_AUDIO_MAX_LATENCY_MS);
    if (the_stream.open) backend_audio_close();
    the_stream.open = 0;
    memset(queue, 0, sizeof *queue);
}

const char *portable_sdl_audio_status_name(PortableSdlAudioStatus status)
{
    static const char *const names[] = {"ok", "invalid argument", "init failed",
        "open failed", "queue failed", "paused", "dropped"};
    return (unsigned)status < sizeof names / sizeof names[0] ? names[status] : "unknown status";
}

bool SDL_SetAudioStreamGain(SDL_AudioStream *stream, float gain)
{
    if (stream != NULL && stream->open) backend_audio_gain(gain);
    return true;
}
bool SDL_ClearAudioStream(SDL_AudioStream *stream)
{
    if (stream != NULL && stream->open) backend_audio_clear();
    return true;
}
