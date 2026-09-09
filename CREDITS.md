# Credits

## Upic picture-viewer technique

Border-color raster picture technique by **Aleksi Eeben**
(aleksi.eeben@me.com) -- see `Source/upic.s` in the original Upic
package, https://csdb.dk/release/?id=263980. Ported to Oscar64/C for
the Ultimate 64 in the sibling project
[landoficeandfire](https://github.com/xahmol/landoficeandfire) by
Xander Mol; `include/upic_viewer.c`/`upic_viewer.h` and
`include/rombank.c`/`rombank.h` in this project are carried over
directly from that port (2026-09-09), with the overlay-loading
mechanism trimmed out since this project doesn't need it yet -- see
`include/rombank.h`'s own header comment.

Picture-buffer relocation (part of the picture data moved to $E000,
freeing low memory for ordinary code) was Aleksi Eeben's own suggestion,
relayed during landoficeandfire's development.

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

Ported from **UBoot64-v2** (https://github.com/xahmol/UBoot64-v2) via
landoficeandfire, itself based on Ultimate II Dos Lib by
**Scott Hutter** and **Francesco Sblendorio**
(https://github.com/xlar54/ultimateii-dos-lib) and the official
`ultimate_dos-1.2.docx`/`command interface.docx`
(https://github.com/markusC64/1541ultimate2/tree/master/doc).

## Toolchain

[Oscar64](https://github.com/drmortalwombat/oscar64), a C99/C++
cross-compiler targeting 6502/Commodore machines.
