#ifndef VIDEO_H
#define VIDEO_H

#include <stdint.h>

struct video_options {
    int scale;          /* initial window size: multiples of 320x240 (or 320x200) */
    int fullscreen;
    int square;         /* square pixels, integer scaling (default: 4:3 like a CRT) */
};

int  video_init(const struct video_options *opt);
void video_shutdown(void);

void video_set_palette(const unsigned char *rgb6, int first, int count);
/* show the frame buffer if it changed; checks at most once per display refresh */
void video_present_if_due(uint64_t now_us);
void video_invalidate(void);            /* redraw on the next check */
void video_toggle_fullscreen(void);
void video_toggle_aspect(void);
int  video_screenshot(const char *path);    /* the window contents, as BMP */

#endif
