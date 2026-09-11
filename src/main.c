/*****************************************************************
Mandelbrot Upic -- entry point

Ported from landoficeandfire's proven upic_test.c harness: bank ROM
out, push the palette, generate the fractal (now at 64 MHz turbo, see
below), display it via the Upic border-color raster loop and let the
user browse/zoom forever (zoom.c) -- there is no exit; see zoom.h's own
comment for why.

Requires Ultimate 64 / U64 Elite 2, firmware 3.15+.
******************************************************************/

#include <c64/cia.h>
#include "turbo.h"
#include "ultimate_common_lib.h"
#include "upic_viewer.h"
#include "mandelbrot.h"
#include "rombank.h"
#include "zoom.h"

// uii_setpalette() confirmed rejected ("81,INVALID P...") right after
// a fresh device reboot (2026-09-09), on final-release firmware 3.15
// where this exact call is otherwise known-working (landoficeandfire's
// own iceland_palette) -- working theory: some UCI subsystem beyond
// basic detection isn't fully up yet immediately after boot, even
// though uii_detect() itself already succeeds. Retry a few times with
// a short settle delay rather than accept the first result -- CIA1
// TOD tenths, same timing primitive uii_wait_for_uci() already uses.
static void setpalette_retry(const char *rgb48)
{
	unsigned char attempt;
	for (attempt = 0; attempt < 10; attempt++)
	{
		uii_setpalette(rgb48);
		if (UII_SUCCESS)
			return;

		cia1.todt = 0;
		while (cia1.todt < 2)  // ~0.2s
			;
	}
}

int main(void)
{
	unsigned char uci_ready;

	rombank_out();  // must run before turbo_set()/uii_detect() -- both live at $E000
	// -- and before mandelbrot_generate(), which writes part of the
	// picture to $E000 too.

	// Interrupts masked globally here, for the rest of the program's
	// entire lifetime, and never re-enabled (2026-09-11) -- root-caused
	// a real-hardware "any key press drops to a JiffyDOS text screen"
	// crash (confirmed present even in the completely unmodified,
	// pre-zoom-feature baseline, so it predates the interactive zoom
	// work entirely) to mmap_trampoline() (rombank.c) chaining a
	// same-tick hardware interrupt into real KERNAL/JiffyDOS ROM code
	// while this program's own direct-CIA keyboard polling is active --
	// confirmed by permanently masking IRQ here, which eliminated the
	// crash entirely on real hardware (tested extensively: C key, Q key,
	// no more drops to text mode). This program never genuinely needs a
	// real interrupt for anything -- no music, no raster-IRQ effects,
	// every wait loop in this codebase (render_frame()'s own raster
	// sync included) is plain busy-polled -- so permanently masking IRQ
	// costs nothing functionally. NMI (RESTORE key) still isn't masked
	// by this (SEI can't touch it) but isn't part of this bug family.
	__asm { sei }

	uci_ready = uii_wait_for_uci(5);

	// Palette pushed before generation -- mandelbrot_generate() shows
	// the picture LIVE as it builds (see its own comment), which needs
	// the fractal's own palette active from the start to look right.
	// A plain-text welcome/progress screen was tried in between (see
	// git history) but removed once live rendering made it redundant
	// -- the user explicitly preferred watching the picture build over
	// a progress bar, flicker and all.
	if (uci_ready)
		setpalette_retry(mandelbrot_palette);

	// Turbo on BEFORE generating, not just before displaying -- the
	// whole point of doing this on-device is the 64x speedup on the
	// escape-time iteration itself, which is by far the slow part.
	turbo_fast();

	mandelbrot_generate();

	// Let the user pick a zoom target on the completed picture
	// (zoom.c) -> generate again at the new bounds -> repeat, forever
	// -- there is no way out of this loop, see zoom.h's own comment on
	// why there's no quit key. uii_setpalette() for a 'C' press is
	// pushed HERE, from main()'s own context, not from inside
	// zoom_select() itself -- see zoom_pending_palette's own comment
	// in zoom.h for why.
	for (;;)
	{
		unsigned char zr = zoom_select();

		if (zr == ZOOM_PALETTE_CHANGED)
		{
			if (uci_ready)
				uii_setpalette(zoom_pending_palette);
			continue;  // same view, no regenerate -- straight back to zoom_select()
		}

		// ZOOM_CONFIRMED
		mandelbrot_generate();
	}
}
