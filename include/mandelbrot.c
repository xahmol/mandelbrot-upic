/*****************************************************************
Mandelbrot Upic -- fractal generator (implementation)
See mandelbrot.h for API documentation and current status.
******************************************************************/

#include "mandelbrot.h"

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

// a*b for two Q5.11 values: 32-bit intermediate product, then
// rescale back to Q5.11 with an arithmetic (sign-preserving) right
// shift. Deliberately plain C, not hand-tuned assembly or a
// squaring-table lookup (see docs/MANDELBROT_ALGORITHM.md for why the
// latter doesn't fit this project's memory layout) -- let -O2 do its
// job first, revisit only if profiling on real hardware says so.
static fixed_t fixed_mul(fixed_t a, fixed_t b)
{
    return (fixed_t)(((long)a * (long)b) >> FIXED_SHIFT);
}

// Per-pixel step in both axes: EXACTLY 16 (= 1/128 in Q5.11), chosen
// so the default view's bounds divide evenly by 384/256 pixels with
// zero accumulated rounding error -- see mandelbrot.h's own comment on
// the view. Real axis: -4096 (-2.0) + x*16, x in 0..383, reaching
// +2048 (1.0) - 16 at the right edge. Imaginary axis: -2048 (-1.0) +
// y*16, y in 0..255, reaching +2048 (1.0) - 16 at the bottom edge.
#define MANDEL_X0 (-4096)
#define MANDEL_Y0 (-2048)
#define MANDEL_DX 16
#define MANDEL_DY 16

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
// No cardioid/period-2-bulb early-skip yet (planned Phase 3, see
// docs/MANDELBROT_ALGORITHM.md) and no squaring-table (deliberately
// not used at all, see mandelbrot.h) -- this is the straightforward
// 3-multiplies-per-iteration version, correctness first.
// ---------------------------------------------------------------
static unsigned char mandel_iterate(fixed_t cx, fixed_t cy)
{
    fixed_t zx = 0, zy = 0;
    unsigned char i;

    for (i = 0; i < MANDEL_MAX_ITER; i++)
    {
        fixed_t zx2 = fixed_mul(zx, zx);
        fixed_t zy2 = fixed_mul(zy, zy);

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

void mandelbrot_generate(void)
{
    unsigned bytecol, y;
    fixed_t cy;

    // Columns 0..UPIC_RELOC_COLS-1 -> upic_buffer_reloc[] ($E000),
    // the rest -> upic_buffer[] ($1800-territory) -- see
    // upic_viewer.h's own doc comment for why the picture is split
    // this way. Each byte-column covers 2 pixel columns (x=2*bytecol,
    // x=2*bytecol+1), packed low/high nibble -- see upic_viewer.h.
    for (bytecol = 0; bytecol < UPIC_WIDTH / 2; bytecol++)
    {
        fixed_t cx0 = (fixed_t)(MANDEL_X0 + (long)(bytecol * 2) * MANDEL_DX);
        fixed_t cx1 = (fixed_t)(cx0 + MANDEL_DX);

        for (y = 0, cy = MANDEL_Y0; y < UPIC_HEIGHT; y++, cy += MANDEL_DY)
        {
            unsigned char even = mandel_color(mandel_iterate(cx0, cy));
            unsigned char odd  = mandel_color(mandel_iterate(cx1, cy));
            unsigned char packed = (unsigned char)((odd << 4) | even);

            if (bytecol < UPIC_RELOC_COLS)
                upic_buffer_reloc[bytecol * UPIC_HEIGHT + y] = packed;
            else
                upic_buffer[(bytecol - UPIC_RELOC_COLS) * UPIC_HEIGHT + y] = packed;
        }
    }
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
