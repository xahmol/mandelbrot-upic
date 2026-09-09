/*****************************************************************
Mandelbrot Upic -- welcome/progress screen

Generation takes ~25s even at 64 MHz turbo (see docs/MANDELBROT_ALGORITHM.md) --
without this, the screen just sits at a frozen BASIC-ready prompt the whole
time. This draws a title + a percentage/bar progress display using plain
POKEs to screen RAM ($0400) and color RAM ($D800), not KERNAL CHROUT --
works regardless of rombank_out()'s ROM-banking state (same reason
mandelbrot.c's direct cia1.todt access already works during generation:
$D000-$DFFF I/O, which color RAM shares its select line with, stays
mapped in under MMAP_NO_ROM; only BASIC/KERNAL ROM at $A000/$E000 banks
out). Deliberately NOT using the Upic border-raster display technique
(upic_viewer.h) for this -- that's what caused the flicker bug when tried
mid-generation earlier (see mandelbrot.c's own history/comments); plain
text-mode POKEs have no such per-frame cost.
******************************************************************/

#ifndef _PROGRESS_H_
#define _PROGRESS_H_

// Draws the title and the static parts of the progress display (label,
// empty bar, "0%"). Call once, any time before mandelbrot_generate() --
// works whether or not rombank_out() has run yet.
void progress_init(void);

// Updates the progress bar/percentage. done/total are byte-columns
// completed so far / UPIC_WIDTH/2 (192) -- called once per completed
// column from mandelbrot_generate()'s own loop.
void progress_update(unsigned char done, unsigned char total);

#pragma compile("progress.c")

#endif
