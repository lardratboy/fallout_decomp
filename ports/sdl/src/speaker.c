/*
 * speaker.c -- PIT channel 2 square wave, area-sampled (see speaker.h).
 */
#include <math.h>
#include <string.h>
#include "rtclock.h"
#include "speaker.h"

/* a small cone speaker: bright, with little below the lowest notes */
#define LP_HZ   8000.0
#define HP_HZ   40.0
#define TWO_PI  6.283185307179586

void speaker_init(struct speaker *s, unsigned rate, float gain, int filtered)
{
    memset(s, 0, sizeof *s);
    s->rate = rate;
    s->gain = gain;
    s->filtered = filtered;
    s->lp_a = (float)(1.0 - exp(-TWO_PI * LP_HZ / rate));
    s->hp_a = (float)exp(-TWO_PI * HP_HZ / rate);
}

uint64_t speaker_off_since(const struct speaker *s)
{
    return s->on ? UINT64_MAX : s->off_since;
}

/* time the wave is high in positions [0, x) */
static uint64_t high_until(const struct speaker *s, uint64_t x)
{
    uint64_t r = x % s->period;

    return x / s->period * s->high + (r < s->high ? r : s->high);
}

static uint64_t position(const struct speaker *s, uint64_t t)
{
    return t - s->origin + s->phase0;
}

/* count `div` from time t, starting with the high half or the low one */
static void start_wave(struct speaker *s, unsigned div, uint64_t t, int low)
{
    s->period = (uint64_t)div * s->rate;
    s->high = (uint64_t)((div + 1) / 2) * s->rate;
    s->origin = t;
    s->phase0 = low ? s->high : 0;
}

/* integrate the output up to `end`, loading a pending count on the way */
static void advance(struct speaker *s, uint64_t end)
{
    while (s->now < end) {
        uint64_t stop = s->next_at && s->next_at < end ? s->next_at : end;
        if (s->on)
            s->acc += high_until(s, position(s, stop)) - high_until(s, position(s, s->now));
        s->now = stop;
        if (s->now == s->next_at) {
            /* the output toggles at the end of a half-cycle, as it would have */
            start_wave(s, s->next_div, s->now, position(s, s->now) % s->period != 0);
            s->next_at = 0;
        }
    }
}

void speaker_write(struct speaker *s, unsigned divisor)
{
    uint64_t t = s->now, r;

    if (divisor == 0) {
        if (s->on)
            s->off_since = t / s->rate;
        s->on = 0;
        s->next_at = 0;
    } else if (!s->on) {
        s->on = 1;
        start_wave(s, divisor, t, 0);
        s->next_at = 0;
    } else {
        r = position(s, t) % s->period;
        s->next_at = t + (r < s->high ? s->high - r : s->period - r);
        s->next_div = divisor;
    }
}

static float flush(float v)
{
    return fabsf(v) < 1e-20f ? 0.0f : v;    /* no denormals in long silences */
}

static float output(struct speaker *s, float v)
{
    if (!s->filtered)
        return v * s->gain;
    s->lp = flush(s->lp + s->lp_a * (v - s->lp));
    s->hp_y = flush(s->hp_a * (s->hp_y + s->lp - s->hp_x));
    s->hp_x = s->lp;
    return s->hp_y * s->gain;
}

size_t speaker_render(struct speaker *s, uint64_t clk, float *out, size_t max)
{
    uint64_t target = clk * s->rate, end;
    size_t n = 0;

    while (s->now < target && n < max) {
        end = (s->now / PIT_HZ + 1) * PIT_HZ;       /* end of this sample */
        if (end > target) {
            advance(s, target);
            break;
        }
        advance(s, end);
        out[n++] = output(s, (float)((double)s->acc / PIT_HZ));
        s->acc = 0;
    }
    return n;
}
