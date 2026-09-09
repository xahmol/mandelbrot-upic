/*****************************************************************
Mandelbrot Upic -- fractal generator (interface)

PLACEHOLDER (2026-09-09): scaffolding only, not the real algorithm yet.
See docs/MANDELBROT_ALGORITHM.md for the full design plan (Q-format
fixed-point iteration, squaring table, direct-to-Upic packing, palette
selection) -- this header/mandelbrot.c exist right now purely to prove
the buildchain end-to-end with something real to compile and run.

Credit: the fixed-point algorithm design this project will implement is
based on the documented approach in 0x444454/mandelbr8
(https://github.com/0x444454/mandelbr8, CC BY 4.0) -- a from-scratch C
reimplementation for Oscar64, not a port of their 6502 assembly. See
CREDITS.md.
******************************************************************/

#ifndef _MANDELBROT_H_
#define _MANDELBROT_H_

#include "upic_viewer.h"

// mandelbrot_generate -- fill upic_buffer[]/upic_buffer_reloc[] (see
// upic_viewer.h) with a rendered Mandelbrot set, packed directly into
// Upic's nibble-packed column-major format.
//
// PLACEHOLDER: currently fills a synthetic diagonal test pattern (same
// shape as landoficeandfire's src/upic_test.c) instead of a real
// fractal, purely to exercise the picture buffer + viewer end to end.
// Real implementation: see docs/MANDELBROT_ALGORITHM.md.
void mandelbrot_generate(void);

#pragma compile("mandelbrot.c")

#endif
