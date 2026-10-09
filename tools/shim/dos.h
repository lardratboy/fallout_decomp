/* Minimal stand-in for Turbo C's <dos.h>, only for syntax/type checking the
   reconstructed sources with a modern compiler (tools/check.sh). */
#ifndef SHIM_DOS_H
#define SHIM_DOS_H
#define far
#define MK_FP(seg, ofs) ((void *)(((unsigned long)(seg) << 16) | (unsigned)(ofs)))
extern unsigned short _AX, _BX, _CX, _DX, _DS, _ES;
extern unsigned char _AH, _AL, _DL;
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
