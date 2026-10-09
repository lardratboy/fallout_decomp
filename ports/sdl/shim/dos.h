/* <dos.h> as seen by src/fallout.c and src/gfx.c in the port.

   Far memory is a flat 1 MB (+64 KB wrap) array, as in the test harness:
   MK_FP() masks segment and offset to 16 bits, so every far access stays
   inside shim_mem.  The pseudo-registers live in a union so that _AH/_AL
   alias _AX like on an 8086 (all supported hosts are little-endian). */
#ifndef FO_DOS_H
#define FO_DOS_H

#define far

extern unsigned char shim_mem[];
#define MK_FP(seg, ofs) \
    ((void *)(shim_mem + ((unsigned long)((seg) & 0xFFFF) << 4) + ((ofs) & 0xFFFF)))

union shim_regs {
    struct { unsigned short ax, bx, cx, dx, ds, es; } x;
    struct { unsigned char al, ah, bl, bh, cl, ch, dl, dh; } h;
};
extern union shim_regs shim_r;
#define _AX shim_r.x.ax
#define _BX shim_r.x.bx
#define _CX shim_r.x.cx
#define _DX shim_r.x.dx
#define _DS shim_r.x.ds
#define _ES shim_r.x.es
#define _AH shim_r.h.ah
#define _AL shim_r.h.al
#define _DL shim_r.h.dl

void geninterrupt(int n);
void outportb(int port, unsigned char v);
void outport(int port, int v);
unsigned char inportb(int port);
int allocmem(unsigned size, unsigned *segp);
int freemem(unsigned seg);
void sound(unsigned freq);
void nosound(void);
void delay(unsigned ms);

#endif
