/*
 * video.c -- the mode 13h screen: 320x200 bytes at A000:0000 through a
 * 256-colour palette of 6-bit DAC values.
 *
 * By default the picture is shown at 4:3, as a CRT showed mode 13h (pixels
 * 1:1.2), using SDL's pixel-art scaling so it stays sharp at any size.
 * F12 switches to square pixels with integer scaling.
 */
#include <string.h>
#include <SDL3/SDL.h>
#include "machine.h"
#include "video.h"

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static Uint32 pal32[256];
static unsigned char shown[VRAM_SIZE];  /* the frame last presented */
static int dirty = 1;
static int square;
static uint64_t frame_us = 1000000 / 60, last_check;

static void apply_aspect(void)
{
    if (square) {
        SDL_SetRenderLogicalPresentation(ren, 320, 200,
                                         SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    } else {
        SDL_SetRenderLogicalPresentation(ren, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART);
    }
    dirty = 1;
}

static void update_refresh_rate(void)
{
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(win));

    frame_us = 1000000 / 60;
    if (m && m->refresh_rate > 0)
        frame_us = (uint64_t)(1000000 / m->refresh_rate);
}

int video_init(const struct video_options *opt)
{
    int scale = opt->scale > 0 ? opt->scale : 3;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    square = opt->square;
    if (opt->fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    win = SDL_CreateWindow("FallOut", 320 * scale, (square ? 200 : 240) * scale, flags);
    if (!win)
        return -1;
    SDL_SetWindowMinimumSize(win, 320, 200);
    ren = SDL_CreateRenderer(win, NULL);
    if (!ren)
        return -1;
    SDL_SetRenderVSync(ren, 0);     /* presents are paced by the caller */
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                            SDL_TEXTUREACCESS_STREAMING, 320, 200);
    if (!tex)
        return -1;
    if (opt->fullscreen)
        SDL_HideCursor();
    apply_aspect();
    update_refresh_rate();
    return 0;
}

void video_shutdown(void)
{
    if (tex)
        SDL_DestroyTexture(tex);
    if (ren)
        SDL_DestroyRenderer(ren);
    if (win)
        SDL_DestroyWindow(win);
    tex = 0;
    ren = 0;
    win = 0;
}

/* INT 10h/1012h: DAC registers hold 6 bits per component */
void video_set_palette(const unsigned char *rgb6, int first, int count)
{
    int i;

    for (i = 0; i < count && first + i < 256; i++) {
        const unsigned char *c = rgb6 + 3 * i;
        Uint32 r = c[0] & 63, g = c[1] & 63, b = c[2] & 63;
        r = (r << 2) | (r >> 4);
        g = (g << 2) | (g >> 4);
        b = (b << 2) | (b >> 4);
        pal32[first + i] = 0xFF000000u | r << 16 | g << 8 | b;
    }
    dirty = 1;
}

static void render(void)
{
    void *pixels;
    int pitch, x, y;

    if (SDL_LockTexture(tex, NULL, &pixels, &pitch)) {
        for (y = 0; y < 200; y++) {
            Uint32 *d = (Uint32 *)((Uint8 *)pixels + y * pitch);
            const unsigned char *s = VRAM + y * 320;
            for (x = 0; x < 320; x++)
                d[x] = pal32[s[x]];
        }
        SDL_UnlockTexture(tex);
    }
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (square) {
        SDL_RenderTexture(ren, tex, NULL, NULL);
    } else {
        int w, h;
        SDL_FRect dst;
        SDL_GetCurrentRenderOutputSize(ren, &w, &h);
        if (w * 3 > h * 4) {                /* wider than 4:3: bars left/right */
            dst.h = (float)h;
            dst.w = (float)h * 4 / 3;
        } else {
            dst.w = (float)w;
            dst.h = (float)w * 3 / 4;
        }
        dst.x = (w - dst.w) / 2;
        dst.y = (h - dst.h) / 2;
        SDL_RenderTexture(ren, tex, NULL, &dst);
    }
}

void video_present_if_due(uint64_t now_us)
{
    if (!ren || now_us - last_check < frame_us)
        return;
    last_check = now_us;
    if (!dirty && memcmp(shown, VRAM, VRAM_SIZE) == 0)
        return;
    memcpy(shown, VRAM, VRAM_SIZE);
    dirty = 0;
    render();
    SDL_RenderPresent(ren);
}

void video_invalidate(void)
{
    dirty = 1;
    last_check = 0;
}

void video_toggle_fullscreen(void)
{
    int fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;

    SDL_SetWindowFullscreen(win, !fs);
    if (fs)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
    update_refresh_rate();
    video_invalidate();
}

void video_toggle_aspect(void)
{
    square = !square;
    apply_aspect();
    video_invalidate();
}

int video_screenshot(const char *path)
{
    SDL_Surface *s;
    int ok;

    render();
    s = SDL_RenderReadPixels(ren, NULL);
    ok = s && SDL_SaveBMP(s, path);
    SDL_DestroySurface(s);
    SDL_RenderPresent(ren);
    return ok ? 0 : -1;
}
