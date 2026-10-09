#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/* the PC speaker on the default playback device; times are microseconds on
   the realtime backend's clock.  Without a device, all calls do nothing. */
int  audio_init(int volume);            /* 0..100; 0 opens no device */
void audio_speaker(uint64_t now_us, unsigned divisor);  /* 0 = off */
void audio_update(uint64_t now_us);     /* call often (at least every ~10 ms) */
void audio_shutdown(void);

#endif
