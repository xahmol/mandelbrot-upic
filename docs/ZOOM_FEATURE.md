# Interactive zoom feature

How `include/zoom.c`/`zoom.h` let the user pick a rectangular
sub-region of the currently displayed picture and zoom into it.

## Two modes

**Browse mode** (the default, entered after every generation):
`W`/`A`/`S`/`D` or the cursor keys pan the current view -- shifts the
view by a fraction of its own current width/height at the same zoom
level, proportional to the current zoom depth (a deep zoom's step is
small in absolute terms, the default overview's is large). No-op at
the default overview (nothing to pan to). `O` zooms out one notch,
widening the view by 2x around its own current centre -- not jumping
straight back to the default overview -- clamped so it can never widen
past the original default view. `C` cycles the base color gradient.
`Z` enters box mode.

**Box mode** (entered via `Z`): 4 corner markers appear (a solid 2x2
white block per corner), outlining a box that always keeps the
picture's own 384:256 (3:2) aspect ratio. `W`/`A`/`S`/`D` or the cursor
keys move the whole box (position only; the box always keeps its own
aspect ratio, so there's nothing to distort). `+` grows the box, `-`
shrinks it (both keep the 3:2 ratio -- box size is tracked in "units":
1 unit = 6 cells wide / 4 cells tall, both exact factors of 384:256's
own ratio, so box dimensions are always exact integers with no
runtime division needed). `RETURN` confirms and zooms into the box.
`Z` again cancels back to browse mode without zooming. `C`/`O` work
the same as in browse mode.

## Controls summary

| Input | Browse mode | Box mode |
|---|---|---|
| `W`/`A`/`S`/`D` | Pan the view | Move the box |
| Cursor keys (hold Shift for up/left) | Same, alternative for muscle memory | Same |
| `Z` | Enter box mode | Cancel back to browse (no zoom) |
| `O` | Zoom out one notch | Same |
| `C` | Cycle palette | Same |
| `+` / `-` | -- | Grow / shrink the box |
| `RETURN` | -- | Confirm: zoom into the box |

Cursor-key handling exploits a real hardware property of the C64
keyboard matrix: it only has physical DOWN/RIGHT cursor positions.
UP/LEFT are the SAME two matrix positions read with Shift held (true
in hardware, not a software convention), so unshifted cursor-right/
down move right/down and Shift+cursor-right/down move left/up.

There is no joystick input and no quit key. The exit sequence (restore
text screen/palette, return to BASIC) hangs on real hardware for
reasons distinct from -- but in the same general bug family as -- the
interrupt-related issue fixed in `main.c` (see its own comment on the
global IRQ mask); there's simply no way to quit, same as many C64
demos with no graceful exit path.

## Corner markers: buffer-drawn, not sprites

Each corner marker is a 2x2 solid white block written directly into
the packed picture buffer (`upic_buffer`/`upic_buffer_reloc`), not a
VIC-II hardware sprite. Hardware sprites cannot be composited at all
while this project's Upic border-racing display technique holds
`DEN=0` across the whole frame (confirmed via isolated standalone
tests on real hardware: sprites work fine on a plain `DEN=1` text
screen, and are invisible under the exact same setup once the border-
racing technique is active, despite every sprite register reading back
correct). This differs from the commonly-documented "sprites work fine
in open-border tricks" technique, which describes briefly toggling
`RSEL` per raster line to prevent the border flip-flop from latching,
not holding `DEN` low across an entire multi-frame session.

`zoom_pixel_addr()` computes a pixel's address in the packed buffer
(mirroring `mandelbrot_generate()`'s own split between
`upic_buffer_reloc` for the first `UPIC_RELOC_COLS` byte-columns and
`upic_buffer` for the rest); `zoom_get_pixel()`/`zoom_set_pixel()`
read/write one pixel through the even-column-low-nibble/odd-column-
high-nibble packing. Each marker's 4 covered pixels are backed up
before being overwritten and restored before the marker moves again,
so it never leaves a permanent mark on the picture.

Raw color 15 is reserved exclusively for markers -- `mandel_color()`
(`mandelbrot.c`) only ever emits 1-14 for escaping pixels, and every
selectable palette hardcodes its own 16th RGB entry to white at
compile time, so pushing any palette already reserves the slot with no
extra runtime call needed.

## Confirming a zoom

On `RETURN`, the box's current position and size (in picture cells)
map onto the CURRENT view's own `mandel_x0`/`y0`/`dx`/`dy` (not the
original default view), so repeated zooms compose correctly -- each
zoom is always relative to what's currently displayed. The computed
new per-pixel step is floored at 1 raw Q5.11 unit rather than allowed
to reach 0, which would sample every column/row at the same coordinate
(a degenerate, solid-color result) -- see
`docs/MANDELBROT_ALGORITHM.md`'s precision-limit note.

The division needed for this (box width/height times the current
step, divided by the picture's own pixel width/height) uses a hand-
written 16-bit shift-subtract divider (`zoom_udiv16()`) rather than
Oscar64's generic C library divider, which is deliberately never
linked into this project otherwise (see its own comment) and would
cost more code size than a small hand-written one for this single use.

## Palette push happens in `main()`, not here

Pressing `C` returns `ZOOM_PALETTE_CHANGED` from `zoom_select()` rather
than pushing the palette directly -- the caller (`main()`) pushes it
via `uii_setpalette()` from its own context, then calls
`zoom_select()` again. `zoom_select()` itself lives in the `upiccode`
code region (see [Memory layout](#memory-layout) below); calling
`uii_setpalette()` directly from there would cross into a different
code region through a deep call chain, which this project treats as a
real risk to avoid rather than a merely theoretical one -- see
`zoom_pending_palette`'s own comment in `zoom.h`.

## Interrupts

This whole feature runs under the permanent, whole-program interrupt
mask set once in `main()` (see its own comment) -- nothing here relies
on any interrupt being enabled. The one place this file still touches
interrupts directly is wrapping `keyb_poll()`'s own multi-step CIA1
keyboard-matrix scan in `SEI`/`CLI`, matching the same protection
`upic_show_frame()` (`upic_viewer.c`) applies to its own internal
`keyb_poll()` call.

## Memory layout

`zoom_select()` and most of this file's functions live in `upiccode`,
a shared code/data/bss pool at `$E800-$FFFF` also used by
`mandelbrot.c`'s `sq_table`/`cy2_table` and this file's own marker
backup storage. This pool is tight -- `zoom_out_view()` specifically
is placed in `main` (the default code region) instead, since `upiccode`
had no room left for it; it duplicates a small bounds-check inline
rather than calling the equivalent helper still in `upiccode`, since a
single large function crossing regions nets a real size win while a
small one calling back across regions does not (measured, not assumed
-- Oscar64's cross-region call overhead can exceed what moving a small
function saves).

Corner markers are 4 markers x 4 bytes of backup storage each (a 2x2
block) -- the largest marker size and count this pool currently has
room for alongside `O` (zoom-out). A bigger/outlined marker, and
joystick input, are both natural additions if more room turns up
elsewhere in this pool.

Always verify actual object placement via the build's own `.map` file
after changing anything in this pool -- Oscar64's linker can, in rare
cases, silently wrap an object's address past `$10000` back down near
`$0000` instead of raising a placement error, corrupting zero page
without any visible build failure. A clean build alone is not
sufficient evidence of correct placement this close to the boundary.
