/*****************************************************************
Mandelbrot Upic -- fractal generator (implementation)
See mandelbrot.h for API documentation and current status.
******************************************************************/

#include <c64/cia.h>
#include <fixmath.h>
#include "mandelbrot.h"
#include "progress.h"

// Set at the end of mandelbrot_generate() from CIA1's TOD clock
// (reset to 0 at the start) -- read these back via
// ultimate_read_memory after a run to get an objective generation-time
// measurement, rather than guessing from how it looks/feels. Same
// simple reset+poll pattern ultimate_common_lib.c's uii_wait_for_uci()
// already uses successfully.
volatile unsigned char mandel_gen_mins = 0;
volatile unsigned char mandel_gen_secs = 0;
volatile unsigned char mandel_gen_tenths = 0;

// ---------------------------------------------------------------
// Q5.11 fixed point: a 16-bit signed value where bit 15 is sign,
// bits 14-11 are the integer part, bits 10-0 are the fraction --
// represents [-16, +16) at a resolution of 1/2048. Maps directly onto
// a plain 16-bit signed int, no wrapper type needed. See
// docs/MANDELBROT_ALGORITHM.md for why this range/precision (a
// documented choice from 0x444454/mandelbr8, reused here -- see
// CREDITS.md).
// ---------------------------------------------------------------
typedef int fixed_t;

#define FIXED_SHIFT 11
#define FIXED_ONE   (1 << FIXED_SHIFT)          // 1.0 in Q5.11 = 2048
#define FIXED4      (4 << FIXED_SHIFT)          // 4.0 in Q5.11 -- escape-radius-squared threshold

// a*b for two Q5.11 values: 32-bit intermediate product via Oscar64's
// own lmul16s() (fixmath.h -- a real 16x16->32 signed multiply, half
// the shift/add steps of the generic (long)a*(long)b path this used
// to be, which promotes both operands to 32-bit first and so compiles
// to a full 32x32->32 multiply for no reason), then rescale back to
// Q5.11 with an arithmetic (sign-preserving) right shift.
//
// A quarter-square-table multiply (see include/fastmul.c in git
// history, removed 2026-09-09) was tried here first -- independently
// verified correct (exhaustive Python check against all 65,536 8-bit
// pairs and 200,000 random 16-bit signed pairs, and the isolated
// single-call case matched on real hardware too) but gave silently
// WRONG results specifically when called repeatedly with caller-side
// locals that needed to stay live across several calls (confirmed via
// .asm inspection: the multiply's own internal scratch temporaries
// landed on the SAME statically-allocated zero-page slots the caller
// was using for its own locals). __noinline, volatile on the caller's
// locals, and __dynstack were all tried and confirmed (via identical
// byte-for-byte hardware output) to NOT fix it. Research into this
// (2026-09-09, delegated to a research pass -- see CREDITS.md) traced
// it to a documented, currently-open Oscar64 compiler bug class
// (upstream issues #318/#361 on drmortalwombat/oscar64): a function
// only gets Oscar64's default static, whole-program-shared zero-page
// frame if every function it calls, transitively, is also eligible --
// __dynstack only protects the one function it's applied to, not
// deeper callees, which is consistent with why it didn't help when
// applied only at the top of that 5-level call chain (multiply ->
// muls16 -> mulu16 -> 4x qmul8 -> 2x qsub8). lmul16s()/lmul16u() below
// avoid the whole problem by being ONE flat __native asm function each
// (see fixmath.c) -- no nested C calls, nothing but their own named
// parameters and the fixed `accu` scratch area. Oscar64's own official
// fractal sample (samples/fractals/mbfixed.c) calls this exact family
// repeatedly from inside a live escape-time loop with caller locals
// that must survive multiple calls -- the same calling shape used
// here, and known-working.
static fixed_t fixed_mul(fixed_t a, fixed_t b)
{
    return (fixed_t)(lmul16s(a, b) >> FIXED_SHIFT);
}

// x*x, for the (common -- two of three multiplies per iteration are
// squares) case where the sign-correction half of a full signed
// multiply is pure waste: negate first if negative, then an unsigned
// multiply needs no sign handling at all. Same lmul16u()/shift shape
// as fixmath.h's own lsqr4f12s() (a Q4.12 sibling of this), adapted to
// Q5.11's shift -- see fixed_mul()'s comment above for why this
// shallow, single-library-call shape is the safe one.
static fixed_t fixed_sqr(fixed_t a)
{
    if (a < 0)
        a = (fixed_t)(-a);
    return (fixed_t)(lmul16u((unsigned)a, (unsigned)a) >> FIXED_SHIFT);
}

// Per-pixel step in both axes: EXACTLY 16 (= 1/128 in Q5.11), chosen
// so the default view's bounds divide evenly by 384/256 pixels with
// zero accumulated rounding error -- see mandelbrot.h's own comment on
// the view. Real axis: -4096 (-2.0) + x*16, x in 0..383, reaching
// +2048 (1.0) - 16 at the right edge.
//
// Imaginary axis: -2040 (not -2048/-1.0 -- see below) + y*16, y in
// 0..255. Shifted by half a step (8 = 1/256 in Q5.11) off the "true"
// -1.0..+1.0 framing so the 256-row grid is EXACTLY symmetric about
// cy=0: row y and row 255-y are exact negatives of each other in
// Q5.11, bit for bit (cy(255-y) = -2040+16*(255-y) = 2040-16y =
// -(-2040+16y) = -cy(y)). The Mandelbrot set is symmetric under
// complex conjugation (mandel_iterate(cx,cy) == mandel_iterate(cx,-cy)
// always), so mandelbrot_generate() below only ever iterates the top
// half (y=0..127) and mirrors each row's result into its exact
// opposite -- roughly halves the dominant cost (iteration count) for
// free. Costs an imperceptible ~0.4% crop off both the top and bottom
// edges (true range becomes about -0.996..+0.996 instead of
// -1.0..+0.996) -- found during the same research pass as the
// lmul16s() swap above (see CREDITS.md), not previously noticed.
#define MANDEL_X0 (-4096)
#define MANDEL_Y0 (-2040)
#define MANDEL_DX 16
#define MANDEL_DY 16

// ---------------------------------------------------------------
// Cardioid / period-2-bulb early-skip (2026-09-09, moved up from
// planned Phase 3 -- real hardware timing showed this project's
// straightforward 3-multiplies-per-iteration approach, with no
// squaring table (see mandelbrot.h for why), needs it now, not later).
// Points inside either region are ALWAYS in the set and would
// otherwise burn the full MANDEL_MAX_ITER budget one iteration at a
// time -- a huge fraction of the visible image (everything near the
// origin) falls in these two closed-form-testable regions. Standard
// formulas (see the Wikipedia Mandelbrot set article's
// "Optimizations" section):
//   main cardioid: q = (cx-1/4)^2 + cy^2 ; in set if q*(q+(cx-1/4)) < cy^2/4
//   period-2 bulb: (cx+1)^2 + cy^2 < 1/16
//
// Takes xm/xm2/xp1_2/cy2 as ALREADY-COMPUTED arguments rather than raw
// cx/cy (2026-09-09) -- xm=cx-1/4, xm2=xm^2 and xp1_2=(cx+1)^2 depend
// only on the column, cy2=cy^2 only on the row, but this function used
// to be called once per PIXEL, recomputing all of them from scratch
// every time: cy2 alone was being recomputed 192x more often than
// needed (once per column instead of once for the whole row), and
// xm2/xp1_2 128x more often (once per row instead of once per
// column). mandelbrot_generate() now hoists these into a per-row table
// (cy2_table[]) and per-column locals, computed once each -- see its
// own comments. Pure loop-invariant hoisting, no new arithmetic or
// asm, so none of the fixed_mul()/fixed_sqr() risk above applies.
// ---------------------------------------------------------------
static char mandel_in_cardioid_or_bulb(fixed_t xm, fixed_t xm2, fixed_t xp1_2, fixed_t cy2)
{
    fixed_t q = (fixed_t)(xm2 + cy2);

    if ((long)fixed_mul(q, (fixed_t)(q + xm)) < ((long)cy2 >> 2))
        return 1;

    if ((long)(xp1_2 + cy2) < (long)(FIXED_ONE / 16))
        return 1;

    return 0;
}

// ---------------------------------------------------------------
// Escape-time iteration for one point. Returns the iteration count at
// which |z| exceeded 2 (escape radius, tested as |z|^2 > 4 to avoid a
// square root), or MANDEL_MAX_ITER if it never escaped (considered
// "in the set").
//
// z starts at 0; c = (cx, cy) is this pixel's coordinate. Standard
// z = z^2 + c update, expanded into real/imaginary parts:
//   zx' = zx^2 - zy^2 + cx
//   zy' = 2*zx*zy + cy
//
// No squaring-table (deliberately not used at all, see mandelbrot.h)
// -- this is the straightforward 3-multiplies-per-iteration version,
// just with the cardioid/bulb check above skipping the loop entirely
// for points that would otherwise run it to completion every time.
//
// xm/xm2/xp1_2/cy2: the cardioid/bulb check's already-hoisted terms
// (see mandel_in_cardioid_or_bulb()'s own comment) -- passed straight
// through, not recomputed here.
// ---------------------------------------------------------------
static unsigned char mandel_iterate(fixed_t cx, fixed_t cy, fixed_t xm, fixed_t xm2, fixed_t xp1_2, fixed_t cy2)
{
    fixed_t zx = 0, zy = 0;
    unsigned char i;

    if (mandel_in_cardioid_or_bulb(xm, xm2, xp1_2, cy2))
        return MANDEL_MAX_ITER;

    for (i = 0; i < MANDEL_MAX_ITER; i++)
    {
        fixed_t zx2 = fixed_sqr(zx);
        fixed_t zy2 = fixed_sqr(zy);

        // long, not fixed_t: zx2+zy2 can transiently approach the
        // Q5.11 range's own ceiling right at the escape threshold --
        // widen before adding to avoid 16-bit overflow at the compare.
        if ((long)zx2 + (long)zy2 > (long)FIXED4)
            return i;

        {
            fixed_t zxy = fixed_mul(zx, zy);
            zy = zxy + zxy + cy;   // 2*zx*zy + cy
            zx = zx2 - zy2 + cx;
        }
    }
    return MANDEL_MAX_ITER;
}

// Iteration count -> 4-bit color index. Crude fixed gradient (Phase 1)
// -- see mandelbrot.h's own doc comment. 0 = in the set (black),
// 1-15 = a linear spread across escaping iteration counts (low count
// = escaped fast = far from the set = start of the gradient).
static unsigned char mandel_color(unsigned char iter)
{
    if (iter >= MANDEL_MAX_ITER)
        return 0;
    return (unsigned char)(1 + ((unsigned)iter * 15) / MANDEL_MAX_ITER);
}

// One entry per row (y=0..127, see MANDEL_Y0's comment on why only the
// top half is ever needed) -- see mandelbrot_generate()'s own comment
// on why this is precomputed once instead of once per column.
static fixed_t cy2_table[UPIC_HEIGHT / 2];

void mandelbrot_generate(void)
{
    unsigned bytecol, y;

    // Live build-up via upic_show_frame() once per column, tried
    // 2026-09-09, REVERTED same day: confirmed on real hardware as
    // persistent flicker even after forcing the inter-column border
    // color to black (see git history) -- render_frame() takes a fixed
    // ~20ms (one real PAL frame, NOT sped up by turbo, since it's
    // synced to the actual raster beam), but computing one column takes
    // considerably longer than that, so every call was a brief flash of
    // the picture-so-far followed by a long static hold, regardless of
    // the hold's color. Back to pure precalculate-then-display: the
    // real display loop in main() (`while(!upic_show_frame())`) has no
    // such mismatch, since nothing else runs between its own back-to-
    // back calls. mandel_gen_mins/secs/tenths (see top of file) gives
    // an objective generation-time measurement instead of a progress
    // animation -- read back via ultimate_read_memory after a run.
    //
    // Columns 0..UPIC_RELOC_COLS-1 -> upic_buffer_reloc[] ($E000),
    // the rest -> upic_buffer[] ($1800-territory) -- see
    // upic_viewer.h's own doc comment for why the picture is split
    // this way. Each byte-column covers 2 pixel columns (x=2*bytecol,
    // x=2*bytecol+1), packed low/high nibble -- see upic_viewer.h.
    //
    // Fixed on real hardware (2026-09-09, confirmed correct): the
    // original version of this loop -- indexing both buffers with a
    // fresh `bytecol * UPIC_HEIGHT + y` multiply-add every row, and
    // accumulating `cy` via `cy += MANDEL_DY` in the for-loop's own
    // increment clause -- produced a picture that repeated the same
    // ~16-row band down the whole 256-row height instead of a smooth
    // gradient. Root cause not fully isolated (Oscar64 codegen issue,
    // not an algorithm bug -- the same fixed-point math was
    // independently verified correct in Python first). This version --
    // column base pointer computed ONCE (a single multiply per column,
    // not per pixel), `cy` recomputed directly from `y` each row
    // (`MANDEL_Y0 + y*MANDEL_DY`, not an accumulator) -- renders a
    // correct, full-detail Mandelbrot set on real hardware.
    cia1.todt = 0;
    cia1.tods = 0;
    cia1.todm = 0;

    // cy^2 depends only on the row, not the column -- precompute it
    // once here instead of leaving mandel_in_cardioid_or_bulb() (via
    // mandel_iterate()) to recompute it from scratch for every one of
    // the 192 columns that share the same row (see that function's own
    // comment). 128 entries * 2 bytes = 256 bytes, comfortably inside
    // budget.
    for (y = 0; y < UPIC_HEIGHT / 2; y++)
    {
        fixed_t cy = (fixed_t)(MANDEL_Y0 + (long)y * MANDEL_DY);
        cy2_table[y] = fixed_sqr(cy);
    }

    for (bytecol = 0; bytecol < UPIC_WIDTH / 2; bytecol++)
    {
        fixed_t cx0 = (fixed_t)(MANDEL_X0 + (long)(bytecol * 2) * MANDEL_DX);
        fixed_t cx1 = (fixed_t)(cx0 + MANDEL_DX);
        volatile char *dst = (bytecol < UPIC_RELOC_COLS)
            ? &upic_buffer_reloc[(unsigned)bytecol * UPIC_HEIGHT]
            : &upic_buffer[(unsigned)(bytecol - UPIC_RELOC_COLS) * UPIC_HEIGHT];

        // Cardioid/bulb terms that depend only on this column's cx,
        // not on the row -- same hoisting idea as cy2_table above, the
        // other axis. Computed once per column (twice -- cx0 and cx1
        // are two separate pixel columns packed into this byte-column,
        // see upic_viewer.h) instead of once per pixel.
        fixed_t xm0    = (fixed_t)(cx0 - FIXED_ONE / 4);
        fixed_t xm0_2  = fixed_sqr(xm0);
        fixed_t xp10_2 = fixed_sqr((fixed_t)(cx0 + FIXED_ONE));
        fixed_t xm1    = (fixed_t)(cx1 - FIXED_ONE / 4);
        fixed_t xm1_2  = fixed_sqr(xm1);
        fixed_t xp11_2 = fixed_sqr((fixed_t)(cx1 + FIXED_ONE));

        // Only the top half (y=0..127) is actually iterated -- each
        // row's result is mirrored straight into its exact opposite
        // (255-y), which MANDEL_Y0's shift (see its own comment above)
        // guarantees has the exact negated cy, and the Mandelbrot set
        // is symmetric under complex conjugation. Roughly halves this
        // loop's iteration count.
        for (y = 0; y < UPIC_HEIGHT / 2; y++)
        {
            fixed_t cy = (fixed_t)(MANDEL_Y0 + (long)y * MANDEL_DY);
            fixed_t cy2 = cy2_table[y];
            unsigned char even = mandel_color(mandel_iterate(cx0, cy, xm0, xm0_2, xp10_2, cy2));
            unsigned char odd  = mandel_color(mandel_iterate(cx1, cy, xm1, xm1_2, xp11_2, cy2));
            unsigned char packed = (unsigned char)((odd << 4) | even);
            dst[y] = packed;
            dst[UPIC_HEIGHT - 1 - y] = packed;
        }

        // Cheap (two 16-bit divides, no display-technique overhead) --
        // see progress.h for why this is plain POKEs, not upic_show_frame().
        progress_update((unsigned char)(bytecol + 1), (unsigned char)(UPIC_WIDTH / 2));
    }

    mandel_gen_tenths = cia1.todt;
    mandel_gen_secs   = cia1.tods;
    mandel_gen_mins   = cia1.todm;
}

// Blue -> pale -> orange/red gradient, hand-picked (cosmetic choice,
// not algorithmic -- see docs/MANDELBROT_ALGORITHM.md, this is a
// placeholder for the Phase 2 histogram-optimised palette). Index 0 =
// black, matches mandel_color()'s "in the set" case.
const char mandelbrot_palette[48] = {
    0x00,0x00,0x00,    //  0: black (in the set)
    0x00,0x07,0x3c,    //  1
    0x00,0x1a,0x69,    //  2
    0x0a,0x35,0x8c,    //  3
    0x14,0x5a,0xa0,    //  4
    0x28,0x82,0xb4,    //  5
    0x50,0xaa,0xc8,    //  6
    0x8c,0xcd,0xdc,    //  7
    0xc8,0xe6,0xeb,    //  8
    0xff,0xf5,0xdc,    //  9
    0xff,0xdc,0x96,    // 10
    0xff,0xbe,0x5a,    // 11
    0xff,0x96,0x28,    // 12
    0xe6,0x64,0x14,    // 13
    0xb4,0x3c,0x0a,    // 14
    0x6e,0x1e,0x05,    // 15
};
