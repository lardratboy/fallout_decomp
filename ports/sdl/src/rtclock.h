/* The BIOS clock: INT 1Ah counts timer interrupts, one every 65536 cycles of
   the 1193182 Hz PIT, i.e. 18.2065 per second.  These convert between that
   count and microseconds since start (exact integers; no overflow for the
   first ~179 days). */
#ifndef RTCLOCK_H
#define RTCLOCK_H

#include <stdint.h>

#define PIT_HZ          1193182u
#define US_PER_TICK_NUM 65536000000ull          /* us per tick = this / PIT_HZ */

/* number of ticks that have elapsed after `us` microseconds */
static inline uint64_t ticks_at_us(uint64_t us)
{
    return us * PIT_HZ / US_PER_TICK_NUM;
}

/* the first microsecond at which tick `t` has elapsed */
static inline uint64_t us_of_tick(uint64_t t)
{
    return (t * US_PER_TICK_NUM + PIT_HZ - 1) / PIT_HZ;
}

#endif
