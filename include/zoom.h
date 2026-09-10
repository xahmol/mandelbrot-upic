/*****************************************************************
Mandelbrot Upic -- interactive zoom-target selection

Lets the user pick a rectangular sub-region of the CURRENTLY DISPLAYED
picture to zoom into next, using 4 corner-marker hardware sprites
overlaid directly on the live Upic border-flash display -- see
docs (git commit around 2026-09-10) for the research/planning behind
this: why the mirror-and-halve row optimisation had to become
conditional (a zoom target generally won't straddle cy=0 the way the
default overview does), why sprites need to live in the cassette
buffer specifically (confirmed via UBoot64-v2 precedent -- upic_buffer/
upic_buffer_reloc between them cover part of every single VIC bank, so
that's the only genuinely free, non-overlapping spot), and why the
downsampled-preview alternative was dropped once that was found.

STATUS (2026-09-10): implemented, NOT YET HARDWARE-TESTED -- built on
the zoom-feature branch specifically so it can be dropped without
touching main if it doesn't work out. The sprite X/Y calibration
constants in zoom.c are a best-effort first guess (see their own
comment) and are the first thing to check if the corner markers don't
visually line up with the picture.

W/A/S/D moves the active corner, RETURN advances from picking the
first corner to the second (then confirms the zoom), Q quits.
Deliberately NOT the cursor keys (would need shift-for-up/left
handling for no real benefit) and NOT RUN/STOP for quit (documented,
unresolved break-message issue elsewhere in this project).
******************************************************************/

#ifndef _ZOOM_H_
#define _ZOOM_H_

// Shows the just-completed picture (loops upic_show_frame() itself,
// same as the old plain `while(!upic_show_frame());` loop this
// replaces) while overlaying 4 corner sprites the user moves to pick
// a zoom target. On confirm, updates mandel_x0/y0/dx/dy (see
// mandelbrot.h) to the selected sub-rectangle and returns 1 -- caller
// should call mandelbrot_generate() again. Returns 0 if the user quit
// instead (Q) -- caller should tear down and exit, same as the old
// loop's SPACE-to-exit behaviour.
//
// Call after mandelbrot_generate() returns (rombank_out()/turbo_fast()
// must already be active, same preconditions as upic_show_frame()).
unsigned char zoom_select(void);

#pragma compile("zoom.c")

#endif
