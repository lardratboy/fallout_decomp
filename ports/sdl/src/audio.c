/*
 * audio.c -- the speaker on an SDL3 audio stream.
 *
 * The game switches the speaker from its own thread, at the moments it calls
 * sound() and nosound().  Those moments are stamped with the realtime clock;
 * the speaker is rendered up to "now" before every change and whenever the
 * backend services events (at most once a millisecond, and at least every
 * ~15 ms while the game sleeps), and the samples are queued on the stream.
 * The queue is the delay between the game and what is heard: the device's
 * buffer plus GAP_MS to cover the time between renders.
 *
 * The sound card's clock and the system clock drift apart slowly, and a
 * stalled game thread (a window being dragged) lets the queue run dry.  Both
 * are corrected only once the speaker has been off for longer than the queue
 * is long: everything queued is then silence and can be dropped or padded
 * unheard.  After a stall of more than MAX_BACKLOG_MS the missed part is
 * skipped rather than played late.
 */
#include <stdio.h>
#include <SDL3/SDL.h>
#include "audio.h"
#include "rtclock.h"
#include "speaker.h"

#define DEFAULT_RATE    48000
#define DEVICE_FRAMES   "512"   /* ~11 ms at 48 kHz (SDL's default is 1024) */
#define GAP_MS          20
#define TAIL_MS         30      /* filter ring-out after the speaker goes off */
#define MAX_BACKLOG_MS  250
#define CHUNK           1024

static SDL_AudioStream *stream;
static struct speaker spk;
static unsigned rate;
static int target, low, high;   /* queue levels, in samples */

static uint64_t clk_of_us(uint64_t us)
{
    return us * PIT_HZ / 1000000;
}

static int samples_of_ms(int ms)
{
    return (int)((uint64_t)rate * ms / 1000);
}

static void put_silence(int n)
{
    static const float zero[CHUNK];

    while (n > 0) {
        int k = n < CHUNK ? n : CHUNK;
        SDL_PutAudioStreamData(stream, zero, k * (int)sizeof(float));
        n -= k;
    }
}

int audio_init(int volume)
{
    SDL_AudioSpec spec;
    int frames = 0;

    if (volume <= 0)
        return 0;
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, DEVICE_FRAMES);
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "audio: %s (playing without sound)\n", SDL_GetError());
        return -1;
    }
    /* render at the device's own rate, so that SDL need not resample */
    if (!SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL) ||
        spec.freq <= 0)
        spec.freq = DEFAULT_RATE;
    rate = (unsigned)spec.freq;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 1;
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (!stream) {
        fprintf(stderr, "audio: %s (playing without sound)\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &spec, &frames) &&
        spec.freq > 0)
        frames = (int)((uint64_t)frames * rate / (unsigned)spec.freq);
    target = frames + samples_of_ms(GAP_MS);
    low = samples_of_ms(GAP_MS / 2);
    high = target + samples_of_ms(40);
    speaker_init(&spk, rate, (float)volume / 100.0f, 1);
    put_silence(target);
    SDL_ResumeAudioStreamDevice(stream);
    return 0;
}

void audio_update(uint64_t now_us)
{
    float buf[CHUNK];
    uint64_t clk, off;
    size_t n;
    int queued;

    if (!stream)
        return;
    clk = clk_of_us(now_us);
    if (clk - spk.now / rate > clk_of_us(MAX_BACKLOG_MS * 1000)) {
        uint64_t skip_to = clk - clk_of_us(GAP_MS * 1000);
        while (speaker_render(&spk, skip_to, buf, CHUNK) > 0)
            ;
    }
    while ((n = speaker_render(&spk, clk, buf, CHUNK)) > 0)
        SDL_PutAudioStreamData(stream, buf, (int)(n * sizeof(float)));

    queued = SDL_GetAudioStreamQueued(stream) / (int)sizeof(float);
    if (queued >= low && queued <= high)
        return;
    off = speaker_off_since(&spk);
    if (off == UINT64_MAX ||
        clk - off < (uint64_t)queued * PIT_HZ / rate + clk_of_us(TAIL_MS * 1000))
        return;
    if (queued > high) {
        SDL_ClearAudioStream(stream);
        queued = 0;
    }
    put_silence(target - queued);
}

void audio_speaker(uint64_t now_us, unsigned divisor)
{
    if (!stream)
        return;
    audio_update(now_us);
    speaker_write(&spk, divisor);
}

void audio_shutdown(void)
{
    if (stream)
        SDL_DestroyAudioStream(stream);
    stream = NULL;
}
