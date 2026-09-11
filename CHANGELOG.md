# Changelog

## [1.0.0]

First feature-complete release.

### Added

- Fixed-point (Q5.11) Mandelbrot escape-time generator, with a
  quarter-square multiply table, cardioid/period-2-bulb early-skip,
  and real-axis mirror symmetry for views that support it. See
  `docs/MANDELBROT_ALGORITHM.md`.
- Live picture build-up: the fractal displays column by column as it
  generates, not just once complete.
- Upic border-color raster display (384x256, 16 colors). See
  `docs/UPIC_VIEWER.md`.
- Interactive pan and zoom: browse mode (WASD/cursor panning, gradual
  zoom-out) and box mode (move/resize a selection box, confirm to zoom
  in), with corner markers drawn directly into the picture buffer. See
  `docs/ZOOM_FEATURE.md`.
- 4 selectable color gradients (default blue/orange, fire, ice,
  rainbow), cycled live without leaving the current view.
- Ultimate 64 Elite 2 configuration file
  (`config/MandelbrotUpic-U64E2.cfg`), deployed/zipped as
  `mandelupic.cfg` alongside `mandelupic.prg` so the Ultimate's own
  firmware auto-loads it, enabling the Command Interface and U64 turbo
  registers this demo needs.

### Fixed

- A real-hardware crash where pressing any key during browsing/zoom
  dropped the display to a JiffyDOS text screen, root-caused to the
  permanent ROM-banking trampoline chaining hardware interrupts into
  real KERNAL/JiffyDOS code while this program's own direct-CIA
  keyboard polling was active. Fixed by masking interrupts globally
  for the program's entire lifetime -- see `main.c`'s own comment.

### Removed

- A per-generation histogram-equalised color remap, tried and shipped
  briefly, then removed on user feedback that it made the picture look
  worse than the plain linear gradient.
