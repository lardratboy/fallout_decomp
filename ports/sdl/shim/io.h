/* <io.h> for the port (shadows the MSVC header of the same name).
   open/creat/close themselves are redirected by fo_prefix.h. */
#ifndef FO_IO_H
#define FO_IO_H
#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef S_IREAD
#define S_IREAD  0400
#define S_IWRITE 0200
#endif
#endif
