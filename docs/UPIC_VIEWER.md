# Upic picture viewer

How `include/upic_viewer.c`/`upic_viewer.h` display a 384x256, 16-color
picture using the Upic border-color raster technique -- no bitmap
mode, no sprites, just the VIC-II border color changed at the right
moment on every scanline.

See `CREDITS.md` for attribution: the technique itself is Aleksi
Eeben's; this is a from-scratch Oscar64/C port for the Ultimate 64.

## The technique

With the VIC-II's display enable bit (`DEN`, `$D011` bit 4) held at 0,
the whole visible area becomes "border" -- and the border color
(`$D020`) can be changed by the CPU on every scanline, faster than the
VIC-II can settle into a stable color, producing a visible pixel for
each change. `render_frame()` holds `DEN=0` for the picture's entire
256-row height and, on each scanline, writes one packed picture byte's
low nibble to `$D020` (as a raw byte value, exploiting nibble/color-
index equivalence), then that same byte's high nibble via a lookup
table, producing 2 horizontal pixels per byte per scanline. This is
cycle-exact, hand-tuned 6502 assembly -- `render_line_pixels()` is a
single, fully unrolled sequence (no loop) so its per-pixel timing
never drifts from the raster beam's own position.

`render_frame()` is called once per displayed frame
(`upic_show_frame()`); the whole call is `SEI`-protected in this
project (see `main.c`'s own comment on the global interrupt mask) so
nothing can interrupt the cycle-exact timing mid-scanline.

## Picture buffer: split across two locations

The picture is 384x256 pixels, 2 pixels packed per byte (low nibble =
even column, high nibble = odd column), 192 byte-columns x 256 rows,
stored column-major: byte-column `c`, row `y` is at
`buffer[c*256 + y]`.

The full buffer (98,304 nibbles = 49,152 bytes) doesn't fit in one
contiguous region alongside everything else this project needs, so
it's split:

- **`upic_buffer`** (columns `UPIC_RELOC_COLS`..191, `UPIC_MAIN_BYTES`
  = 47,104 bytes): `$1800`-`$CFFF`. Ordinary RAM, always accessible.
- **`upic_buffer_reloc`** (columns 0..`UPIC_RELOC_COLS`-1,
  `UPIC_RELOC_BYTES` = 2,048 bytes, `UPIC_RELOC_COLS` = 8): `$E000`-
  `$E7FF`. Requires ROM banked out (`rombank_out()`/`MMAP_NO_ROM`) to
  read/write correctly, since it overlaps where the KERNAL ROM would
  otherwise be mapped.

`render_line_pixels()`'s own hardcoded per-column addresses
(`$E000,y`, `$E100,y`, ... for the first 8 columns, then `$1800,y`
onward for the rest) must stay in sync with this split -- both are
generated from the same `UPIC_RELOC_COLS` constant, not independently
maintained.

## Nybble lookup table

`render_line_pixels()` needs each packed byte's HIGH nibble shifted
down to become a plain 0-15 value for the second pixel's border-color
write. Rather than a runtime shift in the hot per-scanline loop,
`nybbles[i] = i >> 4` is a precomputed 256-entry lookup table, built
once (`init_nybbles()`, called lazily on the first `upic_show_frame()`
call) and placed in the otherwise-idle `ovl1` overlay region
(`$0200`-`$0800`) rather than the tight shared code/data/bss pool at
`$E800`-`$FFFF`.

## API

- **`upic_show_frame()`**: call once per frame after the picture buffer
  is filled (or being filled -- generation can call this once per
  column to show live build-up, see `docs/MANDELBROT_ALGORITHM.md`).
  Handles the one-time ROM-bank/nybble-table setup itself, then renders
  exactly one frame. Its return value (whether SPACE is currently held)
  is unused by this project's own control scheme.
- **`upic_restore_display()`**: restores standard `DEN=1` text-mode
  display, default border/background colors, and clears leftover
  KERNAL keyboard-buffer/STOP-key state, for a caller that needs a
  graceful return to BASIC. Not called by this project (see
  `docs/ZOOM_FEATURE.md` for why there's no quit key).

## Memory regions (this project's specific layout)

| Region | Range | Contents |
|---|---|---|
| `startup` | `$0801`-`$0853` | BASIC stub + Oscar64 startup code |
| `main` | `$0853`-`$1800` | Default code/data/bss/heap/stack -- `mandelbrot_generate()`, UCI functions, `main()` itself, `zoom_out_view()` |
| `ovl1` | `$0200`-`$0800` | `nybbles[]` lookup table (permanently, not used as a swappable overlay in this project) |
| `upic_buffer` | `$1800`-`$D000` | Picture buffer, columns 8-191 |
| `upic_buffer_reloc` (`picreloc`) | `$E000`-`$E800` | Picture buffer, columns 0-7 |
| `upiccode` (shared pool) | `$E800`-`$10000` | Most of `zoom.c`, `mandelbrot.c`'s `sq_table`/`cy2_table`, `render_frame()`/`render_line_pixels()` |

The `upiccode` pool is shared code+data+bss (not just code, despite the
name) and is the tightest budget in this project -- see
`docs/ZOOM_FEATURE.md`'s own memory-layout section for its current
usage and the silent-linker-wraparound risk near its `$10000` boundary.

## Testing

No emulator automation exists for this platform -- VICE specifically
doesn't emulate the Ultimate's own UCI/turbo hardware this project
depends on. Manual/visual testing on real Ultimate 64 hardware is the
only way to confirm any change affecting the display.
