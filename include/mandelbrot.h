/*****************************************************************
Mandelbrot Upic -- fractal generator (interface)

PHASE 1 (2026-09-09): fixed-point escape-time iteration + a crude
fixed (non-histogram) palette -- see docs/MANDELBROT_ALGORITHM.md for
the full phased plan. Palette optimisation (Phase 2, histogram-based)
and the cardioid/period-2-bulb early-skip (Phase 3) are not
implemented yet.

Credit: the fixed-point algorithm design (Q5.11 range/precision
choice) is based on the documented approach in 0x444454/mandelbr8
(https://github.com/0x444454/mandelbr8, CC BY 4.0) -- a from-scratch C
reimplementation for Oscar64, not a port of their 6502 assembly. Their
squaring-table optimisation is deliberately NOT used here -- see
docs/MANDELBROT_ALGORITHM.md for why it doesn't fit this project's
memory layout. See CREDITS.md.
******************************************************************/

#ifndef _MANDELBROT_H_
#define _MANDELBROT_H_

#include "upic_viewer.h"

// Default view: real in [-2.0, 1.0], imaginary in [-1.0, 1.0] -- the
// classic full-set framing (the set's leftmost point, the cusp at
// real=-2, sits exactly on the left edge). Chosen so the per-pixel
// step is an EXACT Q5.11 integer (16, i.e. 1/128) in both axes, no
// rounding drift accumulated across 384/256 pixel steps -- see
// mandelbrot.c's own comment on MANDEL_DX/MANDEL_DY.
#define MANDEL_MAX_ITER 32

// mandelbrot_generate -- fill upic_buffer[]/upic_buffer_reloc[] (see
// upic_viewer.h) with a rendered Mandelbrot set at the default view,
// packed directly into Upic's nibble-packed column-major format.
// Palette: caller must still push mandelbrot_palette (see below) via
// uii_setpalette() -- this function only fills the pixel buffer.
//
// Iteration count -> color mapping is currently a crude fixed gradient
// (Phase 1), not the histogram-optimised mapping planned for Phase 2
// (see docs/MANDELBROT_ALGORITHM.md) -- color 0 is reserved for points
// that reach MANDEL_MAX_ITER (considered "in the set"), colors 1-15
// spread linearly across escaping iteration counts.
//
// Call after rombank_out() (upic_buffer_reloc, like upic_buffer,
// genuinely requires MMAP_NO_ROM active to write correctly -- see
// upic_viewer.h) and ideally after turbo_fast() (see turbo.h) --
// nothing about generation depends on turbo being on, but at stock
// 1 MHz this is slow enough to be worth avoiding.
void mandelbrot_generate(void);

// 16-entry RGB palette matching mandelbrot_generate()'s current
// (Phase 1) fixed color mapping -- push via uii_setpalette() after
// calling mandelbrot_generate(). Index 0 = black ("in the set").
extern const char mandelbrot_palette[48];

#pragma compile("mandelbrot.c")

#endif
