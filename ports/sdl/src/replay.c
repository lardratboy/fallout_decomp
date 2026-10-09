/*
 * replay.c -- the deterministic backend: the same BIOS/DOS model as
 * tools/emu/harness.c and tools/emu/run_original.py, writing the same trace.
 *
 *   time:     every get_ticks() call returns the tick counter, which advances
 *             after every TICK_DIV calls;
 *   keyboard: a script of (tick, byte) pairs; a byte becomes available once
 *             the counter reaches its tick.  A blocking read with nothing
 *             available jumps the clock to the next byte; after 1000 empty
 *             kbhit() polls the clock also jumps (some loops never read it);
 *   delay(), sound() and nosound() take no time; system() succeeds at once.
 *
 * The trace has one line per get_ticks() call (tick, CRC-32 of the frame
 * buffer, score, level, bonus, total), the same state at every delay() call,
 * every sound()/nosound() call (machine.c), plus key/mode/exit events.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "machine.h"
#include "replay.h"
#include "keyscript.h"

static unsigned long ticks, calls, tick_div = 1, max_ticks = 200000;

static struct script_key *keys;
static int nkeys, kpos;

static void finish(const char *why)
{
    if (trace_file) {
        fprintf(trace_file, "end %s\n", why);
        fclose(trace_file);
    }
    printf("port:     %s after %lu ticks\n", why, ticks);
    fflush(stdout);
    _Exit(0);
}

static int key_ready(void)
{
    return kpos < nkeys && keys[kpos].tick <= ticks;
}

/* DUMP_TICKS=t1,t2,...: also write the board at those ticks */
static int in_list(const char *list, unsigned long t)
{
    char buf[32];
    size_t n;

    if (!list)
        return 0;
    n = (size_t)sprintf(buf, "%lu", t);
    for (;;) {
        if (strncmp(list, buf, n) == 0 && (list[n] == ',' || list[n] == 0))
            return 1;
        if ((list = strchr(list, ',')) == 0)
            return 0;
        list++;
    }
}

static void dump_state(unsigned long t)
{
    int r, c;

    if (!in_list(getenv("DUMP_TICKS"), t))
        return;
    fprintf(trace_file, "state %lu orient=%d pos=%d,%d piece=%02x%02x%02x\n", t,
            h_orient(), h_piece_row(), h_piece_col(), h_piece()[0] & 255,
            h_piece()[1] & 255, h_piece()[2] & 255);
    for (r = 0; r < 17; r++) {
        fprintf(trace_file, " ");
        for (c = 0; c < 9; c++)
            fprintf(trace_file, " %02x", h_board()[r * 9 + c] & 255);
        fprintf(trace_file, "\n");
    }
}

/* SHOT_TICKS=t1,t2,... SHOT_DIR=dir: dump VRAM (raw 320x200) at those ticks */
static void screenshot(unsigned long t)
{
    const char *dir = getenv("SHOT_DIR");
    char name[512];
    FILE *f;

    if (!dir || !in_list(getenv("SHOT_TICKS"), t))
        return;
    snprintf(name, sizeof name, "%s/shot_%06lu.raw", dir, t);
    if ((f = fopen(name, "rb")) != 0) {     /* first call at this tick only */
        fclose(f);
        return;
    }
    if ((f = fopen(name, "wb")) != 0) {
        fwrite(VRAM, 1, VRAM_SIZE, f);
        fclose(f);
    }
}

static void log_state(void)
{
    fprintf(trace_file, "%08lx %lu %u %u %lu\n",
            crc32_buf(VRAM, VRAM_SIZE), h_score() & 0xFFFFFFFFUL,
            h_level() & 0xFFFF, h_bonus() & 0xFFFF, h_total() & 0xFFFFFFFFUL);
}

static void log_tick(unsigned long t)
{
    dump_state(t);
    screenshot(t);
    fprintf(trace_file, "%lu ", t);
    log_state();
}

/* ------------------------------------------------------------- backend */

static unsigned long rp_ticks(void)
{
    unsigned long t;

    if (ticks >= max_ticks)
        finish("max ticks");
    t = ticks;
    log_tick(t);
    if (++calls % tick_div == 0)
        ticks++;
    return t;
}

static int rp_kbhit(void)
{
    static int idle;

    if (key_ready()) {
        idle = 0;
        return 0xFF;
    }
    if (++idle >= 1000) {
        idle = 0;
        if (kpos >= nkeys)
            finish("script exhausted");
        if (ticks < keys[kpos].tick)
            ticks = keys[kpos].tick;
    }
    return 0;
}

static int rp_getkey(void)
{
    if (!key_ready()) {
        if (kpos >= nkeys)
            finish("script exhausted");
        if (ticks < keys[kpos].tick)
            ticks = keys[kpos].tick;
    }
    fprintf(trace_file, "key %lu %02x\n", ticks, keys[kpos].b);
    return keys[kpos++].b;
}

/* the end-of-game tally reads no clock, so log the state at every delay() */
static void rp_delay(unsigned ms)
{
    fprintf(trace_file, "delay %u ", ms & 0xFFFF);
    log_state();
}
static void rp_speaker(unsigned divisor) { (void)divisor; }
static void rp_video_mode(int mode) { (void)mode; }
static void rp_palette(const unsigned char *rgb, int first, int count)
{
    (void)rgb; (void)first; (void)count;
}
static void rp_con_out(const char *s, size_t n) { (void)s; (void)n; }
static int rp_boss_key(const char *cmd) { (void)cmd; return 0; }
static void rp_quit(int status) { (void)status; finish("exit"); }

static const struct backend replay_backend = {
    rp_ticks, rp_kbhit, rp_getkey, rp_delay, rp_speaker, rp_video_mode,
    rp_palette, rp_con_out, rp_boss_key, rp_quit
};

/* SCRIPT TRACE SEED [MAX_TICKS], plus the harness's environment variables
   TICK_DIV, NO_VGA, ALLOC_FAIL, DUMP_TICKS, SHOT_TICKS/SHOT_DIR. */
int replay_main(int argc, char *argv[])
{
    static char *game_argv[] = { "FALLOUT.EXE", 0 };

    if (argc < 3) {
        fprintf(stderr, "usage: fallout --harness SCRIPT TRACE SEED [MAX_TICKS]\n");
        return 2;
    }
    if ((nkeys = keyscript_load(argv[0], &keys)) < 0)
        return 1;
    if ((trace_file = fopen(argv[1], "w")) == 0) {
        perror(argv[1]);
        return 1;
    }
    machine_seed_forced = 1;
    machine_seed = (unsigned)strtoul(argv[2], 0, 10);
    if (argc > 3)
        max_ticks = strtoul(argv[3], 0, 10);
    if (getenv("TICK_DIV"))
        tick_div = strtoul(getenv("TICK_DIV"), 0, 10);
    if (tick_div == 0)
        tick_div = 1;
    if (getenv("ALLOC_FAIL"))
        machine_alloc_fail = atoi(getenv("ALLOC_FAIL"));
    machine_init(&replay_backend, getenv("NO_VGA") == 0);
    return game_main(1, game_argv);
}
