# Mandelbrot generation algorithm

How `include/mandelbrot.c` generates the fractal.

See `CREDITS.md` for attribution -- the fixed-point range/precision
choice is based on 0x444454/mandelbr8's documented algorithm
(https://github.com/0x444454/mandelbr8), reimplemented from scratch in
C for Oscar64, not ported from their 6502 assembly.

## Target output format (recap)

Upic (see `include/upic_viewer.h`): 384x256 pixels, 16 colors, stored
column-major as 192 byte-columns x 256 rows. Each byte packs 2
horizontally-adjacent pixels: for pixel x, its byte is
`buffer[(x/2)*256 + y]`; the LOW nibble holds the even x (x%2==0), the
HIGH nibble the odd x (x%2==1). Color palette is a settable 16-entry
RGB table (`uii_setpalette()`, 48 bytes) -- see [Palettes](#palettes)
below.

## Fixed-point representation

Q5.11: a 16-bit signed value where bit 15 is sign, bits 14-11 are the
integer part, bits 10-0 are the fraction -- represents the range
[-16, +16) at a resolution of 1/2048. Maps directly onto Oscar64's
native `int` (16-bit signed, `mandelbrot.h`'s `fixed_t`) with no
wrapper type needed: a Q5.11 value `v` represents the real number
`v / 2048.0`. The Mandelbrot set itself sits inside a circle of radius
2, but intermediate values during iteration briefly exceed that --
Q5.11 is the range/precision compromise that avoids overflow while
keeping useful precision.

**Zoom precision ceiling**: `mandel_dx`/`mandel_dy` (the current view's
per-pixel step) can't go below 1 raw unit (1/2048) without rounding to
0 -- sampling every column/row at the same coordinate, a degenerate
solid-color result. Starting from the default view's step of 16,
that's roughly a 16x total zoom-in budget before running out of
precision. The zoom feature's confirm step floors the computed step at
1 rather than letting it reach 0 (see `docs/ZOOM_FEATURE.md`). Deeper
zooming would need a wider fixed-point format or arbitrary precision.

## Multiply: quarter-square table

Squaring uses a **quarter-square multiply table**, `sq_table[512]`
(1024 bytes, `mandelbrot.c`). The identity
`a*b = floor((a+b)^2/4) - floor((a-b)^2/4)` is exact for any integers
`a,b` (their sum `a+b` and difference `a-b` always share parity, since
their sum is `2a`, so whichever square loses a remainder to the floor,
both lose the same amount and it cancels in the subtraction).
`sq_table[n] = floor(n*n/4)` for `n` up to 510 covers every possible
`a+b`/`a-b` for 8-bit unsigned `a,b` (max sum 510). `qmul8u(a,b)` is
then two table lookups and a subtract, replacing a shift-add multiply
loop -- a 1KB table, small enough to coexist with the 47KB Upic
picture buffer.

**Composing a full Q5.11 multiply**: `fixed_mul()` splits both
operands into high/low bytes and combines 4 unsigned 8x8 quarter-
square multiplies via the classic byte-split decomposition
(`a*b = al*bl + (al*bh+ah*bl)<<8 + ah*bh<<16`), sign-corrected
separately (operands negated to unsigned before multiplying, sign
XORed back in after) since the quarter-square identity only works on
unsigned inputs. `fixed_sqr()` (`a*a`) is a cheaper 3-multiply variant
of the same idea -- `al*bh` and `ah*bl` are identical when `a==b`, so
it's computed once and doubled instead of computed twice.

## Escape-time iteration

Standard algorithm, per pixel, expanded into real/imaginary parts:
```
zx' = zx^2 - zy^2 + cx
zy' = 2*zx*zy + cy
```
escape tested as `zx^2 + zy^2 > 4.0` (avoiding a square root), up to
`MANDEL_MAX_ITER` (32) iterations. `mandel_iterate()` takes the
cardioid/bulb check's already-hoisted terms (see below) as arguments
rather than recomputing them, and needs 3 quarter-square multiplies
per iteration (`zx^2`, `zy^2`, `zx*zy`).

**Cardioid and period-2-bulb early-skip** (`mandel_in_cardioid_or_bulb()`):
tests whether `c` lies inside the main cardioid or the period-2 bulb
using their closed-form inequalities before iterating at all:
```
main cardioid: q = (cx-1/4)^2 + cy^2 ; in set if q*(q+(cx-1/4)) < cy^2/4
period-2 bulb: (cx+1)^2 + cy^2 < 1/16
```
Points inside either region are ALWAYS in the set and would otherwise
burn the full 32-iteration budget one at a time -- a large fraction of
the visible image (everything near the origin) falls in these two
regions.

**Per-row/per-column hoisting**: `xm`/`xm2`/`xp1_2` (cardioid/bulb
terms) depend only on the column, `cy2` only on the row -- both
computed once per column/row (`cy2_table[]`, sized for the worst case
of a full 256-row non-mirrored generation) and passed in, rather than
recomputed once per pixel.

## View window and real-axis mirror symmetry

Default view: real range exactly [-2.0, +1.0), imaginary range about
[-0.996, +0.996] -- per-pixel step is exactly 16 raw Q5.11 units (1/128)
in both axes, chosen so the default view's bounds divide evenly by
384/256 pixels with zero accumulated rounding error. The imaginary
axis is deliberately offset by half a step (8 raw units) off the "true"
±1.0 framing so the 256-row grid is EXACTLY symmetric about `cy=0`: row
`y` and row `255-y` are exact negatives of each other in Q5.11, bit for
bit. This costs an imperceptible ~0.4% crop off the top/bottom edges in
exchange for exploiting the Mandelbrot set's own complex-conjugation
symmetry (`mandel_iterate(cx,cy) == mandel_iterate(cx,-cy)` always) --
`mandelbrot_generate()` iterates only the top half of a symmetric view
and mirrors each row's result into its exact opposite, roughly halving
the dominant cost (iteration count).

This symmetry is checked at runtime, not assumed -- it's a property of
the DEFAULT view's own specific bounds, not of the Mandelbrot set in
general. A user-selected zoom target (`zoom.c`) generally does NOT
straddle `cy=0` the way the default view deliberately does.
`mandelbrot_generate()` checks whether the CURRENT `mandel_y0`/
`mandel_dy` actually produce a symmetric view and only takes the
mirror-and-halve shortcut when valid; an asymmetric (typically zoomed)
view computes all 256 rows directly.

## Live build-up while generating

`mandelbrot_generate()` calls `upic_show_frame()` once per byte-column,
so the picture visibly builds up left-to-right as it's computed rather
than appearing all at once.

`mandel_gen_mins`/`mandel_gen_secs`/`mandel_gen_tenths` (CIA1 TOD clock,
reset at the start of generation) give an objective generation-time
measurement, readable via the Ultimate's own memory-read API.

## Palettes

Four selectable 48-byte RGB48 gradients (`mandelbrot_palette`,
`mandel_palette_fire`, `mandel_palette_amethyst`, `mandel_palette_rainbow`),
cycled via `C` on the zoom screen (`zoom.c`). Each maps
`mandel_color()`'s linear iteration-count-to-index gradient (0 = in
the set/black, 1-14 = linearly spread across escaping iteration
counts, 15 = reserved for the zoom feature's corner markers, white in
every gradient by construction) to actual RGB colors.

All 4 gradients were reworked 2026-09-12 (forum + direct feedback,
two rounds) against two concrete, measured problems, not just
eyeballed:

- **Adjacent steps too close to perceive as distinct.** A forum
  reader counted 11 visible shading bands in a screenshot and asked
  why not 14; mapping that screenshot's actual pixels to nearest
  palette index confirmed indices 1-2 covered under 1% of visible
  pixels between them -- present in the data, essentially invisible on
  screen, because they sat only ~25-30 RGB units from pure black.
  Separately, `mandelbrot_palette`'s pale end had the same problem
  approaching white. Every gradient's every adjacent step, including
  the 0->1 and 14->15 boundaries against the fixed black/white
  endpoints, is now checked to be at least ~26-37 RGB units apart
  (most are 40-100+) -- verified by generating swatch renders and
  measuring, not assumed.
- **Two palettes reading as near-identical.** The original default
  (blue -> pale -> orange/red) and the palette then called
  `mandel_palette_ice` (blue -> cyan -> white) turned out to share an
  almost exact blue ramp across their first 9 entries, hand-picked
  separately and never compared side by side -- since the fractal's
  exterior "lake" is dominated by exactly those low-iteration colors,
  the two looked nearly the same for most of the picture. Fixed by
  keeping default's own *identity* (the blue/pale/orange/red "sunset"
  gradient the project shipped with, cool end shifted toward
  indigo/violet rather than navy) and giving the fourth slot a
  completely different theme rather than a blue variant at all. That
  slot went through two more names before settling: a teal/cyan/mint
  "glacier" redesign satisfied the distance checks but read as flat
  and boring next to the other three; reverted in favor of
  `mandel_palette_amethyst` (black -> deep violet -> vivid magenta ->
  hot pink -> pale pink), built from keyframes resampled at equal RGB
  arc-length so the color spread stays even across all 15 steps
  regardless of how the hue curves, and pushed toward more saturated,
  brighter tones per direct feedback ("move from the purples to the
  more brights"). Verified index-by-index against all three other
  palettes: every same-position pair is at least ~32 RGB units apart
  (most 35+; the tightest is index 1, the shared dark corner where
  amethyst, the sunset default, and fire's pure-red start all compete
  for limited room -- was as low as 21 before any of this).

See `mandelbrot.c`'s own comments on each array for the full
reasoning, keyframe choices, and exact RGB-distance numbers.

## Memory budget

`sq_table` (1024 bytes) and `cy2_table` (512 bytes) both live in the
shared `upiccode`/`moddata`/`modbss` code/data/bss pool ($E800-$FFFF),
alongside the interactive zoom feature's own code and corner-marker
backup storage -- see `docs/ZOOM_FEATURE.md` for that pool's full
budget.

## Out of scope

- **Periodicity detection** -- would help deep zooms specifically, not
  worthwhile given the precision ceiling already limits useful zoom
  depth to roughly 16x.
- **Deeper zoom via wider fixed-point / arbitrary precision** -- a
  separate, bigger undertaking.
