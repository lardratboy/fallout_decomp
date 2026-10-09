/*
 * machine.c -- far memory, pseudo-registers and the BIOS/DOS services the
 * game uses.  geninterrupt() is the single entry point for INT 10h, 1Ah and
 * 21h, as in tools/emu/harness.c; the trace lines it writes match the
 * harness's so that replay traces can be compared byte for byte.
 */
#include <stdarg.h>
#include <string.h>
#include "machine.h"

unsigned char shim_mem[0x110000];
union shim_regs shim_r;
void *shim_ptr;

const struct backend *be;
FILE *trace_file;

int machine_alloc_fail = -1;
int machine_seed_forced;
unsigned machine_seed;

static unsigned alloc_next = 0x3000;

void trace(const char *fmt, ...)
{
    va_list ap;

    if (!trace_file)
        return;
    va_start(ap, fmt);
    vfprintf(trace_file, fmt, ap);
    va_end(ap);
}

void machine_init(const struct backend *b, int has_vga)
{
    be = b;
    shim_mem[0x48A] = has_vga ? 8 : 0;      /* BIOS display combination index */
}

/* ------------------------------------------------------------ BIOS / DOS */

static void int10(void)
{
    if (_AH == 0x00) {                      /* set video mode */
        int mode = _AL & 0x7F;
        if (mode == 0x13 && !(_AL & 0x80))
            memset(VRAM, 0, 65536);
        be->video_mode(mode);
    } else if (_AX == 0x1012) {             /* load DAC block from ES:DX */
        be->palette((const unsigned char *)shim_ptr, _BX, _CX);
    }
    /* 1A00h (display combination): ignored, like vga_present() does */
    trace("int10 %04x\n", _AX);
}

static void int21(void)
{
    switch (_AH) {
    case 0x07:                              /* key without echo */
        _AL = (unsigned char)be->getkey();
        break;
    case 0x09: {                            /* '$'-terminated string at DS:DX */
        const char *s = (const char *)shim_ptr;
        const char *e = strchr(s, '$');
        trace("puts\n");
        be->con_out(s, e ? (size_t)(e - s) : strlen(s));
        break;
    }
    case 0x02: {
        char c = (char)_DL;
        be->con_out(&c, 1);
        break;
    }
    case 0x3F:
        _AX = (unsigned short)fo_dos_read(_BX, shim_ptr, _CX);
        break;
    case 0x40:
        _AX = (unsigned short)fo_dos_write(_BX, shim_ptr, _CX);
        break;
    default:
        fprintf(stderr, "unhandled DOS function %02x\n", _AH);
        be->quit(255);
    }
}

void geninterrupt(int n)
{
    unsigned long t;

    switch (n) {
    case 0x10:
        int10();
        break;
    case 0x1A:                              /* get_ticks() */
        t = be->ticks();
        _CX = (unsigned short)(t >> 16);
        _DX = (unsigned short)t;
        _AX = 0;
        break;
    case 0x21:
        int21();
        break;
    default:
        fprintf(stderr, "unhandled interrupt %02x\n", n);
        be->quit(255);
    }
}

int kbhit(void)
{
    return be->kbhit();
}

/* the CRTC routines in gfx.c are never called */
void outportb(int port, unsigned char v) { (void)port; (void)v; }
void outport(int port, int v) { (void)port; (void)v; }
unsigned char inportb(int port) { (void)port; return 0; }

/* Turbo C allocmem(): -1 on success, else the largest available block.
   The two 64000-byte buffers go at 3000:0 and 4000:0, like the harness. */
int allocmem(unsigned size, unsigned *segp)
{
    int remaining = (0x5000 - alloc_next) / 0x1000;

    (void)size;
    if (machine_alloc_fail >= 0 && remaining <= machine_alloc_fail)
        return 0x100;
    *segp = alloc_next;
    alloc_next += 0x1000;
    return -1;
}

int freemem(unsigned seg)
{
    (void)seg;
    return 0;
}

/* Turbo C sound(): the original (EXE offset 0x7110) returns without touching
   the speaker when freq <= 18 (1193181 / freq would not fit in 16 bits), so
   sound(0) and sound(1) are no-ops rather than divide errors. */
void sound(unsigned freq)
{
    trace("sound %u\n", freq & 0xFFFF);
    if (freq <= 18)
        return;
    be->speaker((unsigned)(1193181UL / freq));
}

void nosound(void)
{
    trace("nosound\n");
    be->speaker(0);
}

void delay(unsigned ms)
{
    be->delay(ms);
}
