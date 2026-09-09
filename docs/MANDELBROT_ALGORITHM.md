# Mandelbrot generation algorithm -- design plan

Status: **planning only, not implemented**. `include/mandelbrot.c`
currently fills a synthetic diagonal test pattern (see its own doc
comment) purely to prove the buildchain end to end. This document is
the plan for replacing that with a real fractal generator.

See `CREDITS.md` for attribution -- the fixed-point design below is
based on 0x444454/mandelbr8's documented algorithm
(https://github.com/0x444454/mandelbr8), reimplemented from scratch in
C for Oscar64, not ported from their 6502 assembly.

## Target output format (recap)

Upic (see `include/upic_viewer.h`): 384x256 pixels, 16 colors, stored
column-major as 192 byte-columns x 256 rows. Each byte packs 2
horizontally-adjacent pixels: for pixel x, its byte is
`buffer[(x/2)*256 + y]`; the LOW nibble holds the even x (x%2==0), the
HIGH nibble the odd x (x%2==1) -- confirmed against
`include/mandelbrot.c`'s current placeholder pattern (ported from
landoficeandfire's `fill_test_pattern()`). Color palette is a settable
16-entry RGB table (`uii_setpalette()`, 48 bytes).

## Fixed-point representation

Q5.11: a 16-bit signed value where bit 15 is sign, bits 14-11 are the
integer part, bits 10-0 are the fraction -- represents the range
[-16, +16) at a resolution of 1/2048. Maps directly onto Oscar64's
native `int` (16-bit signed) with no wrapper type needed: a Q5.11 value
`v` represents the real number `v / 2048.0`.

**Why Q5.11, not something else**: the Mandelbrot set itself sits
inside a circle of radius 2, but intermediate values during iteration
briefly exceed that -- mandelbr8's own measurements (see their README's
"max_values.jpg") justify Q5.11 as the range/precision compromise that
avoids overflow while keeping useful precision. No reason to
re-derive this from scratch; reuse their conclusion.

**Multiply**: `a * b` for two Q5.11 values needs a 32-bit intermediate
product (`long`), then a right-shift by 11 to rescale back to Q5.11,
with sign handled correctly (arithmetic shift, not logical). Oscar64
has this already: `long` multiply + shift, or reach for the
existing `mul32`/`divmod32`-style runtime helpers if profiling shows
the compiler's own codegen isn't tight enough. Start with plain C
(`(int)(((long)a * (long)b) >> 11)`) and let `-O2` do its job before
hand-optimizing anything.

## Squaring-table optimization: evaluated and NOT recommended here

mandelbr8's biggest speed win is a precomputed 32 KB table (16384
entries) that turns squaring (`x*x`, needed twice per iteration for
the escape-radius test and the real/imaginary update) into a table
lookup instead of a multiply, cutting the multiply count from 3 to 1
per iteration.

**This doesn't transfer well to this project's memory layout.**
mandelbr8 renders into an 8 KB VIC-II bitmap and can afford a 32 KB
table alongside it in the C64's 64 KB address space. This project's
Upic buffer alone is 47 KB (`UPIC_MAIN_BYTES`) -- there is no room left
for a 32 KB table in main RAM at the same time as the picture buffer.
Putting the table in REU instead doesn't work either: a squaring table
needs one lookup *per multiply, per iteration, per pixel* -- for
98,304 pixels at even a modest 20-iteration average, that's ~2 million
tiny, effectively-random REU accesses. This project already measured
REU transfer cost directly this session (a 192-byte transfer costs
~45us even at 64 MHz turbo, dominated by fixed per-transfer overhead,
not size) -- a 2-byte-at-a-time REU lookup at that rate is many minutes
of pure REU latency, worse than just multiplying.

**Recommendation**: skip the squaring table. Use direct fixed-point
multiplication (see above) and lean on 64 MHz turbo instead of
mandelbr8's stock-1MHz target -- this project has ~64x the raw cycle
budget mandelbr8 was designed around, which likely absorbs the
multiply cost mandelbr8 needed the table to avoid. Measure actual
generation time once Phase 1 (below) exists before deciding this needs
revisiting.

## Escape-time iteration

Standard algorithm, per pixel:
```
c = (cx, cy)          // this pixel's complex coordinate, Q5.11
z = (0, 0)
for i in 0..max_iterations:
    if zx*zx + zy*zy > 4.0 (Q5.11):   // escaped
        record iteration count i, stop
    new_zx = zx*zx - zy*zy + cx
    new_zy = 2*zx*zy + cy
    zx, zy = new_zx, new_zy
// reached max_iterations without escaping -> "in the set"
```
`zx*zx`, `zy*zy`, and `zx*zy` are the three Q5.11 multiplies per
iteration (see above -- no squaring-table shortcut here, so this really
is 3 multiplies/iteration, not mandelbr8's optimized 1).

**Cardioid and period-2-bulb early-skip** (recommended, cheap,
worthwhile): before iterating, test whether `c` lies inside the main
cardioid or the period-2 bulb using their closed-form inequalities (see
the Wikipedia Mandelbrot set article's "Optimizations" section for the
standard formulas). Points inside either region are ALWAYS in the set
and would otherwise burn the full `max_iterations` budget one at a
time -- since a large fraction of the image (everything near the
origin) falls in these two regions, this is a significant win for
comparatively little code, independent of the squaring-table question
above.

**Periodicity detection** (optional, more complex): detect exact-cycle
repetition in z to bail out early on bounded-but-periodic points. Real
win for deep zooms; skip for v1 given no interactive zoom is planned
yet (see "Future ideas" below) -- revisit if generation time at a fixed
default view is still too slow after the cardioid/bulb check.

## View window

Pick a fixed default view for v1: real range roughly [-2.5, 1.0],
imaginary range scaled to match Upic's 384:256 (3:2) pixel aspect
ratio rather than the set's natural ~1.4:1 bounding box -- exact
match isn't essential, a full-set view just needs to not clip the
interesting region. `max_iterations` needs its own tuning pass once
real timing numbers exist (mandelbr8 defaults are a starting point,
not a target -- this project's rendering cost profile, dominated by
the Upic border-raster display technique rather than a cheap bitmap
blit, is different enough that iteration-count tuning should be
re-derived here, not copied).

## Palette selection (the "optimised palette" from the original idea)

Two-pass design:

**Pass 1**: for every pixel, run the escape-time iteration, write the
raw iteration count (1 byte, since `max_iterations` fits in a byte) to
a REU-resident buffer (98,304 bytes -- one per pixel, trivial for
REU's 16 MB, and this is bulk *sequential* REU access the whole way
through, not the random-access pattern the squaring-table idea was
ruled out for above -- matches the same REU usage pattern already
proven safe for whole-picture and whole-MOD-file loads elsewhere in
this project family). Build a histogram of iteration-count frequency
while doing this pass (256 counters, one per possible byte value,
resident in ordinary C64 RAM -- small).

**Between passes**: choose 15 iteration-count "buckets" (color 0
reserved for "reached max_iterations" = in the set, conventionally
black) from the histogram -- e.g. equal-population buckets (each band
covers roughly 1/15th of the escaping pixels) rather than equal-width
iteration ranges, so no color is wasted on a rarely-hit iteration
count. Assign each bucket an RGB color from a chosen ramp (classic
"fire"/rainbow Mandelbrot palettes are well documented; picking one is
a cosmetic decision, not an algorithmic one). Push the resulting
16-entry palette via `uii_setpalette()`.

**Pass 2**: re-read the REU iteration-count buffer sequentially, map
each byte through the bucket assignment to a 4-bit color index, and
pack two pixels per byte directly into `upic_buffer`/
`upic_buffer_reloc` in Upic's own column-major layout -- no separate
"convert then pack" step, generate straight into the packed format
matching the exact addressing `include/mandelbrot.c`'s current
placeholder already uses.

This mirrors mandelbr8's own two-pass philosophy (a cheap first pass
informing a smarter second pass) for a different purpose: they use it
for tile-skip optimization against VIC-II's color-clash limits, which
don't exist for Upic (16 real colors per pixel, no per-block
restriction) -- so that part of their design doesn't need porting at
all, a real simplification this project gets essentially for free from
targeting Upic instead of a native bitmap mode.

## Memory budget (needs re-deriving once real code exists)

Unlike landoficeandfire's `upicmodplay` target, there is no `modplay.c`
here competing for the `$E800-$FFFF`/`main` budget -- `include/rombank.h`
was already trimmed of the overlay-loading mechanism that project
needed for exactly that reason (see its own header comment). Don't
assume this project needs the same N=8 `UPIC_RELOC_COLS` split,
`ovl1`-holds-nybbles trick, or tight region tuning landoficeandfire
fought hard for -- re-derive the real budget once the actual
generator's code size is known; it will very likely have much more
natural headroom in `main` than that project did. If it doesn't
(unexpectedly large generator code), the overlay mechanism is
available to port back in -- see landoficeandfire's `rombank.c`/
`upic_viewer.c` for the working pattern, and this repo's git history
of that project for the debugging story behind it (plain
`#pragma region`s below `$0801` holding real content corrupt the
`.prg`'s own load address on this toolchain -- confirmed the hard way;
the native `#pragma overlay` mechanism is the safe alternative).

## Suggested implementation phases

1. **Fixed-point core + naive display**: Q5.11 multiply/square
   primitives, escape-time loop for the fixed default view, a crude
   fixed (non-histogram) 16-band palette, direct-to-Upic packing, reuse
   the existing viewer to display it. Goal: something real and
   recognizably-a-fractal on screen, end to end.
2. **Histogram-based palette**: the two-pass REU design above, once
   Phase 1 proves the math and packing are correct.
3. **Cardioid/period-2-bulb early skip**: once real generation timing
   is measured, add this if it's worth the code -- likely is, it's a
   well-understood, self-contained win.
4. **Performance pass, if still needed**: profile before optimizing
   further. Periodicity detection is the next lever if generation time
   at 64 MHz turbo is still unsatisfying after phase 3.
5. **Future idea, out of v1 scope**: interactive pan/zoom via
   joystick, matching mandelbr8's own UX (their README's "CONTROLS"
   section). Q5.11's fixed precision caps useful zoom depth regardless
   (mandelbr8's own README notes the same limit) -- worth having in
   mind when deciding whether this is worth the added complexity of a
   re-render-on-input loop, rather than assuming it's free.
