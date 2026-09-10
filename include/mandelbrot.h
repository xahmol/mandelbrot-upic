/*****************************************************************
Mandelbrot Upic -- fractal generator (interface)

PHASE 1 (2026-09-09): fixed-point escape-time iteration + a crude
fixed (non-histogram) palette, PLUS the cardioid/period-2-bulb
early-skip pulled in from the planned Phase 3 (needed sooner than
planned -- real hardware timing showed it matters a lot). Confirmed
rendering a correct, full-detail Mandelbrot set on real hardware --
see docs/MANDELBROT_ALGORITHM.md for the full phased plan. Palette
optimisation (Phase 2, histogram-based) is still not implemented.

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

// ---------------------------------------------------------------
// Q5.11 fixed point: a 16-bit signed value where bit 15 is sign,
// bits 14-11 are the integer part, bits 10-0 are the fraction --
// represents [-16, +16) at a resolution of 1/2048. Maps directly onto
// a plain 16-bit signed int, no wrapper type needed. See
// docs/MANDELBROT_ALGORITHM.md for why this range/precision (a
// documented choice from 0x444454/mandelbr8, reused here -- see
// CREDITS.md).
//
// Moved here from mandelbrot.c (2026-09-10) -- zoom.c needs the same
// representation/constants to compute a new view from a user-selected
// screen rectangle. Note the precision ceiling this puts on zooming:
// mandel_dx/dy (below) can't go below 1 raw unit (1/2048) without
// rounding to 0 (sampling every column/row at the same coordinate) --
// starting from the default view's step of 16, that's roughly a 16x
// total zoom-in budget before running out of precision. Deep zooming
// beyond that would need a wider fixed-point format (or arbitrary
// precision), a separate, bigger undertaking not attempted here.
// ---------------------------------------------------------------
typedef int fixed_t;

#define FIXED_SHIFT 11
#define FIXED_ONE   (1 << FIXED_SHIFT)          // 1.0 in Q5.11 = 2048
#define FIXED4      (4 << FIXED_SHIFT)          // 4.0 in Q5.11 -- escape-radius-squared threshold

#define MANDEL_MAX_ITER 32

// Current view bounds -- MUTABLE (2026-09-10, were #define constants)
// so zoom.c can retarget mandelbrot_generate() at an arbitrary
// user-selected sub-rectangle of whatever's currently displayed,
// instead of only ever the fixed default overview. Initialised (see
// mandelbrot.c) to the same default overview as before -- real in
// [-2.0, 1.0], imaginary in about [-0.996, 0.996] (not quite
// -1.0..1.0 -- see mandelbrot.c's own comment on the initial
// mandel_y0 value for why: an imperceptible crop in exchange for
// exploiting the set's real-axis mirror symmetry, though that
// optimisation is now conditional on the CURRENT view actually being
// symmetric -- see mandelbrot_generate()'s own comment. Not generally
// true after a zoom). mandel_dx/dy are equal for the default view
// (square pixels, no aspect distortion) but kept as two independent
// variables since a user-selected zoom rectangle's width/height in
// cells isn't necessarily equal.
extern fixed_t mandel_x0;
extern fixed_t mandel_y0;
extern fixed_t mandel_dx;
extern fixed_t mandel_dy;

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
// calling mandelbrot_generate(). Index 0 = black ("in the set"). Also
// mandel_palettes[0] below -- kept as its own named symbol too since
// main.c already references it directly.
extern const char mandelbrot_palette[48];

// Selectable base color gradients (2026-09-10) -- all four are 48-byte
// RGB48 blocks in the exact format uii_setpalette() expects, index 0
// always black ("in the set", never part of the escaping gradient,
// same convention regardless of gradient). zoom.c cycles through these
// on a keypress; which one's active is independent of and unaffected
// by mandel_bucket_hist's own per-generation histogram equalisation
// (see mandelbrot.c) -- that decides which of the 15 escaping indices
// each pixel gets, this decides what RGB color each index displays as,
// completely orthogonal to each other.
#define MANDEL_PALETTE_COUNT 4
extern const char *const mandel_palettes[MANDEL_PALETTE_COUNT];

// Generation time, captured from CIA1's TOD clock at the end of
// mandelbrot_generate() -- read back via ultimate_read_memory after a
// run for an objective timing measurement (no on-screen readout yet).
extern volatile unsigned char mandel_gen_mins;
extern volatile unsigned char mandel_gen_secs;
extern volatile unsigned char mandel_gen_tenths;

#pragma compile("mandelbrot.c")

#endif
