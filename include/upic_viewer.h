/*****************************************************************
Land of Ice and Fire -- Upic picture viewer

Ports Aleksi Eeben's Upic border-color raster technique (see
Source/upic.s in the local reference package and docs/UPIC_VIEWER.md)
into Oscar64. See upic_viewer.c's own doc comment for the deliberate
differences from the original (dropped self-modifying optimization,
PAL-only, one-frame-per-call instead of an infinite loop).

CONFIRMED WORKING ON REAL ULTIMATE 64 HARDWARE (2026-09-09) -- both the
synthetic test pattern and real converted photos (see src/upic_test.c,
src/upic_dragon_test.c, src/upic_iceland_test.c). Full validation
writeup in docs/UPIC_VIEWER.md.

Usage:
    // fill upic_buffer[] (e.g. via UCI file load) with a 49152-byte
    // packed Upic picture -- see tools/upic_convert.py's .upic output
    uii_setpalette(pal48);   // push the picture's palette (SET_PALETTE)
    turbo_fast();
    while (!upic_show_frame())
        ;  // one PAL frame per call; returns nonzero once SPACE seen
    upic_restore_display();
    uii_resetpalette();      // or load the next scene's own .pal
    turbo_slow();

Exit key is SPACE, not RUN/STOP -- deliberate, see upic_show_frame()'s
own doc comment below for why.
******************************************************************/

#ifndef _UPIC_VIEWER_H_
#define _UPIC_VIEWER_H_

#define UPIC_WIDTH  384
#define UPIC_HEIGHT 256
// Must stay a plain literal, NOT a computed expression like
// ((UPIC_WIDTH / 2) * UPIC_HEIGHT) -- confirmed via bisection that a
// computed constant expression used as upic_buffer[]'s array bound
// crashes Oscar64 itself (std::bad_alloc) once combined with this
// file's custom #pragma region/section setup, even though it folds to
// the exact same literal value. Oscar64 v1.32-series bug, not a
// project design choice -- revisit if a future Oscar64 fixes this.
#define UPIC_BYTES  49152

// Picture storage is SPLIT across two physical locations (2026-09-09,
// suggested by Aleksi Eeben, the technique's original author): the
// first UPIC_RELOC_COLS of the picture's 192 byte-columns live at
// $E000 (freed KERNAL ROM, only valid once MMAP_NO_ROM is active, same
// as the render code) instead of at the low end of the old single
// $1000-$CFFF span. This frees low memory (from $1000 up to
// $1000+UPIC_RELOC_BYTES) for ordinary code/data -- the actual
// motivation, since that budget was the project's tightest constraint
// all session. render_line_pixels() in upic_viewer.c has the
// corresponding hardcoded-address split (first UPIC_RELOC_COLS `ldx
// $ssXX,y` operands read $E000 upward, the rest read $1000+
// UPIC_RELOC_BYTES upward unchanged) -- the two must stay in sync if
// the split point changes, along with upic_viewer.c's picreloc/
// upiccode/upicbuf/main region bounds (NOT automatically derived from
// these constants -- grep for 0xe500/0x1500 there when changing this).
//
// COLUMN COUNT DIFFERS BY BUILD, not a single global constant: 16 for
// upictest/upicdragon/upiciceland (no modplay/dos_lib/audio.c linked,
// upiccode's $E000-$FFFF budget has plenty of room), only 5 for
// upicmodplay (modplay's tick path + ultimate_dos_lib.c + audio.c also
// need that same $E000-$FFFF space -- measured directly, 16 columns'
// worth of picreloc left too little room for upiccode's actual
// content, confirmed via the widen-region-and-measure technique
// documented throughout docs/UPIC_VIEWER.md; Oscar64 does NOT error
// when a region overflows this way, it silently spills content into
// unrelated regions, so this was caught by manually inspecting the
// .map, not by a build failure). Since all four targets `#include`
// this same header and upic_viewer.c, UPIC_RELOC_COLS below reflects
// upicmodplay's smaller number -- the simple three targets have
// comfortable spare `main`-region headroom either way, so using the
// smaller, uniform value for all of them (rather than adding
// conditional compilation) was the simpler, lower-risk choice. All
// three literals below must stay plain literals, not computed
// expressions -- see UPIC_BYTES's own note above.
#define UPIC_RELOC_COLS  8      // number of columns relocated to $E000
#define UPIC_RELOC_BYTES 2048   // UPIC_RELOC_COLS * UPIC_HEIGHT
#define UPIC_MAIN_BYTES  47104  // UPIC_BYTES - UPIC_RELOC_BYTES

// Picture buffer, columns UPIC_RELOC_COLS..191 -- fill this (UCI file
// load, or a test pattern) before calling upic_show_frame(), alongside
// upic_buffer_reloc[] below for columns 0..UPIC_RELOC_COLS-1. Fixed at
// $2000-$CFFF (see upic_viewer.c); writes are always safe regardless
// of memory-map banking, only reads (i.e. actually displaying it)
// require MMAP_NO_ROM to be active.
extern volatile char upic_buffer[UPIC_MAIN_BYTES];

// Picture buffer, columns 0..UPIC_RELOC_COLS-1 -- see upic_buffer's own
// doc comment above. Fixed at $E000-$EFFF; UNLIKE upic_buffer, this
// range genuinely does require MMAP_NO_ROM to be active even to WRITE
// to it correctly (it's not just "reads need banking" the way the rest
// of the picture buffer is) -- rombank_out() must have already run
// before filling this array, whereas upic_buffer itself can be filled
// at any time.
extern volatile char upic_buffer_reloc[UPIC_RELOC_BYTES];

// Render exactly one PAL frame (256 lines) from upic_buffer, then poll
// the keyboard once. Banks ROM out (MMAP_NO_ROM) via mmap_trampoline()
// the first time this is called, and leaves it out for the whole
// picture-viewing session (not restored every frame) -- see
// upic_restore_display() for putting it back. Does NOT touch turbo
// speed or the palette -- call turbo_fast()/uii_setpalette() yourself
// first. Returns 1 once SPACE is pressed (caller should stop looping),
// 0 otherwise. Call in a loop to keep displaying the picture.
//
// SPACE, not RUN/STOP: RUN/STOP was tried first and confirmed working
// for the render/exit mechanics themselves, but real-hardware testing
// found it triggers a KERNAL-level "BREAK IN <line>" on exit that
// survived every targeted fix attempted (keyboard buffer clear, atomic
// restore sequencing, waiting for physical key release, resetting
// STKEY $91) -- something about RUN/STOP's own extra KERNAL handling
// beyond those wasn't identified. Switched to SPACE, confirmed clean
// (no break, no leftover character) on real hardware. See
// upic_restore_display()'s doc comment for the full diagnostic story.
char upic_show_frame(void);

// Re-enable the normal VIC-II display (DEN=1, standard 25-row text/
// bitmap timing) after a picture session ends. upic_show_frame() clears
// DEN every call so the border-color trick covers the whole screen, not
// just the physical border strip -- nothing else turns it back on, so
// without this the screen stays a static, unchanging $D020 color forever
// (confirmed on real hardware: looks exactly like a hang, even though
// the CPU has actually returned to the caller/BASIC just fine). Call
// once after the upic_show_frame() loop exits, before showing anything
// else on screen.
void upic_restore_display(void);

#pragma compile("upic_viewer.c")

#endif
