/*****************************************************************
Mandelbrot Upic -- shared permanent ROM-banking setup

This demo runs with BASIC+KERNAL ROM banked out (MMAP_NO_ROM) for its
ENTIRE lifetime, not just during specific effects -- this is what makes
$A000-$BFFF and $E000-$FFF9 usable as ordinary code/data space, without
which there isn't enough RAM for the Upic picture buffer alongside
anything else.

rombank_out() is idempotent and safe to call from multiple modules in
any order -- whichever runs first does the real setup via
mmap_trampoline() (Oscar64's c64/memmap.h: makes a real IRQ/NMI firing
while ROM is banked out land in a safe trampoline instead of executing
garbage -- see oscar64manual.md).

rombank_restore() puts BASIC+KERNAL+I/O back. Only call this from a
STANDALONE TEST HARNESS that wants to cleanly return to BASIC.
******************************************************************/

#ifndef _ROMBANK_H_
#define _ROMBANK_H_

void rombank_out(void);
void rombank_restore(void);

#pragma compile("rombank.c")

#endif
