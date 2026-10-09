/*
 * gfx.h -- low level VGA / board / DOS helpers.
 *
 * In the shipped FALLOUT.EXE these routines were hand-written 8086 assembly
 * (code offsets 0x43AE-0x5FE3, linked after the C module).  gfx.c contains C
 * equivalents.  All routines take video/buffer memory as a *segment* (0xA000
 * for the screen, or a 64000-byte block obtained with allocmem()) and address
 * pixels as seg:(y*320 + x) in mode 13h.
 */
#ifndef GFX_H
#define GFX_H

#define SCREEN_SEG  0xA000u

/* --- VGA / BIOS ------------------------------------------------------------ */
void vga_set_start(unsigned addr);              /* 43AE  CRTC start address        */
void vga_set_split(unsigned line);              /* 43CB  CRTC line compare         */
void set_palette(unsigned char *pal);           /* 4409  INT 10h/1012h, 256 colours */
int  vga_present(void);                         /* 441F  >= 7 when VGA/MCGA        */
void set_video_mode(int mode);                  /* 4431  INT 10h/00h               */
void dead_fill(unsigned seg, int x, int y, int w, int h, int color); /* 4440 unused */

/* --- memory / DOS ---------------------------------------------------------- */
void copy_seg(unsigned src, unsigned dst, unsigned nwords);    /* 449F */
void dos_puts(char *s);                         /* 44BF  '$'-terminated + CR/LF    */
int  getkey(void);                              /* 44D7  INT 21h/07h, no echo      */
int  dos_read(int fd, void *buf, unsigned n);   /* 44DE  INT 21h/3Fh               */
int  dos_write(int fd, void *buf, unsigned n);  /* 44F0  INT 21h/40h               */

/* --- 9 x 17 playfield (char board[17][9]) ---------------------------------- */
void board_clear(char *b);                      /* 4502 */
int  board_count(char *b);                      /* 451A  number of non-zero cells  */
void str_copy(char *d, char *s);                /* 4533 */
void board_collapse(char *b, char *marks, char *tmp);  /* 4552 clear marked, drop  */
void board_find_matches(char *b, char *marks);  /* 45C5  runs of 3, 4 directions   */
unsigned long get_ticks(void);                  /* 46A5  INT 1Ah/00h               */

/* --- drawing (mode 13h, 320 bytes per line) -------------------------------- */
void draw_mask(unsigned seg, unsigned char *src, int x, int y, int w, int h, int color); /* 46AD */
void draw_sprite(unsigned seg, unsigned char *src, int x, int y, int w, int h);         /* 46F9 */
void blit(unsigned seg, unsigned char *src, int x, int y, int w, int h);                /* 4740 */
void fill_rect(unsigned seg, int x, int y, int w, int h, int color);                    /* 4F5A */
int  get_pixel(unsigned seg, int x, int y);     /* 577D  (reads a 16-bit word!)    */
void put_pixel(unsigned seg, int x, int y, int color);                                  /* 579E */
void copy_rect(unsigned dseg, unsigned sseg, int dx, int dy,
               int sx, int sy, int w, int h);                                           /* 57C2 */
void draw_box(unsigned seg, int x, int y, int w, int h, int color);                     /* 5E58 */
void tint_rect(unsigned seg, int x, int y, int w, int h, int color);                    /* 5EAE */
void swap_color_rect(unsigned seg, int x, int y, int w, int h, int from, int to);       /* 5EF7 */
void draw_mask2(unsigned seg, unsigned char *src, int x, int y, int w, int h,
                int fg, int bg);                                                        /* 5F41 */
void dissolve7(unsigned src, unsigned dst);     /* 5F8C  stride-7 screen dissolve  */
void dissolve9(unsigned src, unsigned dst);     /* 5FB6  stride-9 screen dissolve  */

#endif
