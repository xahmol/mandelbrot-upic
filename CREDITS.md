# Credits

## Upic picture-viewer technique

Border-color raster picture technique by **Aleksi Eeben**
(aleksi.eeben@me.com) -- see `Source/upic.s` in the original Upic
package, https://csdb.dk/release/?id=263980. Ported to Oscar64/C for
the Ultimate 64 by Xander Mol; `include/upic_viewer.c`/`upic_viewer.h`
and `include/rombank.c`/`rombank.h` implement this port, including a
picture-buffer relocation (part of the picture data moved to `$E000`,
freeing low memory for ordinary code) that was Aleksi Eeben's own
suggestion.

## Mandelbrot fixed-point algorithm design

Algorithm design (Q-format fixed-point iteration to avoid floating
point on 8-bit CPUs, squaring-table optimisation to cut the
per-iteration multiply count) based on the documented approach in
**0x444454/mandelbr8** -- https://github.com/0x444454/mandelbr8,
"A fast Mandelbrot generator for 8 bit computers", licensed CC BY 4.0.

This project does NOT use or port mandelbr8's own 6502 assembly code
(`src/6502/mandelbr8.asm`) -- it's a from-scratch C reimplementation
for Oscar64, informed by mandelbr8's publicly documented algorithm
description (its README's "ALGORITHM" section) rather than its source.
See `docs/MANDELBROT_ALGORITHM.md` for how this project's own design
adapts those ideas for Upic's very different output format (16-color
nibble-packed border-raster canvas, not a VIC-II multicolor bitmap --
no color-clash/tile logic is needed here as a result).

mandelbr8's own README additionally credits **TobyLobster**'s
`smult16.a` fast 6502 multiplication routine
(https://github.com/TobyLobster/multiply_test) for its assembly
implementation -- not used here (Oscar64 generates its own multiply
code from portable C), noted for completeness since it's part of the
lineage of ideas this project draws on.

## Ultimate 64 Command Interface (UCI) library

Ported from **UBoot64-v2** (https://github.com/xahmol/UBoot64-v2),
itself based on Ultimate II Dos Lib by
**Scott Hutter** and **Francesco Sblendorio**
(https://github.com/xlar54/ultimateii-dos-lib) and the official
`ultimate_dos-1.2.docx`/`command interface.docx`
(https://github.com/markusC64/1541ultimate2/tree/master/doc).

## Fixed-point overflow fix (v1.0.0 noise speckles)

**DDT** ([Lemon64 forum thread](https://www.lemon64.com/forum/viewtopic.php?t=85911))
challenged an earlier diagnosis of visible noise speckles in this
project's rendered output, arguing the multiply routine rather than the
add/sub steps was the likely cause, and included their own per-pixel
max-magnitude analysis of the same default view. That analysis was the
right kind of evidence, even though the fix ended up being about where
Q5.11 values get truncated back to 16 bits (in `fixed_sqr()`/`fixed_mul()`'s
own return path), not the quarter-square multiply algorithm itself, which
remains exact -- see `include/mandelbrot.c`'s own comments on both
functions for the full root-cause writeup. DDT is also **0x444454** on
GitHub, author of the mandelbr8 project this project's own fixed-point
algorithm design is credited to above.

## Missing color shade (v1.0.1 palette gradient)

**DDT** (see above) compared this project's rendered output against
their own [mandelbr8 reference renders](https://github.com/0x444454/mandelbr8)
across multiple 8-bit platforms and reported that the second-lowest
escaping iteration count was sharing a color with the lowest one
instead of getting its own distinct shade, also pointing out that this
explained why the earlier noise-speckle overflow bug showed up as
isolated "islands" rather than pixels sitting at a visible color
boundary. Root cause: `mandel_color()`'s iteration-count-to-palette-
index formula merged three consecutive iteration counts into one color
at that specific point (the only one of 14 colors that did), rather
than the intended two -- fixed by replacing the formula with a fixed
lookup table.

That first table pushed every other unavoidable merge point to the
high-iteration end instead, reasoning the busy detail band right at
the fractal boundary would hide it better. DDT's follow-up showed
that reasoning was wrong: a contour overlay comparing this project's
output against mandelbr8's own, at matching iteration boundaries,
showed multiple real color transitions simply missing in exactly that
band. Rebalanced to spread the necessary merges evenly across the
whole range instead of clustering them.

DDT also described their own mandelbr8 coloring scheme directly:
reserve only black, and let every other color (including white) be
part of the escaping-iteration gradient -- "I don't reserve white
either, I just pick the most white color of the iterations palette."
Adopted here too: every gradient now uses a full 15 colors, reaching
genuine white, instead of stopping short at a 14th near-white color
with white held back exclusively for the zoom feature's corner
markers. See `mandel_color_table[]`'s own comment in
`include/mandelbrot.c` and `docs/MANDELBROT_ALGORITHM.md`'s "Palettes"
section for the full writeup, including the marker-visibility tradeoff
this brings back.

## Toolchain

[Oscar64](https://github.com/drmortalwombat/oscar64), a C99/C++
cross-compiler targeting 6502/Commodore machines.
