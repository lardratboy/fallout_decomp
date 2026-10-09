/*
 * speaker.h -- the PC speaker, rendered as audio samples.
 *
 * The speaker is PIT channel 2 in mode 3 (square wave), switched on and off
 * through port 61h.  The model follows the original's Turbo C sound() and
 * nosound() (EXE offsets 0x7110 and 0x713C):
 *
 *  - sound() with the speaker off enables it and programs mode 3: the output
 *    starts high and toggles every half period.  An odd count N is high for
 *    (N+1)/2 clocks and low for (N-1)/2.
 *  - sound() with the speaker already on only writes the new count; the 8254
 *    loads it at the end of the current half-cycle, so the wave never breaks.
 *  - nosound() drops the speaker to low at once.
 *
 * Each sample is the exact fraction of its interval during which the output
 * was high (area sampling).  A one-pole low-pass then softens the edges, and
 * a DC-blocking high-pass removes the offset, like the speaker's coupling.
 * Time is counted in PIT clocks since start.  A change applies where the
 * last render stopped, so render up to the moment of the change first.
 */
#ifndef SPEAKER_H
#define SPEAKER_H

#include <stddef.h>
#include <stdint.h>

struct speaker {
    unsigned rate;              /* samples per second */
    float gain;
    int filtered;               /* 0: raw high fraction * gain (tests) */
    float lp_a, hp_a, lp, hp_x, hp_y;

    /* Time unit: 1 / (PIT_HZ * rate) s.  A PIT clock is `rate` units and a
       sample is PIT_HZ units, so every edge and sample boundary is exact. */
    uint64_t now;               /* rendered up to here */
    uint64_t acc;               /* high time so far in the current sample */
    int on;
    uint64_t origin, phase0;    /* wave position at t: t - origin + phase0 */
    uint64_t period, high;      /* the current wave, in units */
    uint64_t next_at;           /* a count written while on loads here; 0: none */
    unsigned next_div;
    uint64_t off_since;         /* PIT clock at which it last went off */
};

void speaker_init(struct speaker *s, unsigned rate, float gain, int filtered);

/* divisor 0 = nosound() */
void speaker_write(struct speaker *s, unsigned divisor);

/* Advance to `clk`, writing the samples that end by then, at most `max` of
   them.  Returns how many; when that is `max`, call again for the rest. */
size_t speaker_render(struct speaker *s, uint64_t clk, float *out, size_t max);

/* PIT clock since which the speaker has been off; UINT64_MAX while on */
uint64_t speaker_off_since(const struct speaker *s);

#endif
