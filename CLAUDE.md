# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**Mandelbrot Upic** — a Commodore 64 Ultimate demo that generates a
Mandelbrot fractal on-device at 64 MHz turbo, packs it directly into
Upic format (a 16-color, 384x256 border-color raster picture
technique), and displays it live. Targets **Ultimate firmware 3.15 or
newer only** (no fallback path for older firmware).

Spun off (2026-09-09) from
[landoficeandfire](https://github.com/xahmol/landoficeandfire), which
built and hardware-validated the Upic viewer this project reuses
as-is (`include/upic_viewer.c`/`.h`, `include/rombank.c`/`.h` trimmed
down -- see that file's own header comment for what was dropped and
why). See `CREDITS.md` for full attribution, `docs/ARCHITECTURE.md`
for the project layout, and `docs/MANDELBROT_ALGORITHM.md` for the
fractal generator's design plan.

**Status (2026-09-09)**: buildchain scaffolded and building/running a
placeholder (`include/mandelbrot.c` fills a synthetic test pattern
instead of a real fractal, same one landoficeandfire used to first
validate its own viewer) -- the real generator isn't implemented yet.
Read `docs/MANDELBROT_ALGORITHM.md` in full before touching
`include/mandelbrot.c` -- it covers the fixed-point (Q5.11) design, why
a squaring-table lookup table (mandelbr8's own biggest optimization)
was evaluated and rejected for THIS project's memory layout, the
two-pass histogram palette design, and the suggested implementation
phases.

## Toolchain (summary — see `docs/ARCHITECTURE.md` and the global
`~/.claude/CLAUDE.md`'s Oscar64 section for detail)

**Oscar64**, a C99/C++ cross-compiler targeting 6502/C64 — see
`oscar64manual.md` (canonical copy) before re-researching compiler
behavior.

- `make` / `make all` — compiles to `build/mandelupic.prg`,
  regenerates `README.pdf`, builds the release ZIP
- `make deploy` — FTP the compiled `.prg` to the Ultimate device set in
  `.env` (copy from `.env.example`, sets `ULTIP1`)
- `make docs` — regenerates `README.pdf` via pandoc

## Firmware 3.15+ features this demo is built around

- **UCI cartridge-side auto-enable**: `uii_wait_for_uci()` in
  `include/ultimate_common_lib.c` — no need for the user to turn on
  "Command Interface" in the Ultimate menu first.
- **Palette control**: `uii_getpalette()`/`uii_setpalette()`/
  `uii_setpalettecolor()`/`uii_resetpalette()`, wrapping UCI control
  commands `$51`-`$54` (`GET_PALETTE`/`SET_PALETTE`/
  `SET_PALETTE_COLOR`/`RESET_PALETTE`) — this is how the generated
  fractal's optimised palette gets pushed to real hardware colors.

Full protocol reference: `UCILIBMANUAL.md`. Since this demo requires
firmware 3.15+ unconditionally, there's no need to guard these calls
behind a version/capability check.

## Testing

No emulator automation exists for this platform (same as
landoficeandfire — see its `CLAUDE.md`'s Testing section for why VICE
specifically doesn't work for UCI/cycle-exact code). Manual/visual
testing on real Ultimate 64 hardware is the way to confirm any
graphics-affecting change.

## Code conventions

Default terse-comment style applies here (see the top-level global
instructions).

## License

GPLv3 (see `LICENSE`). Any code added to this repository should be
compatible with GPLv3 licensing. Note: the fixed-point algorithm
*design* this project implements is informed by 0x444454/mandelbr8
(CC BY 4.0, see `CREDITS.md`) but no code from that project is used —
this is a from-scratch C reimplementation, so no license conflict.
