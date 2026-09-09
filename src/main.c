/*****************************************************************
Mandelbrot Upic -- entry point

Ported from landoficeandfire's proven upic_test.c harness: bank ROM
out, push the palette, generate the fractal (now at 64 MHz turbo, see
below), display it via the Upic border-color raster loop until SPACE,
restore.

Phase 1 (2026-09-09): the fractal generator (include/mandelbrot.c) is
a real fixed-point escape-time Mandelbrot now, not the earlier
diagonal-pattern placeholder -- see docs/MANDELBROT_ALGORITHM.md for
the design and what's still Phase 2/3 (histogram-optimised palette,
cardioid/period-2-bulb early-skip).

Requires Ultimate 64 / U64 Elite 2, firmware 3.15+.
******************************************************************/

#include <c64/cia.h>
#include "turbo.h"
#include "ultimate_common_lib.h"
#include "upic_viewer.h"
#include "mandelbrot.h"
#include "rombank.h"
#include "progress.h"

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

	// First thing, before anything else -- plain screen/color-RAM POKEs,
	// no KERNAL calls, so nothing else needs to happen first (see
	// progress.h). Gives instant visual feedback instead of a frozen
	// BASIC-ready screen during rombank_out()/UCI detection/palette
	// retries, which can themselves take a couple of seconds.
	progress_init();

	rombank_out();  // must run before turbo_set()/uii_detect() -- both live at $E000
	// -- and before mandelbrot_generate(), which writes part of the
	// picture to $E000 too.

	uci_ready = uii_wait_for_uci(5);

	// Turbo on BEFORE generating, not just before displaying -- the
	// whole point of doing this on-device is the 64x speedup on the
	// escape-time iteration itself, which is by far the slow part.
	turbo_fast();

	mandelbrot_generate();

	// Palette pushed only now, after generation -- SET_PALETTE rewrites
	// the actual RGB behind indices 0-15, which VIC-II's border/
	// background/text colors read from too. Pushing it before
	// generation (as this used to) recolored progress_init()'s welcome/
	// progress screen with the fractal's custom blue->orange gradient
	// instead of normal colors for the whole ~12s generation now takes.
	// uii_resetpalette() (below) already proved UCI calls work fine
	// with turbo left on, so no need to touch turbo state around this.
	if (uci_ready)
		setpalette_retry(mandelbrot_palette);

	while (!upic_show_frame())
		;

	upic_restore_display();
	uii_resetpalette();
	turbo_slow();
	rombank_restore();  // clean return to BASIC
	return 0;
}
