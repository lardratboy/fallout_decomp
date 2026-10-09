/* Force-included into the generated fallout_port.c (-include / MSVC /FI).

   The real C library headers come first; the #defines below then send the
   game's library calls to the port, without editing src/:

   - rand/srand: Turbo C's 32-bit LCG.  Required, not just for determinism:
     host RAND_MAX is 2^31-1, and the game scales rand() by 32767.
   - exit/system/open/creat/close: the platform versions (the test harness
     instead redefines libc's own symbols, which MSVC will not link).
   - shutdown: the game has a shutdown(); on ELF systems an executable that
     exports that name can capture the socket call made by shared libraries. */
#ifndef FO_PREFIX_H
#define FO_PREFIX_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>

#if defined(_MSC_VER)
#define FO_NORETURN __declspec(noreturn)
#else
#define FO_NORETURN __attribute__((noreturn))
#endif

int  fo_rand(void);
void fo_srand(unsigned seed);
FO_NORETURN void fo_exit(int status);
int  fo_system(const char *cmd);
int  fo_open(const char *path, int flags, ...);
int  fo_creat(const char *path, int mode);
int  fo_close(int fd);

#define rand     fo_rand
#define srand    fo_srand
#define exit     fo_exit
#define system   fo_system
#define open     fo_open
#define creat    fo_creat
#define close    fo_close
#define shutdown fo_shutdown

#endif
