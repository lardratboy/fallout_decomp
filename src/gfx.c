/*
 * gfx.c -- C equivalents of FallOut's hand-written assembly module.
 *
 * The original routines (code offsets 0x43AE-0x5FE3 of the unpacked EXE) were
 * written in assembly; the blitters even used fully unrolled 200-row loops.
 * The versions below reproduce their observable behaviour, including a few
 * quirks that the C code relies on (noted where they occur):
 *
 *   - rows are addressed by bumping the *segment* by y*20 paragraphs and using
 *     x as the offset (draw_mask, draw_sprite, blit, fill_rect, get/put_pixel,
 *     copy_rect); draw_box, tint_rect and swap_color_rect instead use offset
 *     y*320+x in the given segment.  Both address the same pixel.
 *   - widths/heights are treated as byte counts by most routines (all callers
 *     use values < 256).
 *   - get_pixel() returns a 16-bit word, i.e. two adjacent pixels.
 *   - copy_rect() copies whole words only: an odd width drops the last pixel
 *     of every row (and skews following rows by one byte).
 *
 * 16-bit word accesses use `unsigned short` (the same as `unsigned` under
 * Turbo C) so the file also behaves correctly in the native test harness.
 *
 * Written for Turbo C++ 1.0, small memory model.  BIOS/DOS calls use Turbo C
 * pseudo-registers + geninterrupt() so no extra library code is pulled in.
 */
#include <dos.h>
#include "gfx.h"

/* pixel (x,y) of a 320-byte-wide buffer, addressed like the original code */
#define PIX(seg, x, y)  ((unsigned char far *)MK_FP((seg) + (y) * 20, (x)))
#define OFS(seg, x, y)  ((unsigned char far *)MK_FP((seg), (y) * 320 + (x)))

/* ---------------------------------------------------------------- VGA / BIOS */

/* 43AE: set the CRTC display start address (hardware scrolling). Unused. */
void vga_set_start(unsigned addr)
{
    outportb(0x3D4, 0x0C);
    outportb(0x3D5, addr >> 8);
    outportb(0x3D4, 0x0D);
    outportb(0x3D5, addr & 0xFF);
}

/* 43CB: set the CRTC line-compare (split screen) register. Unused. */
void vga_set_split(unsigned line)
{
    unsigned char v;

    outport(0x3D4, ((line & 0xFF) << 8) | 0x18);        /* bits 0-7         */
    outportb(0x3D4, 0x07);                              /* overflow reg     */
    v = inportb(0x3D5);
    outport(0x3D4, (((v & 0xEF) | ((line >> 4) & 0x10)) << 8) | 0x07);  /* bit 8 */
    outportb(0x3D4, 0x09);                              /* max scan line    */
    v = inportb(0x3D5);
    outport(0x3D4, (((v & 0xBF) | ((line >> 3) & 0x40)) << 8) | 0x09);  /* bit 9 */
}

/* 4409: load all 256 DAC registers from pal (768 bytes, 6-bit RGB). */
void set_palette(unsigned char *pal)
{
    _ES = _DS;
    _DX = (unsigned)pal;
    _CX = 256;
    _BX = 0;
    _AX = 0x1012;
    geninterrupt(0x10);
}

/*
 * 441F: issues INT 10h/1A00h (read display combination code) but then ignores
 * the result and returns the byte at 0040:008A (the BIOS DCC table index).
 * The caller treats a value below 7 as "no VGA/MCGA".
 */
int vga_present(void)
{
    _AX = 0x1A00;
    geninterrupt(0x10);
    return *(unsigned char far *)MK_FP(0x40, 0x8A);
}

/* 4431: INT 10h function 00h. */
void set_video_mode(int mode)
{
    _BX = 0xFFFF;
    _AX = (unsigned char)mode;
    geninterrupt(0x10);
}

/*
 * 4440: never called.  Intended as a "recolour non-zero pixels" fill, but the
 * original overwrites BP before reading its stack arguments and tests each
 * pixel with "cmp al,al", so it never writes anything.  Kept for completeness.
 */
void dead_fill(unsigned seg, int x, int y, int w, int h, int color)
{
    (void)seg; (void)x; (void)y; (void)w; (void)h; (void)color;
}

/* ------------------------------------------------------------ memory / DOS */

/* 449F: copy nwords 16-bit words from src:0000 to dst:0000. */
void copy_seg(unsigned src, unsigned dst, unsigned nwords)
{
    unsigned short far *s = (unsigned short far *)MK_FP(src, 0);
    unsigned short far *d = (unsigned short far *)MK_FP(dst, 0);

    while (nwords--)
        *d++ = *s++;
}

/* 44BF: print a '$'-terminated string with INT 21h/09h, then CR LF. */
void dos_puts(char *s)
{
    _DX = (unsigned)s;
    _AH = 0x09;
    geninterrupt(0x21);
    _DL = '\r';
    _AH = 0x02;
    geninterrupt(0x21);
    _DL = '\n';
    _AH = 0x02;
    geninterrupt(0x21);
}

/* 44D7: read a key without echo (INT 21h/07h).  Extended keys return 0 first. */
int getkey(void)
{
    _AH = 0x07;
    geninterrupt(0x21);
    _AH = 0;
    return _AX;
}

/* 44DE: INT 21h/3Fh.  Returns AX (byte count, or the DOS error code). */
int dos_read(int fd, void *buf, unsigned n)
{
    _DX = (unsigned)buf;
    _CX = n;
    _BX = fd;
    _AH = 0x3F;
    geninterrupt(0x21);
    return _AX;
}

/* 44F0: INT 21h/40h.  Returns AX. */
int dos_write(int fd, void *buf, unsigned n)
{
    _DX = (unsigned)buf;
    _CX = n;
    _BX = fd;
    _AH = 0x40;
    geninterrupt(0x21);
    return _AX;
}

/* ------------------------------------------------------------- playfield */

#define CELLS   (17 * 9)

/* 4502 */
void board_clear(char *b)
{
    int i;

    for (i = 0; i < CELLS; i++)
        b[i] = 0;
}

/* 451A */
int board_count(char *b)
{
    int i, n = 0;

    for (i = 0; i < CELLS; i++)
        if (b[i] != 0)
            n++;
    return n;
}

/* 4533 */
void str_copy(char *d, char *s)
{
    while ((*d++ = *s++) != 0)
        ;
}

/*
 * 4552: remove every cell whose mark is set, then let each column fall:
 * the column is packed bottom-up into tmp[17] and copied back.
 */
void board_collapse(char *b, char *marks, char *tmp)
{
    int i, col, row;
    char *p, *t;

    for (i = 0; i < CELLS; i++)
        if (marks[i] != 0)
            b[i] = 0;

    for (col = 0; col < 9; col++) {
        for (i = 0; i < 17; i++)
            tmp[i] = 0;
        t = tmp + 16;
        for (row = 0, p = b + 16 * 9 + col; row < 17; row++, p -= 9)
            if (*p != 0)
                *t-- = *p;
        for (row = 0, p = b + col, t = tmp; row < 17; row++, p += 9)
            *p = *t++;
    }
}

/*
 * 45C5: mark every cell that is part of a run of three equal non-zero
 * symbols horizontally, vertically or on either diagonal.  marks[] is not
 * cleared first.
 */
#define RUN3(i, d)                                                      \
    if (b[i] != 0 && b[i] == b[(i) + (d)] && b[i] == b[(i) + 2 * (d)])  \
        m[i] = m[(i) + (d)] = m[(i) + 2 * (d)] = 1

void board_find_matches(char *b, char *m)
{
    int row, col;

    for (row = 0; row < 17; row++)              /* horizontal  */
        for (col = 0; col < 7; col++)
            RUN3(row * 9 + col, 1);
    for (col = 0; col < 9; col++)               /* vertical    */
        for (row = 0; row < 15; row++)
            RUN3(row * 9 + col, 9);
    for (col = 0; col < 7; col++)               /* down-right  */
        for (row = 0; row < 15; row++)
            RUN3(row * 9 + col, 10);
    for (col = 8; col > 1; col--)               /* down-left   */
        for (row = 0; row < 15; row++)
            RUN3(row * 9 + col, 8);
}

/* 46A5: BIOS tick counter (18.2 Hz) via INT 1Ah/00h. */
unsigned long get_ticks(void)
{
    unsigned hi, lo;

    _AH = 0;
    geninterrupt(0x1A);
    hi = _CX;
    lo = _DX;
    return ((unsigned long)hi << 16) | lo;
}

/* --------------------------------------------------------------- drawing */

/* 46AD: draw every non-zero source pixel in a single colour. */
void draw_mask(unsigned seg, unsigned char *src, int x, int y, int w, int h, int color)
{
    unsigned char far *d = PIX(seg, x, y);
    int i, j;

    for (i = 0; i < h; i++, d += 320 - w)
        for (j = 0; j < w; j++, d++, src++)
            if (*src != 0)
                *d = color;
}

/* 46F9: transparent sprite (colour 0 is see-through). */
void draw_sprite(unsigned seg, unsigned char *src, int x, int y, int w, int h)
{
    unsigned char far *d = PIX(seg, x, y);
    int i, j;

    for (i = 0; i < h; i++, d += 320 - w)
        for (j = 0; j < w; j++, d++, src++)
            if (*src != 0)
                *d = *src;
}

/* 4740: opaque sprite copy (original: unrolled rep movsw/movsb per row). */
void blit(unsigned seg, unsigned char *src, int x, int y, int w, int h)
{
    unsigned char far *d = PIX(seg, x, y);
    int i, j;

    for (i = 0; i < h; i++, d += 320 - w)
        for (j = 0; j < w; j++)
            *d++ = *src++;
}

/* 4F5A: solid rectangle (original: unrolled rep stosw/stosb per row). */
void fill_rect(unsigned seg, int x, int y, int w, int h, int color)
{
    unsigned char far *d = PIX(seg, x, y);
    int i, j;

    for (i = 0; i < h; i++, d += 320 - w)
        for (j = 0; j < w; j++)
            *d++ = color;
}

/* 577D: NB returns the word at (x,y): pixel (x,y) | pixel (x+1,y) << 8. */
int get_pixel(unsigned seg, int x, int y)
{
    return *(unsigned short far *)PIX(seg, x, y);
}

/* 579E */
void put_pixel(unsigned seg, int x, int y, int color)
{
    *PIX(seg, x, y) = color;
}

/* 57C2: copy a w x h block between buffers, a word at a time. */
void copy_rect(unsigned dseg, unsigned sseg, int dx, int dy,
               int sx, int sy, int w, int h)
{
    unsigned char far *d = PIX(dseg, dx, dy);
    unsigned char far *s = PIX(sseg, sx, sy);
    int i, j;

    for (i = 0; i < h; i++) {
        for (j = 0; j < (w >> 1); j++) {
            *(unsigned short far *)d = *(unsigned short far *)s;
            d += 2;
            s += 2;
        }
        d += 320 - w;
        s += 320 - w;
    }
}

/* 5E58: one-pixel rectangle outline. */
void draw_box(unsigned seg, int x, int y, int w, int h, int color)
{
    unsigned char far *d = OFS(seg, x, y);
    int i;

    for (i = 0; i < w; i++)
        *d++ = color;
    d += 320 - w;
    for (i = 0; i != h - 2; i++, d += 320) {
        d[0] = color;
        d[w - 1] = color;
    }
    for (i = 0; i < w; i++)
        *d++ = color;
}

/* 5EAE: recolour every non-zero pixel of a screen area (used for shadows). */
void tint_rect(unsigned seg, int x, int y, int w, int h, int color)
{
    unsigned char far *d = OFS(seg, x, y);
    int i, j;

    for (i = 0; i != h; i++, d += 320 - w)
        for (j = 0; j < w; j++, d++)
            if (*d != 0)
                *d = color;
}

/* 5EF7: replace colour `from` with `to` inside an area (menu highlight). */
void swap_color_rect(unsigned seg, int x, int y, int w, int h, int from, int to)
{
    unsigned char far *d = OFS(seg, x, y);
    int i, j;

    for (i = 0; i != h; i++, d += 320 - w)
        for (j = 0; j < w; j++, d++)
            if (*d == (unsigned char)from)
                *d = to;
}

/* 5F41: two-colour mask: non-zero source -> fg, zero -> bg. */
void draw_mask2(unsigned seg, unsigned char *src, int x, int y, int w, int h,
                int fg, int bg)
{
    unsigned char far *d = PIX(seg, x, y);
    int i, j;

    for (i = 0; i < h; i++, d += 320 - w)
        for (j = 0; j < w; j++, d++, src++)
            *d = (*src != 0) ? fg : bg;
}

/* 5F8C: copy a whole screen in 7 interleaved passes (dissolve effect). */
void dissolve7(unsigned src, unsigned dst)
{
    unsigned char far *s = (unsigned char far *)MK_FP(src, 0);
    unsigned char far *d = (unsigned char far *)MK_FP(dst, 0);
    unsigned i, n;
    int pass;

    for (pass = 0; pass < 7; pass++)
        for (i = pass, n = 0; n < 0x23B7; n++, i += 7)
            d[i] = s[i];
}

/* 5FB6: copy a whole screen in 9 interleaved passes (dissolve effect). */
void dissolve9(unsigned src, unsigned dst)
{
    unsigned char far *s = (unsigned char far *)MK_FP(src, 0);
    unsigned char far *d = (unsigned char far *)MK_FP(dst, 0);
    unsigned i, n;
    int pass;

    for (pass = 0; pass < 9; pass++)
        for (i = pass, n = 0; n < 0x1BC8; n++, i += 9)
            d[i] = s[i];
}
