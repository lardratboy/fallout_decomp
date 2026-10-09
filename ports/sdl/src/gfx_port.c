/*
 * gfx_port.c -- src/gfx.c, unmodified, plus replacements for the four
 * routines that pass a buffer to BIOS/DOS as `_DX = (unsigned)ptr`.  On a
 * 64-bit host that cast drops the upper bits of the pointer, so the
 * originals are renamed on inclusion and the versions below load the same
 * registers but hand the real pointer over in shim_ptr.
 */
#define set_palette gfx_unused_set_palette
#define dos_puts    gfx_unused_dos_puts
#define dos_read    gfx_unused_dos_read
#define dos_write   gfx_unused_dos_write
#include "gfx.c"
#undef set_palette
#undef dos_puts
#undef dos_read
#undef dos_write

#include "machine.h"

void set_palette(unsigned char *pal);
void dos_puts(char *s);
int  dos_read(int fd, void *buf, unsigned n);
int  dos_write(int fd, void *buf, unsigned n);

/* 4409: INT 10h/1012h, all 256 DAC registers from ES:DX. */
void set_palette(unsigned char *pal)
{
    _ES = _DS;
    shim_ptr = pal;
    _DX = 0;
    _CX = 256;
    _BX = 0;
    _AX = 0x1012;
    geninterrupt(0x10);
}

/* 44BF: INT 21h/09h, then CR LF with INT 21h/02h. */
void dos_puts(char *s)
{
    shim_ptr = s;
    _DX = 0;
    _AH = 0x09;
    geninterrupt(0x21);
    _DL = '\r';
    _AH = 0x02;
    geninterrupt(0x21);
    _DL = '\n';
    _AH = 0x02;
    geninterrupt(0x21);
}

/* 44DE: INT 21h/3Fh. */
int dos_read(int fd, void *buf, unsigned n)
{
    shim_ptr = buf;
    _DX = 0;
    _CX = n;
    _BX = fd;
    _AH = 0x3F;
    geninterrupt(0x21);
    return _AX;
}

/* 44F0: INT 21h/40h. */
int dos_write(int fd, void *buf, unsigned n)
{
    shim_ptr = buf;
    _DX = 0;
    _CX = n;
    _BX = fd;
    _AH = 0x40;
    geninterrupt(0x21);
    return _AX;
}
