#ifndef HARNESS_IO_H
#define HARNESS_IO_H
#include <fcntl.h>
#include <unistd.h>
#ifndef O_BINARY
#define O_BINARY 0x8000
#endif
#ifndef S_IREAD
#define S_IREAD  0400
#define S_IWRITE 0200
#endif
#endif
