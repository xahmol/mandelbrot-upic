/*****************************************************************
Mandelbrot Upic -- fractal generator (implementation)
See mandelbrot.h for API documentation and current status.
******************************************************************/

#include "mandelbrot.h"

// PLACEHOLDER pattern (2026-09-09): same diagonal color-bar test used
// by landoficeandfire's src/upic_test.c to first validate its viewer --
// exercises every nibble value across the whole buffer split, nothing
// more. Split across upic_buffer_reloc[] (columns 0..UPIC_RELOC_COLS-1)
// and upic_buffer[] (the rest) -- see upic_viewer.h's own doc comment
// for why. Caller must run this AFTER rombank_out(): unlike
// upic_buffer, upic_buffer_reloc genuinely requires MMAP_NO_ROM active
// even to write to it correctly.
void mandelbrot_generate(void)
{
	unsigned xpair, y;
	for (xpair = 0; xpair < UPIC_RELOC_COLS; xpair++)
	{
		for (y = 0; y < UPIC_HEIGHT; y++)
		{
			unsigned char even = (unsigned char)(xpair & 0x0F);
			unsigned char odd = (unsigned char)((xpair + y) & 0x0F);
			upic_buffer_reloc[(unsigned)xpair * UPIC_HEIGHT + y] = (odd << 4) | even;
		}
	}
	for (xpair = UPIC_RELOC_COLS; xpair < UPIC_WIDTH / 2; xpair++)
	{
		for (y = 0; y < UPIC_HEIGHT; y++)
		{
			unsigned char even = (unsigned char)(xpair & 0x0F);
			unsigned char odd = (unsigned char)((xpair + y) & 0x0F);
			upic_buffer[(unsigned)(xpair - UPIC_RELOC_COLS) * UPIC_HEIGHT + y] = (odd << 4) | even;
		}
	}
}
