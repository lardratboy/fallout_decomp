/*
 * harness.c -- run the reconstructed FallOut sources natively, against the
 * same deterministic BIOS/DOS model as tools/emu/run_original.py, and write
 * the same trace format.  Comparing the two traces checks that the C code
 * behaves exactly like the original executable.
 *
 * usage: native_harness SCRIPT OUT.trace SEED [MAX_TICKS]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <zlib.h>
#include "dos.h"

unsigned char shim_mem[0x110000];
union shim_regs shim_r;

/* game variables sampled into the trace (accessors added by mkport.py) */
unsigned long h_score(void), h_total(void);
unsigned h_level(void), h_bonus(void);
int game_main(int argc, char *argv[]);

static FILE *trace;
static unsigned long ticks, calls, tick_div = 1, max_ticks = 200000;
static unsigned rand_seed_lo, rand_seed_hi, forced_seed;
static unsigned alloc_next = 0x3000;

struct key { unsigned long tick; unsigned char b; };
static struct key keys[100000];
static int nkeys, kpos;

#ifdef COVERAGE                 /* flush the profile before _exit() */
int __llvm_profile_write_file(void);
#endif

static void finish(const char *why)
{
#ifdef COVERAGE
    __llvm_profile_write_file();
#endif
    fprintf(trace, "end %s\n", why);
    fclose(trace);
    printf("native:   %s after %lu ticks\n", why, ticks);
    fflush(stdout);
    _exit(0);
}

/* the game's exit(): record the status like INT 21h/4Ch does */
void exit(int status)
{
    fprintf(trace, "exit %d\n", status & 0xFF);
    finish("exit");
}

static int key_ready(void)
{
    return kpos < nkeys && keys[kpos].tick <= ticks;
}

/* ------------------------------------------------------------- BIOS / DOS */

extern int orient;
char *h_board(void), *h_piece(void);
int h_piece_row(void), h_piece_col(void);

static void dump_state(unsigned long t)
{
    const char *env = getenv("DUMP_TICKS");
    char buf[32];
    int r, c;

    if (!env)
        return;
    sprintf(buf, "%lu", t);
    {   /* is t in the comma separated list? */
        const char *p = env;
        size_t n = strlen(buf);
        for (;;) {
            if (strncmp(p, buf, n) == 0 && (p[n] == ',' || p[n] == 0))
                break;
            if ((p = strchr(p, ',')) == 0)
                return;
            p++;
        }
    }
    fprintf(trace, "state %lu orient=%d pos=%d,%d piece=%02x%02x%02x\n", t, orient,
            h_piece_row(), h_piece_col(), h_piece()[0] & 255, h_piece()[1] & 255,
            h_piece()[2] & 255);
    for (r = 0; r < 17; r++) {
        fprintf(trace, " ");
        for (c = 0; c < 9; c++)
            fprintf(trace, " %02x", h_board()[r * 9 + c] & 255);
        fprintf(trace, "\n");
    }
}

/* SHOT_TICKS=t1,t2,... SHOT_DIR=dir: dump VRAM (raw 320x200) at those ticks */
static void screenshot(unsigned long t)
{
    const char *env = getenv("SHOT_TICKS"), *dir = getenv("SHOT_DIR");
    char name[512];
    FILE *f;

    if (!env || !dir)
        return;
    for (;;) {
        if (strtoul(env, 0, 10) == t)
            break;
        if ((env = strchr(env, ',')) == 0)
            return;
        env++;
    }
    sprintf(name, "%s/shot_%06lu.raw", dir, t);
    if ((f = fopen(name, "rb")) != 0) {     /* first call at this tick only */
        fclose(f);
        return;
    }
    if ((f = fopen(name, "wb")) != 0) {
        fwrite(shim_mem + 0xA0000, 1, 64000, f);
        fclose(f);
    }
}

static void log_state(void)
{
    fprintf(trace, "%08lx %lu %u %u %lu\n",
            crc32(0L, shim_mem + 0xA0000, 64000), h_score() & 0xFFFFFFFFUL,
            h_level() & 0xFFFF, h_bonus() & 0xFFFF, h_total() & 0xFFFFFFFFUL);
}

static void log_tick(unsigned long t)
{
    dump_state(t);
    screenshot(t);
    fprintf(trace, "%lu ", t);
    log_state();
}

void geninterrupt(int n)
{
    unsigned char ah = _AH;

    if (n == 0x1A) {
        unsigned long t;
        if (ticks >= max_ticks)
            finish("max ticks");
        t = ticks;
        log_tick(t);
        if (++calls % tick_div == 0)
            ticks++;
        _CX = (unsigned short)(t >> 16);
        _DX = (unsigned short)t;
        _AX = 0;
    } else if (n == 0x10) {
        if (ah == 0 && (_AL & 0x7F) == 0x13)
            memset(shim_mem + 0xA0000, 0, 65536);
        fprintf(trace, "int10 %04x\n", _AX);
    } else if (n == 0x21) {
        switch (ah) {
        case 0x07:
            if (!key_ready()) {
                if (kpos >= nkeys)
                    finish("script exhausted");
                if (ticks < keys[kpos].tick)
                    ticks = keys[kpos].tick;
            }
            fprintf(trace, "key %lu %02x\n", ticks, keys[kpos].b);
            _AL = keys[kpos++].b;
            break;
        case 0x09:
            fprintf(trace, "puts\n");
            break;
        case 0x02:
            break;
        default:
            fprintf(stderr, "unhandled DOS fn %02x\n", ah);
            finish("unhandled DOS fn");
        }
    } else {
        fprintf(stderr, "unhandled int %02x\n", n);
        finish("unhandled int");
    }
}

void outportb(int port, unsigned char v) { (void)port; (void)v; }
void outport(int port, int v) { (void)port; (void)v; }
unsigned char inportb(int port) { (void)port; return 0; }

int allocmem(unsigned size, unsigned *segp)
{
    /* ALLOC_FAIL=n: fail once at most n blocks remain (same as run_original) */
    int remaining = (0x5000 - alloc_next) / 0x1000;
    (void)size;
    if (getenv("ALLOC_FAIL") && remaining <= atoi(getenv("ALLOC_FAIL")))
        return 0x100;
    *segp = alloc_next;
    alloc_next += 0x1000;
    return -1;
}

int freemem(unsigned seg) { (void)seg; return 0; }
/* the speaker: logged as the calls the game makes */
void sound(unsigned freq) { fprintf(trace, "sound %u\n", freq & 0xFFFF); }
void nosound(void) { fprintf(trace, "nosound\n"); }
/* the end-of-game tally reads no clock, so log the state at every delay() */
void delay(unsigned ms)
{
    fprintf(trace, "delay %u ", ms & 0xFFFF);
    log_state();
}

/* INT 21h/0Bh.  Same idle rule as run_original.py: after 1000 empty polls
   the clock jumps to the next scripted key. */
int kbhit(void)
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

/* Turbo C rand()/srand(): 32-bit LCG, multiplier 0x015A4E35 */
static unsigned long tc_seed;

void srand(unsigned seed)
{
    (void)seed;                         /* use the original's seed instead */
    fprintf(trace, "srand %u\n", forced_seed);
    tc_seed = forced_seed;
}

int rand(void)
{
    tc_seed = (tc_seed * 0x015A4E35UL + 1) & 0xFFFFFFFFUL;
    return (int)((tc_seed >> 16) & 0x7FFF);
}

int system(const char *cmd) { (void)cmd; fprintf(trace, "exec\n"); return 0; }
/* Files, as in run_original.py: open() fails and writes are accepted.  With
   HIS_FILE=path, open() reads that file instead and writes are traced.
   gfx.c's dos_read()/dos_write() pass the buffer in _DX, which cannot hold a
   64-bit pointer, so the build renames them and these stand in. */
static unsigned char his[65536];
static size_t his_len, his_pos;

int open(const char *path, int flags, ...)
{
    const char *name = getenv("HIS_FILE");
    FILE *f;

    (void)path;
    (void)flags;
    if (!name || (f = fopen(name, "rb")) == 0)
        return -1;
    his_len = fread(his, 1, sizeof his, f);
    his_pos = 0;
    fclose(f);
    return 5;
}

int creat(const char *path, mode_t mode) { (void)path; (void)mode; return 5; }
int close(int fd) { (void)fd; return 0; }

int dos_read(int fd, void *buf, unsigned n)
{
    (void)fd;
    if (n > his_len - his_pos)
        n = (unsigned)(his_len - his_pos);
    memcpy(buf, his + his_pos, n);
    his_pos += n;
    return (int)n;
}

int dos_write(int fd, void *buf, unsigned n)
{
    unsigned i;

    (void)fd;
    if (getenv("HIS_FILE")) {
        fprintf(trace, "write %u ", n);
        for (i = 0; i < n; i++)
            fprintf(trace, "%02x", ((unsigned char *)buf)[i]);
        fprintf(trace, "\n");
    }
    return (int)n;
}

/* ------------------------------------------------------------- key script */

static void add_key(unsigned long tick, int b)
{
    keys[nkeys].tick = tick;
    keys[nkeys++].b = (unsigned char)b;
}

static void load_script(const char *path)
{
    static const struct { const char *name; int ext, code; } named[] = {
        {"F1", 1, 0x3B}, {"F10", 1, 0x44}, {"UP", 1, 0x48}, {"LEFT", 1, 0x4B},
        {"PAD5", 1, 0x4C}, {"RIGHT", 1, 0x4D}, {"DOWN", 1, 0x50},
        {"ENTER", 0, 13}, {"ESC", 0, 27}, {"BS", 0, 8}, {"TAB", 0, 9},
        {"SPACE", 0, 32}, {0, 0, 0}
    };
    char line[1024], *p, *tok;
    FILE *f = fopen(path, "r");

    if (!f) { perror(path); exit(1); }
    while (fgets(line, sizeof line, f)) {
        unsigned long tick;
        if ((p = strchr(line, '#')) != 0)
            *p = 0;
        if ((tok = strtok(line, " \t\r\n")) == 0)
            continue;
        tick = strtoul(tok, 0, 10);
        while ((tok = strtok(0, " \t\r\n")) != 0) {
            int i, done = 0;
            for (i = 0; named[i].name; i++)
                if (strcmp(tok, named[i].name) == 0) {
                    if (named[i].ext)
                        add_key(tick, 0);
                    add_key(tick, named[i].code);
                    done = 1;
                    break;
                }
            if (done)
                continue;
            if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
                add_key(tick, (int)strtol(tok, 0, 16));
            else
                for (p = tok; *p; p++)
                    add_key(tick, (unsigned char)*p);
        }
    }
    fclose(f);
}

int main(int argc, char *argv[])
{
    static char *game_argv[] = { "FALLOUT.EXE", 0 };

    if (argc < 4) {
        fprintf(stderr, "usage: %s SCRIPT TRACE SEED [MAX_TICKS]\n", argv[0]);
        return 2;
    }
    load_script(argv[1]);
    trace = fopen(argv[2], "w");
    forced_seed = (unsigned)strtoul(argv[3], 0, 10);
    if (argc > 4)
        max_ticks = strtoul(argv[4], 0, 10);
    if (getenv("TICK_DIV"))
        tick_div = strtoul(getenv("TICK_DIV"), 0, 10);
    shim_mem[0x48A] = getenv("NO_VGA") ? 0 : 8;     /* BIOS DCC index */
    (void)rand_seed_lo; (void)rand_seed_hi;
    return game_main(1, game_argv);
}
