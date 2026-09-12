# Changelog

## [1.0.2]

Fixes a genuinely missing color shade, reported after v1.0.1 shipped.

- Fixed `mandel_color()`'s iteration-count-to-palette-index mapping:
  the old formula (`1 + iter*14/32`) merged escaping iteration counts
  0-2 into a single shade -- the only one of 14 shades covering three
  counts instead of two, and the most common/visible one, since low
  counts dominate any view's exterior background. Replaced with a
  fixed lookup table (`mandel_color_table[]`) that gives counts 0 and 1
  each their own shade. This also explains why the v1.0.1 noise-
  speckle bug showed up as isolated "islands" rather than pixels at a
  visible color boundary -- see `docs/MANDELBROT_ALGORITHM.md`'s
  "Palettes" section and `CREDITS.md`.
- Confirmed on real hardware: the previously-merged shade is now
  visibly distinct in all 4 palettes.

## [1.0.1]

Real-hardware bug-fix pass, plus a palette rework.

- Fixed a fixed-point overflow causing noise speckles in the generated
  fractal (`fixed_sqr`/`fixed_mul` in `include/mandelbrot.c`). See
  `CREDITS.md`.
- Fixed two zoom-box off-by-one bugs (`include/zoom.c`): a
  self-contradictory clamp at the largest box size, and the right/
  bottom corner markers landing one cell past the box's true edge.
- Fixed the picture's horizontal alignment (`render_frame()`'s timing
  pad in `include/upic_viewer.c`) so the box mode's left-edge corner
  markers no longer fall partly off-screen.
- Reworked all 4 color gradients for distinctness: every gradient's
  adjacent steps (including the black/white boundary steps) are now
  comfortably separated, and no two gradients read as near-identical
  at the same escape-iteration band. The former `ice` gradient was
  replaced by `amethyst` (black -> deep violet -> vivid magenta -> hot
  pink -> pale pink). See `docs/MANDELBROT_ALGORITHM.md`'s "Palettes"
  section for the full reasoning.
- Confirmed working on firmware 3.15a in addition to 3.15.

## [1.0.0]

Initial release.

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
