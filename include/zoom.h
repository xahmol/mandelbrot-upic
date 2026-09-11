/*****************************************************************
Mandelbrot Upic -- interactive zoom-target selection

Lets the user pick a rectangular sub-region of the CURRENTLY DISPLAYED
picture to zoom into next. Rewritten from scratch (2026-09-11) on top
of a confirmed-stable baseline (see main.c's own comment on the global
IRQ mask) after an earlier attempt at this same feature ran into a
real-hardware "any key press drops to text mode" crash that turned out
to predate the whole feature -- see git history (zoom-feature-broken
branch) for that attempt's own preserved work and partial findings.

Two lessons carried forward from that attempt, deliberately baked into
this version instead of rediscovering them:
  - Corner markers are drawn DIRECTLY INTO THE PACKED PICTURE BUFFER,
    not VIC-II hardware sprites -- confirmed via two isolated,
    controlled standalone tests (built and run on real hardware) that
    sprites cannot be composited at all while this project's DEN=0
    border-racing display technique is active. See zoom.c's own
    comment (above zoom_pixel_addr()) for the full writeup.
  - ZOOM_MARKER_COLOR_INDEX (below) is a raw color index reserved
    exclusively for these markers, never assigned to any picture pixel
    -- mandel_color() (mandelbrot.c) only ever emits 1-14, and every
    selectable palette hardcodes its own 16th RGB entry to white at
    compile time.

STATUS (2026-09-11): keyboard-only rebuild in progress. Two-mode
design -- "browse" (default after generation: WASD/cursor pans the
current view) and "box" (entered via 'Z': all 4 corner markers appear
-- each a 4x4 block, a 1-pixel black outline around a 2x2 white core,
upgraded from an earlier 2-marker/single-pixel design once real memory
headroom turned up -- see zoom.c's own comment above zoom_marker_draw()),
WASD/cursor moves the whole box, '+'/'-' resize it with the picture's
own 3:2 aspect ratio always locked, RETURN confirms and zooms in, 'Z'
again cancels back to browse without zooming). See zoom_select()'s own
comment in zoom.c for the full per-key breakdown.

Deliberately NOT included yet, to get the rest onto a solid, committed
footing first:
  - Joystick input (keyboard only for now).
  - 'O' zoom-out (didn't get this working in the previous attempt;
    revisit once everything else here is confirmed stable).
  - Any quit key -- Q used to tear down and return to BASIC in both
    the original 2-corner design and the previous attempt's redesign,
    but the exit sequence hung on real hardware in both, consistent
    with this project's own documented, unresolved KERNAL-IRQ-vs-ROM-
    banking bug family (predating even today's global-IRQ-mask fix --
    that fix covers the picture-viewing loop, not the exit sequence's
    own ROM-bank restore). Not fixed; there's simply no way to quit
    now, same as many C64 demos with no graceful exit path.
******************************************************************/

#ifndef _ZOOM_H_
#define _ZOOM_H_

#define ZOOM_CONFIRMED       1
#define ZOOM_PALETTE_CHANGED 2

// Color index reserved EXCLUSIVELY for the corner markers, never
// assigned to any picture pixel -- see this header's own opening
// comment and mandelbrot.c's own mandel_color()/palette comments.
#define ZOOM_MARKER_COLOR_INDEX 15

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
