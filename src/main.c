/*****************************************************************
Mandelbrot Upic -- entry point

PLACEHOLDER (2026-09-09): proves the buildchain end to end (Upic viewer
+ palette push + render loop, all ported from landoficeandfire's proven
upic_test.c) with a synthetic test pattern in place of the real
fractal. See mandelbrot.h/docs/MANDELBROT_ALGORITHM.md for the actual
generator, not implemented yet.

Requires Ultimate 64 / U64 Elite 2, firmware 3.15+.
******************************************************************/

#include "turbo.h"
#include "ultimate_common_lib.h"
#include "upic_viewer.h"
#include "mandelbrot.h"
#include "rombank.h"

// Placeholder 16-color test palette -- the standard VIC-II palette.
// Stands in for a real generated/optimised palette (see
// docs/MANDELBROT_ALGORITHM.md's "Palette selection" section) until
// the real generator exists.
static const char test_palette[48] = {
	0x00,0x00,0x00,  0xFF,0xFF,0xFF,  0x68,0x37,0x2B,  0x70,0xA4,0xB2,
	0x6F,0x3D,0x86,  0x58,0x8D,0x43,  0x35,0x28,0x79,  0xB8,0xC7,0x6F,
	0x6F,0x4F,0x25,  0x43,0x39,0x00,  0x9A,0x67,0x59,  0x44,0x44,0x44,
	0x6C,0x6C,0x6C,  0x9A,0xD2,0x84,  0x6C,0x5E,0xB5,  0x95,0x95,0x95,
};

int main(void)
{
	rombank_out();  // must run before turbo_set()/uii_detect() -- both live at $E000
	// -- and before mandelbrot_generate(), which writes part of the
	// picture to $E000 too.

	mandelbrot_generate();

	if (uii_wait_for_uci(5))
		uii_setpalette(test_palette);

	turbo_fast();

	while (!upic_show_frame())
		;

	upic_restore_display();
	uii_resetpalette();
	turbo_slow();
	rombank_restore();  // clean return to BASIC
	return 0;
}
