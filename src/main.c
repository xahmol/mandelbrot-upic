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

#include "turbo.h"
#include "ultimate_common_lib.h"
#include "upic_viewer.h"
#include "mandelbrot.h"
#include "rombank.h"

int main(void)
{
	rombank_out();  // must run before turbo_set()/uii_detect() -- both live at $E000
	// -- and before mandelbrot_generate(), which writes part of the
	// picture to $E000 too.

	if (uii_wait_for_uci(5))
		uii_setpalette(mandelbrot_palette);

	// Turbo on BEFORE generating, not just before displaying -- the
	// whole point of doing this on-device is the 64x speedup on the
	// escape-time iteration itself, which is by far the slow part.
	turbo_fast();

	mandelbrot_generate();

	while (!upic_show_frame())
		;

	upic_restore_display();
	uii_resetpalette();
	turbo_slow();
	rombank_restore();  // clean return to BASIC
	return 0;
}
