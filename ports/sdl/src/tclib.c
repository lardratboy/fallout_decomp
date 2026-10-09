/*
 * tclib.c -- the Turbo C library calls the game makes that fo_prefix.h
 * redirects here: rand/srand, exit, system, and the file calls used for
 * FALLOUT.HIS.
 *
 * Files: for now the DOS file model is the test harness's (open fails, so
 * the default high-score table is used; writes are accepted and dropped).
 * Milestone 4 adds the real FALLOUT.HIS with conversion to the 140-byte
 * DOS layout.
 */
#include <stdlib.h>
#include "machine.h"

#if defined(_MSC_VER)
#define NORETURN __declspec(noreturn)
#else
#define NORETURN __attribute__((noreturn))
#endif

/* Turbo C rand()/srand(): 32-bit LCG, multiplier 0x015A4E35, 15-bit result.
   srand() takes a 16-bit seed. */
static unsigned long tc_seed;

void fo_srand(unsigned seed)
{
    if (machine_seed_forced)
        seed = machine_seed;
    seed &= 0xFFFF;
    trace("srand %u\n", seed);
    tc_seed = seed;
}

int fo_rand(void)
{
    tc_seed = (tc_seed * 0x015A4E35UL + 1) & 0xFFFFFFFFUL;
    return (int)((tc_seed >> 16) & 0x7FFF);
}

/* exit(): DOS only keeps the low byte of the status */
NORETURN void fo_exit(int status)
{
    trace("exit %d\n", status & 0xFF);
    be->quit(status & 0xFF);
    abort();                            /* quit() does not return */
}

/* system(): only reached from the in-game ESC "boss key" */
int fo_system(const char *cmd)
{
    trace("exec\n");
    return be->boss_key(cmd);
}

/* ---------------------------------------------------------------- files */

#define HIS_FD 5

int fo_open(const char *path, int flags, ...)
{
    (void)path;
    (void)flags;
    return -1;
}

int fo_creat(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return HIS_FD;
}

int fo_close(int fd)
{
    (void)fd;
    return 0;
}

int fo_dos_read(int fd, void *buf, unsigned n)
{
    (void)fd;
    (void)buf;
    (void)n;
    return 0;
}

int fo_dos_write(int fd, const void *buf, unsigned n)
{
    (void)fd;
    (void)buf;
    return (int)n;
}
