/*
 * tclib.c -- the Turbo C library calls the game makes that fo_prefix.h
 * redirects here: rand/srand, exit, system, and the file calls used for
 * FALLOUT.HIS.
 *
 * Files: the game opens one file, FALLOUT.HIS, reads or writes it whole and
 * closes it again.  Its high-score table has the DOS layout in memory (see
 * tools/emu/mkport.py), so the bytes go to and from the file unchanged and a
 * FALLOUT.HIS from DOS works as is.  The backend loads the file at open()
 * and saves it at close(); where it lives is up to the backend.
 */
#include <stdlib.h>
#include <string.h>
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

#define FILE_FD     5               /* DOS's first free handle */
#define FILE_MAX    4096

static struct {
    int open, writing;
    char name[64];
    unsigned char data[FILE_MAX];
    size_t len, pos;
} file;

static int file_start(const char *path, int writing)
{
    if (file.open || strlen(path) >= sizeof file.name)
        return -1;
    strcpy(file.name, path);
    file.open = 1;
    file.writing = writing;
    file.len = file.pos = 0;
    return FILE_FD;
}

int fo_open(const char *path, int flags, ...)
{
    long n;

    (void)flags;
    if (file_start(path, 0) < 0)
        return -1;
    if ((n = be->load_file(path, file.data, FILE_MAX)) < 0) {
        file.open = 0;                  /* DOS: file not found */
        return -1;
    }
    file.len = (size_t)n;
    return FILE_FD;
}

/* creat() truncates the file; the backend replaces it at close() */
int fo_creat(const char *path, int mode)
{
    (void)mode;
    return file_start(path, 1);
}

int fo_close(int fd)
{
    if (fd != FILE_FD || !file.open)
        return -1;
    if (file.writing)
        be->save_file(file.name, file.data, file.len);
    file.open = 0;
    return 0;
}

/* INT 21h/3Fh, 40h: the byte count (the game ignores it) */
int fo_dos_read(int fd, void *buf, unsigned n)
{
    if (fd != FILE_FD || !file.open || file.writing)
        return 0;
    if (n > file.len - file.pos)
        n = (unsigned)(file.len - file.pos);
    memcpy(buf, file.data + file.pos, n);
    file.pos += n;
    return (int)n;
}

int fo_dos_write(int fd, const void *buf, unsigned n)
{
    if (fd != FILE_FD || !file.open || !file.writing)
        return 0;
    if (n > FILE_MAX - file.len)
        n = (unsigned)(FILE_MAX - file.len);
    memcpy(file.data + file.len, buf, n);
    file.len += n;
    return (int)n;
}
