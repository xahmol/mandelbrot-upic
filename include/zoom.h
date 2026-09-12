/*****************************************************************
Mandelbrot Upic -- interactive zoom-target selection

Lets the user pick a rectangular sub-region of the CURRENTLY DISPLAYED
picture to zoom into next, rebuilt on top of a confirmed-stable
baseline (see main.c's own comment on the global IRQ mask) that fixed
a real-hardware "any key press drops to text mode" crash predating
this feature entirely.

Two lessons worth keeping in mind, deliberately baked into
this version instead of rediscovering them:
  - Corner markers are drawn DIRECTLY INTO THE PACKED PICTURE BUFFER,
    not VIC-II hardware sprites -- confirmed via two isolated,
    controlled standalone tests (built and run on real hardware) that
    sprites cannot be composited at all while this project's DEN=0
    border-racing display technique is active. See zoom.c's own
    comment (above zoom_pixel_addr()) for the full writeup.
  - ZOOM_MARKER_COLOR_INDEX (below) USED TO BE reserved exclusively
    for these markers -- mandel_color() (mandelbrot.c) capped its own
    gradient at color 14, leaving 15 (every selectable palette's own
    16th RGB entry, hardcoded to white at compile time) genuinely
    unused by any picture pixel. That changed 2026-09-12: the gradient
    now uses white too (DDT/0x444454's own approach, see
    mandel_color_table[]'s own comment for the full writeup and why),
    so a marker CAN now land on a picture pixel of its own exact color
    and be hard to spot there. A 1px-outlined marker would close that
    gap (see zoom.c's own comment above zoom_marker_draw() -- a version
    like that was already built and confirmed working on real hardware
    once) but there isn't shared-pool memory to bring it back right
    now; accepted as a real, known tradeoff instead. (White itself also
    moved, same day: rather than sitting at the very end of each
    gradient right where it borders true black, it's now each
    palette's own mid-gradient brightest point, at the SAME index --
    8 -- in all four, which is why ZOOM_MARKER_COLOR_INDEX can still be
    one constant instead of varying per palette.)

Two-mode design -- "browse" (default after generation: WASD/cursor
pans the current view, 'O' zooms out one notch, clamped to the default
overview) and "box" (entered via 'Z': all 4 corner markers appear --
each a solid 2x2 white block, see zoom.c's own comment above
zoom_marker_draw() for this design's own history) -- WASD/cursor moves
the whole box, '+'/'-' resize it with the picture's own 3:2 aspect
ratio always locked, RETURN confirms and zooms in, 'Z' again cancels
back to browse without zooming. See zoom_select()'s own comment in
zoom.c for the full per-key breakdown, and docs/ZOOM_FEATURE.md for the
full manual.

Keyboard only -- no joystick input, and no quit key (the exit sequence
hangs on real hardware for reasons in the same general bug family as
the interrupt-related issue fixed in main.c, but not fixed by it;
there's simply no way to quit, same as many C64 demos with no graceful
exit path).
******************************************************************/

#ifndef _ZOOM_H_
#define _ZOOM_H_

#define ZOOM_CONFIRMED       1
#define ZOOM_PALETTE_CHANGED 2

// Color index used for the corner markers -- no longer guaranteed
// absent from the picture itself (mandel_color() can emit it too,
// 2026-09-12 onward), see this header's own opening comment and
// mandelbrot.c's own mandel_color()/palette comments. Index 8 is
// every selectable palette's own shared mid-gradient white peak, not
// index 15 -- deliberately the same index in all four so this can
// stay one constant.
#define ZOOM_MARKER_COLOR_INDEX 8

// Runs browse mode and (once 'Z' is pressed) box mode, looping forever
// -- there's no way out of this call other than one of the ZOOM_*
// returns below (no quit; see this header's own opening comment).
// Returns one of the ZOOM_* values above:
//   ZOOM_CONFIRMED       -- mandel_x0/y0/dx/dy (mandelbrot.h) updated
//                           to the newly selected view (a box-mode
//                           confirm or a browse-mode pan); caller
//                           should call mandelbrot_generate() again.
//   ZOOM_PALETTE_CHANGED -- user pressed 'C'; zoom_pending_palette
//                           (below) names the newly-selected gradient.
//                           Caller must push it via uii_setpalette()
//                           ITSELF, then call zoom_select() again --
//                           see zoom_pending_palette's own comment for
//                           why this isn't done from inside
//                           zoom_select() itself. Current box-mode
//                           state (if any) is preserved across this
//                           round-trip -- resumes exactly where it
//                           left off, no fractal recomputation.
//
// Call after mandelbrot_generate() returns (rombank_out()/turbo_fast()
// must already be active, same preconditions as upic_show_frame()).
unsigned char zoom_select(void);

// Valid only immediately after zoom_select() returns
// ZOOM_PALETTE_CHANGED -- the palette the caller should push.
//
// Deliberately NOT pushed directly from inside zoom_select() itself:
// zoom_select() is placed in upiccode (see its own #pragma code), and
// calling uii_setpalette() from there goes through a deep call chain
// (uii_setpalette -> uii_settarget/uii_sendcommand/uii_readdata/
// uii_readstatus/uii_accept, all in "main") crossing back into a
// different pragma-code region -- the previous attempt at this feature
// found this specific cross-region call risky on real hardware (see
// git history), so this rebuild avoids it from the start rather than
// rediscovering why. Returning to main() (which shares "main" with the
// whole uii_setpalette() call chain) to push it instead sidesteps the
// cross-region call entirely.
extern const char *zoom_pending_palette;

#pragma compile("zoom.c")

#endif
