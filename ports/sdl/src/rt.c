/*
 * rt.c -- the realtime backend: the game played on SDL3.
 *
 * The game is a 1991 DOS program: it never yields, it spins.  Every call it
 * makes into the machine (get_ticks, kbhit, getkey, delay) is therefore the
 * place where the port pumps events, presents the screen and, when the game
 * is only waiting, sleeps:
 *
 *  - get_ticks() returning the same value many times in a row means a loop
 *    waiting for the clock (tick waits, the falling piece, title screen, main
 *    menu, the explosion flash).  It sleeps until the next tick or the next
 *    70 Hz retrace, whichever comes first; a key press wakes it.
 *  - kbhit() with no get_ticks() call since the previous kbhit() means a loop
 *    that only polls the keyboard (pause screen, difficulty menu).  On the
 *    original its speed depended on the CPU; here each poll waits for the
 *    next 70 Hz retrace (mode 13h's refresh rate).
 *  - getkey() blocks; delay() sleeps for exactly that long.
 *
 * The speaker is rendered against the same clock (audio.c): every sound() and
 * nosound() is stamped with the time the game made it.  The original's
 * delay() was exact and nothing else ran; here a delay can overrun, e.g. while
 * a frame is presented.  While the speaker sounds, its clock leaves such an
 * overrun out, so a tone lasts exactly as long as the game asked.  Once the
 * speaker is off, its clock catches up with silence.
 *
 * The clock is the BIOS tick count (18.2 Hz) since start; it never wraps at
 * midnight (the original can stall when it does).
 *
 * FALLOUT.HIS is kept in the user's data folder (SDL_GetPrefPath), or where
 * --hiscores says, and replaced atomically when the game saves it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "audio.h"
#include "machine.h"
#include "keymap.h"
#include "keyscript.h"
#include "rt.h"
#include "rtclock.h"
#include "video.h"

#define IDLE_POLLS  64          /* same-tick get_ticks() calls before sleeping */
#define RETRACE_HZ  70
#define HIS_ORG     "bradptaylor"   /* data folder: .../bradptaylor/FallOut/ */
#define HIS_APP     "FallOut"

void pause_game(void);          /* fallout.c */
void save_high_scores(void);

static uint64_t t0_ns;
static unsigned long last_tick;
static int same_polls;
static int ticks_polled;        /* get_ticks() called since the last kbhit() */
static int quit_requested;
static uint64_t last_pump;
static int speaker_on;
static uint64_t speaker_lag;            /* real time - speaker time */
static uint64_t speaker_hold = UINT64_MAX;  /* speaker time a delay() ends at */

static uint64_t now_us(void)
{
    return (SDL_GetTicksNS() - t0_ns) / 1000;
}

static uint64_t speaker_us(void)
{
    uint64_t t = now_us() - speaker_lag;

    return t < speaker_hold ? t : speaker_hold;
}

static uint64_t next_retrace_us(uint64_t now)
{
    return (now * RETRACE_HZ / 1000000 + 1) * 1000000 / RETRACE_HZ;
}

/* ------------------------------------------------------ keyboard buffer */

/* The BIOS buffer holds 15 keystrokes of (ASCII, scan code).  INT 21h/07h
   returns an extended key as 0 and then, on the next call, its scan code. */
#define KBUF_SIZE 15

static struct { unsigned char ascii, scan; } kbuf[KBUF_SIZE];
static int kb_head, kb_count;
static int kb_pending = -1;     /* scan code still to be returned */

static void kb_push(int ascii, int scan)
{
    if (kb_count == KBUF_SIZE)
        return;                 /* full: the BIOS beeps and drops it */
    kbuf[(kb_head + kb_count) % KBUF_SIZE].ascii = (unsigned char)ascii;
    kbuf[(kb_head + kb_count) % KBUF_SIZE].scan = (unsigned char)scan;
    kb_count++;
}

static int kb_ready(void)
{
    return kb_pending >= 0 || kb_count > 0;
}

static int kb_read(void)
{
    int a;

    if (kb_pending >= 0) {
        a = kb_pending;
        kb_pending = -1;
        return a;
    }
    a = kbuf[kb_head].ascii;
    if (a == 0)
        kb_pending = kbuf[kb_head].scan;
    kb_head = (kb_head + 1) % KBUF_SIZE;
    kb_count--;
    return a;
}

/* --------------------------------------------- scripted input, screenshots */

static struct script_key *script;
static int script_n, script_pos;

#define MAX_SHOTS 16
static struct { unsigned long tick; const char *path; } shots[MAX_SHOTS];
static int nshots;

static void run_script(unsigned long t)
{
    int i;

    while (script_pos < script_n && script[script_pos].tick <= t) {
        int b = script[script_pos++].b;
        if (b == 0 && script_pos < script_n)
            kb_push(0, script[script_pos++].b);
        else
            kb_push(b, 0);
    }
    for (i = 0; i < nshots; i++)
        if (shots[i].path && t >= shots[i].tick) {
            if (video_screenshot(shots[i].path) != 0)
                fprintf(stderr, "screenshot %s: %s\n", shots[i].path, SDL_GetError());
            shots[i].path = 0;
        }
}

/* --------------------------------------------------------------- events */

static void rt_quit(int status);

static int is_fullscreen_key(const SDL_KeyboardEvent *k)
{
    if (k->key == SDLK_F11)
        return 1;
    if (k->key == SDLK_RETURN && (k->mod & SDL_KMOD_ALT))
        return 1;
    return k->key == SDLK_F && (k->mod & SDL_KMOD_GUI) && (k->mod & SDL_KMOD_CTRL);
}

static void on_key(const SDL_KeyboardEvent *k)
{
    int ascii, scan;

    if (is_fullscreen_key(k)) {
        if (!k->repeat)
            video_toggle_fullscreen();
        return;
    }
    if (k->key == SDLK_F12) {
        if (!k->repeat)
            video_toggle_aspect();
        return;
    }
    if (k->mod & (SDL_KMOD_ALT | SDL_KMOD_GUI))
        return;                 /* port and system shortcuts */
    if (keymap_translate(k->key,
                         SDL_GetKeyFromScancode(k->scancode, k->mod & SDL_KMOD_SHIFT, true),
                         k->mod, &ascii, &scan))
        kb_push(ascii, scan);   /* key repeat stands in for BIOS typematic */
}

static void on_event(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_QUIT:
        /* Act like F10, which every input loop of the game handles.  A second
           request (e.g. during the end-of-game tally) exits at once. */
        if (quit_requested)
            rt_quit(0);
        quit_requested = 1;
        kb_count = 0;
        kb_pending = -1;
        kb_push(0, 0x44);
        break;
    case SDL_EVENT_KEY_DOWN:
        on_key(&e->key);
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        video_invalidate();
        break;
    default:
        break;
    }
}

static void pump(void)
{
    SDL_Event e;

    while (SDL_PollEvent(&e))
        on_event(&e);
}

/* called on every poll: events at most once per ms, screen when due */
static void service(void)
{
    uint64_t now = now_us();

    if (now - last_pump >= 1000) {
        last_pump = now;
        pump();
        audio_update(speaker_us());
        if (script || nshots)
            run_script((unsigned long)ticks_at_us(now));
    }
    video_present_if_due(now);
}

/* sleep until `target` (us) or until an event arrives */
static void wait_until(uint64_t target)
{
    uint64_t now = now_us();

    if (target > now)
        SDL_WaitEventTimeout(NULL, (Sint32)((target - now + 999) / 1000));
    last_pump = 0;
    service();
}

/* -------------------------------------------------------------- backend */

static unsigned long rt_ticks(void)
{
    uint64_t now;
    unsigned long t;

    service();
    now = now_us();
    t = (unsigned long)ticks_at_us(now);
    if (t == last_tick && ++same_polls >= IDLE_POLLS) {
        uint64_t next_tick = us_of_tick(t + 1), retrace = next_retrace_us(now);
        wait_until(next_tick < retrace ? next_tick : retrace);
        t = (unsigned long)ticks_at_us(now_us());
    }
    if (t != last_tick)
        same_polls = 0;
    last_tick = t;
    ticks_polled = 1;
    return t;
}

static int rt_kbhit(void)
{
    service();
    if (!kb_ready() && !ticks_polled)
        wait_until(next_retrace_us(now_us()));
    ticks_polled = 0;
    return kb_ready() ? 0xFF : 0;
}

static int rt_getkey(void)
{
    while (!kb_ready())
        wait_until(next_retrace_us(now_us()));
    return kb_read();
}

static void rt_delay(unsigned ms)
{
    uint64_t end = now_us() + (uint64_t)ms * 1000, now;

    if (speaker_on)
        speaker_hold = end - speaker_lag;

    for (;;) {
        service();
        now = now_us();
        if (now >= end)
            break;
        if (end - now > 2000)               /* coarse sleep, keep pumping */
            SDL_DelayNS(SDL_min(end - now - 1000, 10000) * 1000);
        else
            SDL_DelayPrecise((end - now) * 1000);
    }
    if (speaker_on)
        speaker_lag = now - speaker_hold;   /* the overrun is not heard */
    speaker_hold = UINT64_MAX;
}

static void rt_speaker(unsigned divisor)
{
    audio_speaker(speaker_us(), divisor);
    speaker_on = divisor != 0;
    if (!speaker_on)
        speaker_lag = 0;
}

/* Mode 3 is only set for the boss key and at exit: the picture stays. */
static void rt_video_mode(int mode)
{
    (void)mode;
    video_invalidate();
}

static void rt_palette(const unsigned char *rgb, int first, int count)
{
    video_set_palette(rgb, first, count);
}

static void rt_con_out(const char *s, size_t n)
{
    fwrite(s, 1, n, stderr);
}

/* system(): only reached from the in-game ESC boss key, which here pauses
   the game exactly like P (the game restores its screen afterwards). */
static int rt_boss_key(const char *cmd)
{
    (void)cmd;
    pause_game();
    return 0;
}

static char *his_path;          /* FALLOUT.HIS; NULL: no data folder */
static enum { HIS_UNREAD, HIS_READ, HIS_SAVED } his_state;

static long rt_load_file(const char *name, unsigned char *buf, size_t max)
{
    SDL_IOStream *io;
    size_t n;

    (void)name;                 /* the game only ever opens FALLOUT.HIS */
    his_state = HIS_READ;
    if (!his_path || (io = SDL_IOFromFile(his_path, "rb")) == NULL)
        return -1;
    n = SDL_ReadIO(io, buf, max);
    SDL_CloseIO(io);
    return (long)n;
}

/* write a new file next to the old one, then rename it over the old one */
static void rt_save_file(const char *name, const unsigned char *buf, size_t n)
{
    char *tmp;
    SDL_IOStream *io;
    bool ok;

    (void)name;
    his_state = HIS_SAVED;      /* the game's table is scrambled now */
    if (!his_path || SDL_asprintf(&tmp, "%s.tmp", his_path) < 0)
        return;
    ok = (io = SDL_IOFromFile(tmp, "wb")) != NULL;
    if (ok) {
        ok = SDL_WriteIO(io, buf, n) == n;
        ok = SDL_CloseIO(io) && ok;
    }
    if (ok)
        ok = SDL_RenamePath(tmp, his_path);
    if (!ok) {
        fprintf(stderr, "high scores not saved: %s: %s\n", his_path, SDL_GetError());
        SDL_RemovePath(tmp);
    }
    SDL_free(tmp);
}

static void rt_quit(int status)
{
    /* Closing the window quits like F10, which does not save the high scores
       (only Q in the main menu does).  Save them anyway, once the game has
       read them and unless it has saved them already. */
    if (quit_requested && his_state == HIS_READ)
        save_high_scores();
    audio_shutdown();
    video_shutdown();
    SDL_Quit();
    exit(status);
}

static const struct backend rt_backend = {
    rt_ticks, rt_kbhit, rt_getkey, rt_delay, rt_speaker, rt_video_mode,
    rt_palette, rt_con_out, rt_boss_key, rt_load_file, rt_save_file, rt_quit
};

/* ---------------------------------------------------------------- start */

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  --fullscreen        start in fullscreen (toggle: F11, Alt+Enter, Cmd+Ctrl+F)\n"
        "  --scale N           window size, N x 320x240 (default 3)\n"
        "  --square            square pixels, integer scaling (toggle: F12)\n"
        "  --seed N            fixed random seed (0..65535)\n"
        "  --volume N          speaker volume, 0..100 (default 50; 0: no audio device)\n"
        "  --hiscores FILE     high-score file (default: FALLOUT.HIS in the user's\n"
        "                      data folder; a FALLOUT.HIS from DOS works as is)\n"
        "testing:\n"
        "  --keys SCRIPT       type the bytes of a key script at its BIOS ticks\n"
        "  --shot TICK:FILE    save the window as BMP at that tick (repeatable)\n"
        "  %s --harness SCRIPT TRACE SEED [MAX_TICKS]   deterministic replay\n",
        prog, prog);
}

int rt_main(int argc, char *argv[])
{
    static char *game_argv[] = { "FALLOUT.EXE", 0 };
    struct video_options vo = { 3, 0, 0 };
    int volume = 50;
    int i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--fullscreen") == 0)
            vo.fullscreen = 1;
        else if (strcmp(a, "--square") == 0)
            vo.square = 1;
        else if (strcmp(a, "--scale") == 0 && i + 1 < argc)
            vo.scale = atoi(argv[++i]);
        else if (strcmp(a, "--seed") == 0 && i + 1 < argc) {
            machine_seed_forced = 1;
            machine_seed = (unsigned)strtoul(argv[++i], 0, 10);
        } else if (strcmp(a, "--volume") == 0 && i + 1 < argc) {
            volume = atoi(argv[++i]);
            volume = SDL_clamp(volume, 0, 100);    /* SDL_clamp is a macro */
        } else if (strcmp(a, "--hiscores") == 0 && i + 1 < argc) {
            his_path = SDL_strdup(argv[++i]);
        } else if (strcmp(a, "--keys") == 0 && i + 1 < argc) {
            if ((script_n = keyscript_load(argv[++i], &script)) < 0)
                return 1;
        } else if (strcmp(a, "--shot") == 0 && i + 1 < argc && nshots < MAX_SHOTS) {
            char *colon = strchr(argv[++i], ':');
            if (!colon) {
                usage(argv[0]);
                return 2;
            }
            shots[nshots].tick = strtoul(argv[i], 0, 10);
            shots[nshots++].path = colon + 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    SDL_SetAppMetadata("FallOut", "1991", "net.bradptaylor.fallout");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (video_init(&vo) != 0) {
        fprintf(stderr, "video: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    audio_init(volume);
    if (!his_path) {
        char *dir = SDL_GetPrefPath(HIS_ORG, HIS_APP);
        if (!dir || SDL_asprintf(&his_path, "%sFALLOUT.HIS", dir) < 0) {
            fprintf(stderr, "no data folder, high scores will not be saved: %s\n",
                    SDL_GetError());
            his_path = NULL;
        }
        SDL_free(dir);
    }
    t0_ns = SDL_GetTicksNS();
    machine_init(&rt_backend, 1);
    return game_main(1, game_argv);
}
