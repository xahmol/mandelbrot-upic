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

	// Generate -> let the user pick a zoom target on the completed
	// picture (zoom.c, 2026-09-10) -> generate again at the new
	// bounds -> repeat, until they quit instead of confirming a zoom.
	// zoom_select() replaces the old plain `while(!upic_show_frame());`
	// loop -- it still shows the picture the same way, just with
	// corner-sprite selection UI layered on top; see zoom.h.
	do
	{
		mandelbrot_generate();
	} while (zoom_select());

	upic_restore_display();
	uii_resetpalette();
	turbo_slow();
	rombank_restore();  // clean return to BASIC
	return 0;
}
