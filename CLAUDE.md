# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**Mandelbrot Upic** — a Commodore 64 Ultimate demo that generates a
Mandelbrot fractal on-device at 64 MHz turbo, packs it directly into
Upic format (a 16-color, 384x256 border-color raster picture
technique), displays it live as it renders, and lets the user
interactively pan and zoom into any region of the result. Targets
**Ultimate firmware 3.15 or newer only** (no fallback path for older
firmware) -- in practice this means an Ultimate 64 Elite 2 for now,
since the corresponding C64U firmware hasn't been released yet.

**Status**: v1.0.3, feature-complete. See `README.md` for controls and
installation, `docs/ARCHITECTURE.md` for the project layout,
`docs/MANDELBROT_ALGORITHM.md` for the fractal generator's design,
`docs/UPIC_VIEWER.md` for the display technique, and
`docs/ZOOM_FEATURE.md` for the interactive pan/zoom control scheme.
See `CREDITS.md` for full attribution.

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
  "Command Interface" in the Ultimate menu first (though
  `config/MandelbrotUpic-U64E2.cfg` enables it anyway, alongside U64
  turbo registers).
- **Palette control**: `uii_getpalette()`/`uii_setpalette()`/
  `uii_setpalettecolor()`/`uii_resetpalette()`, wrapping UCI control
  commands `$51`-`$54` (`GET_PALETTE`/`SET_PALETTE`/
  `SET_PALETTE_COLOR`/`RESET_PALETTE`) — this is how the generated
  fractal's palette gets pushed to real hardware colors.

Full protocol reference: `UCILIBMANUAL.md`. Since this demo requires
firmware 3.15+ unconditionally, there's no need to guard these calls
behind a version/capability check.

## Memory layout

The shared `upiccode`/`moddata`/`modbss` code/data/bss pool
(`$E800`-`$FFFF`) is this project's tightest memory budget -- see
`docs/ZOOM_FEATURE.md`'s memory-layout section before adding anything
there. Oscar64's linker can, in rare cases, silently wrap an object's
address past `$10000` back down near `$0000` instead of raising a
placement error. Always verify actual object placement via the
build's own `.map` file after changing anything in this pool -- a
clean build alone is not sufficient evidence of correct placement this
close to the boundary.

Interrupts are masked globally for the program's entire lifetime (see
`main.c`'s own comment) -- this program has no functional need for a
real interrupt, and this avoids a real class of bug where a same-tick
hardware interrupt chains into genuine KERNAL/JiffyDOS ROM code while
this program's own direct-CIA keyboard polling is active.

## Testing

No emulator automation exists for this platform -- VICE specifically
doesn't emulate the Ultimate's own UCI/turbo hardware this project
depends on. Manual/visual testing on real Ultimate 64 hardware is the
way to confirm any graphics- or control-affecting change.

## Code conventions

This project uses generously detailed comments throughout, not the
terse default style -- explaining WHY a design choice was made (a
hardware constraint, a memory-budget fight, a non-obvious interaction)
is valued here, since this codebase pushes close to several real
hardware and toolchain limits where that reasoning matters for future
changes.

## License

GPLv3 (see `LICENSE`). Any code added to this repository should be
compatible with GPLv3 licensing. Note: the fixed-point algorithm
*design* this project implements is informed by 0x444454/mandelbr8
(CC BY 4.0, see `CREDITS.md`) but no code from that project is used —
this is a from-scratch C reimplementation, so no license conflict.
