# Architecture

## What this is

A Commodore 64 Ultimate demo that generates a Mandelbrot fractal
on-device at 64 MHz turbo, packs it directly into Upic format (the
border-color raster picture technique -- see `include/upic_viewer.h`),
and displays it live. Spun off from
[landoficeandfire](https://github.com/xahmol/landoficeandfire), which
built and hardware-validated the Upic viewer this project reuses.

## Current status (2026-09-09)

Buildchain scaffolded and builds/runs a placeholder: `include/mandelbrot.c`
fills the picture buffer with a synthetic diagonal test pattern (same
one landoficeandfire used to first validate its own viewer) instead of
a real fractal. See `docs/MANDELBROT_ALGORITHM.md` for the actual
generator's design -- not implemented yet.

## Layout

- `src/main.c` -- entry point: bank ROM out, generate the picture,
  push the palette, display it, wait for SPACE, restore.
- `include/mandelbrot.c`/`.h` -- the fractal generator. Currently a
  placeholder; see `docs/MANDELBROT_ALGORITHM.md`.
- `include/upic_viewer.c`/`.h` -- the Upic border-color raster
  renderer, ported as-is from landoficeandfire (hardware-proven there).
- `include/rombank.c`/`.h` -- permanent ROM-banking setup, trimmed down
  from landoficeandfire's version (no overlay-loading mechanism -- not
  needed here yet, see that file's own header comment for when it
  might be).
- `include/turbo.c`/`.h`, `include/ultimate_common_lib.c`/`.h` -- UCI
  protocol / turbo-speed control, ported as-is from landoficeandfire.
- `docs/MANDELBROT_ALGORITHM.md` -- the fractal generation design plan.
- `oscar64manual.md`, `UCILIBMANUAL.md`, `TURBOCONTROLMANUAL.md` --
  reference manuals (see global Claude Code conventions for why these
  are kept in-repo rather than re-fetched).

## What was deliberately NOT carried over from landoficeandfire

- `modplay.c`/`audio.c`/`ultimate_dos_lib.c` (MOD playback + file
  loading) -- this project has no audio component (yet -- could
  change) and no need to load files from disk (the fractal is
  generated, not loaded).
- The `#pragma overlay` code-loading mechanism in `rombank.h`/`.c` --
  built to solve a real memory-budget fight against `modplay.c`'s own
  code competing for the same `$E800-territory` region in
  landoficeandfire. Nothing here competes for that space yet. See
  `docs/MANDELBROT_ALGORITHM.md`'s memory-budget section for when this
  might need porting back in.
- `demo_reset()`/`demo_end_message()` -- landoficeandfire built these
  chasing a since-abandoned low-memory relocation approach; both ended
  up unused there too. Not carried over.

## Testing

No emulator automation exists for this platform (same as
landoficeandfire -- see its `CLAUDE.md`'s Testing section for why VICE
specifically doesn't work for UCI/cycle-exact code). Manual/visual
testing on real Ultimate 64 hardware is the way to confirm any
graphics-affecting change.
