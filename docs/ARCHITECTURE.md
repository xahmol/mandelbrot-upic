# Architecture

## What this is

A Commodore 64 Ultimate demo that generates a Mandelbrot fractal
on-device at 64 MHz turbo, packs it directly into Upic format (a
16-color, 384x256 border-color raster picture technique), displays it
live as it renders, and lets the user interactively pan and zoom into
any region of the result. See `CREDITS.md` for the Upic technique's
own attribution.

## Program flow

1. Bank ROM out permanently (`rombank_out()`) and mask interrupts
   globally for the rest of the program's lifetime (see
   [Interrupts](#interrupts) below).
2. Detect the Ultimate Command Interface (UCI) and push the default
   color palette.
3. Enable 64 MHz turbo.
4. Generate the fractal (`mandelbrot_generate()`), showing it live as
   it builds.
5. Loop forever: let the user browse/zoom/cycle the palette
   (`zoom_select()`); regenerate at the newly selected view on a
   confirmed zoom or pan.

There is no exit -- see `docs/ZOOM_FEATURE.md` for why.

## Components

- **`src/main.c`** -- entry point; see [Program flow](#program-flow)
  above.
- **`include/mandelbrot.c`/`.h`** -- the fractal generator (fixed-point
  escape-time iteration, quarter-square multiply, cardioid/bulb early
  skip, selectable color gradients). See
  `docs/MANDELBROT_ALGORITHM.md`.
- **`include/upic_viewer.c`/`.h`** -- the Upic border-color raster
  renderer. See `docs/UPIC_VIEWER.md`.
- **`include/zoom.c`/`.h`** -- interactive pan/zoom/palette-cycle
  control scheme, drawing corner markers directly into the picture
  buffer. See `docs/ZOOM_FEATURE.md`.
- **`include/rombank.c`/`.h`** -- permanent ROM-banking setup shared by
  every module that needs it.
- **`include/turbo.c`/`.h`** -- Ultimate 64 CPU speed control. See
  `TURBOCONTROLMANUAL.md`.
- **`include/ultimate_common_lib.c`/`.h`** -- UCI protocol (palette
  control, device detection). See `UCILIBMANUAL.md`.

## Interrupts

Interrupts are masked globally, once, immediately after `rombank_out()`
in `main()`, and never re-enabled for the rest of the program's
lifetime. This program has no functional need for a real interrupt --
no music, no raster-IRQ effects, every wait loop (including the
picture viewer's own cycle-exact raster sync) is plain busy-polled --
so this costs nothing functionally, and avoids a real class of bug
where a same-tick hardware interrupt chains (via `rombank.c`'s
`mmap_trampoline()`) into genuine KERNAL/JiffyDOS ROM code while this
program's own direct-CIA keyboard polling is active. NMI (the RESTORE
key) isn't maskable this way and remains chained through the
trampoline as a safety net, but isn't otherwise relied on.

## Memory layout

See `docs/UPIC_VIEWER.md`'s own memory-regions table for the full
picture-buffer/code-region split, and `docs/ZOOM_FEATURE.md`'s memory-
layout section for the shared `upiccode` code/data/bss pool's current
budget -- the tightest constraint in this codebase, requiring careful,
verified-not-assumed object placement near its `$10000` boundary.

## Testing

No emulator automation exists for this platform -- VICE specifically
doesn't emulate the Ultimate's own UCI/turbo hardware this project
depends on. Manual/visual testing on real Ultimate 64 hardware is the
only way to confirm any change affecting the display or controls.
