/*****************************************************************
Land of Ice and Fire -- Upic picture viewer implementation

Based on Aleksi Eeben's Upic v1.1 border-color raster technique
(aleksi.eeben@me.com, Source/upic.s in the local reference package,
https://csdb.dk/release/?id=263980) -- see docs/UPIC_VIEWER.md for
the full technical background this port is based on.

Deliberate differences from the original (see docs/UPIC_VIEWER.md for
the reasoning behind each):
  - The self-modifying "8th pixel pair" optimization and its PatchLine
    routine are dropped. They saved ~2 cycles per 16-pixel chunk (a
    few dozen cycles total per line) at the cost of self-modifying
    code and hardcoded absolute addresses tied to the original's own
    fixed $E000 placement -- not worth the risk/complexity here, and
    barely affects the frame's cycle budget.
  - The per-line pixel rendering is its own subroutine (render_line_
    pixels), called via jsr from render_frame's line loop -- inlining
    it directly (an earlier draft of this file) put the loop-back
    branch (bne line) more than 127 bytes from its target, which
    6502 relative branches cannot reach; factoring it out mirrors how
    the original's own Frame/RenderLine split works for the same
    underlying reason (Frame's `jsr RenderLine` keeps its own loop-back
    branch short regardless of how big RenderLine itself is).
  - PAL only -- no NTSC row-count crop yet (see upic.s's `ntsc_` patch
    for what that would need).
  - Renders exactly one frame per call (upic_show_frame()) instead of
    an infinite loop, so the caller can poll for the exit key between
    frames without needing to interrupt cycle-exact code mid-line.

CONFIRMED WORKING ON REAL ULTIMATE 64 HARDWARE (2026-09-09) -- both the
synthetic test pattern and real converted photos. Full validation
writeup, including the 4 real render/exit bugs found and fixed via
live hardware memory read/write (no emulator automation exists for
this project -- see CLAUDE.md's Testing section, and see
docs/UPIC_VIEWER.md for why VICE specifically can't validate this
module), is in docs/UPIC_VIEWER.md's "Real-hardware validation" section.
******************************************************************/

#include <c64/keyboard.h>
#include "rombank.h"
#include "upic_viewer.h"

// ---------------------------------------------------------------
// Picture buffer, split across two physical locations (2026-09-09,
// Aleksi Eeben's own suggestion -- see upic_viewer.h's doc comment for
// the full rationale and the render_line_pixels() addressing this must
// stay in sync with).
//
// upic_buffer (columns UPIC_RELOC_COLS..191): $1000+UPIC_RELOC_BYTES
// through $CFFF (UPIC_MAIN_BYTES bytes -- see upic_viewer.h; currently
// $1500-$CFFF, 47,872 bytes, for UPIC_RELOC_COLS=5). $1000-$9FFF is
// always plain RAM; $A000-$CFFF only reads correctly once MMAP_NO_ROM
// is active (see upic_show_frame()) -- but *writes* always land in
// the underlying RAM regardless of banking, so filling this part of
// the buffer (e.g. via a UCI file load) works at any time, banked or
// not.
//
// upic_buffer_reloc (columns 0..UPIC_RELOC_COLS-1): $E000 through
// $E000+UPIC_RELOC_BYTES (currently $E000-$E4FF, 1,280 bytes) -- see
// its own region declaration further down, alongside upiccode, since
// it shares that $E000-$FFFF space and (unlike upic_buffer above)
// genuinely does need MMAP_NO_ROM active even to write to it
// correctly.
//
// This overlaps Oscar64's own default "main" region ($0a00-$a000), so
// that region is explicitly shrunk below ($0a00-$1000, then extended
// back up to $1000+UPIC_RELOC_BYTES now that the relocation frees that
// range -- see the #pragma region(main, ...) near the end of this
// file, and upic_viewer.h's own comment on why UPIC_RELOC_COLS is a
// single value shared by all four targets rather than per-target.
// ---------------------------------------------------------------
#pragma section(upicbuf, 0)
#pragma region(upicbuf, 0x1800, 0xd000, , , {upicbuf})
#ifdef UPIC_EMBED_DRAGON
// Test-only: bake a real reference picture in at compile time instead of
// leaving upic_buffer uninitialized bss, so real hardware can be checked
// against actual photographic content, not just the synthetic diagonal
// test pattern. include/dragon3.upic is Aleksi Eeben's own reference
// sample (see docs/UPIC_VIEWER.md), copied in from the local upic
// package -- not the real demo's picture-loading path (that's still UCI
// file I/O, not done yet). Only enabled by the upicdragon Makefile
// target's -dUPIC_EMBED_DRAGON; the normal upic_buffer stays plain bss
// (the real API contract: caller fills it, e.g. via UCI file load).
//
// __export is required here, not decorative: upic_buffer is never
// accessed through this C array symbol at all -- render_line_pixels()'s
// __asm block reads it via hardcoded literal addresses ($2000, $2100,
// ...), invisible to Oscar64's dataflow tracing. Without __export, the
// optimizer sees an "unreferenced" initialized array and silently drops
// the embedded content entirely (confirmed: region ended up 0 bytes in
// the .map, and the dragon3.upic byte signature was nowhere in the
// compiled .prg at all, even though the build succeeded with no warning).
//
// #embed's offset+length form slices the same 49,152-byte .upic file
// (column-major, so the first UPIC_RELOC_BYTES bytes ARE exactly
// columns 0..UPIC_RELOC_COLS-1) into the two physical arrays -- see
// upic_buffer_reloc's own #embed further down for the first slice.
#pragma data(upicbuf)
__export volatile char upic_buffer[UPIC_MAIN_BYTES] = {
    #embed 47104 2048 "dragon3.upic"
};
#pragma data(data)
#elif defined(UPIC_EMBED_ICELAND)
// Same mechanism as UPIC_EMBED_DRAGON above, but with a real converted
// project photo (tools/upic_convert.py's output) instead of the
// reference sample -- first end-to-end test of the actual conversion
// pipeline, not just the viewer. See src/upic_iceland_test.c.
#pragma data(upicbuf)
__export volatile char upic_buffer[UPIC_MAIN_BYTES] = {
    #embed 47104 2048 "diamond_beach.upic"
};
#pragma data(data)
#else
#pragma bss(upicbuf)
volatile char upic_buffer[UPIC_MAIN_BYTES];
#pragma bss(bss)
#endif

// Nybble shift table: nybbles[i] = i >> 4 -- looks up the "odd" pixel's
// color from a packed byte (high nibble) without a runtime shift in
// the hot loop. Built once (init_nybbles(), called lazily from
// upic_show_frame()), not embedded as a literal 256-entry table.
//
// Placed in ovl1's window ($0200-$0800, bssovl1 -- see the #pragma
// region(ovl1, ...) near the end of this file), NOT modbss
// ($E800-territory, upiccode's own tight budget -- confirmed too full
// once ultimate_dos_lib.c's uii_open_file/uii_load_reu/uii_close_file,
// which live in modcode by that file's own #pragma placement, are
// counted; 53 bytes short, 2026-09-09) and NOT modlowbss (a PLAIN
// #pragma region below $0801, confirmed to corrupt the whole .prg's
// load-address header when holding any real content -- see modlowbss's
// own now-empty declaration below). ovl1 itself is a genuinely idle
// resource right now: modplay_load() (its only real function user) was
// removed entirely (see modplay.h), so nothing else contends for this
// window across this build's whole lifetime -- safe to give it to
// nybbles permanently instead of using it as an actual swappable
// overlay. Confirmed safe via the SAME #pragma overlay mechanism that
// keeps ovl1/ovl2's real code safe from the modlowbss-style header
// corruption (an #pragma overlay region, unlike a plain one, doesn't
// trigger it) -- verified via `xxd -l2` after this exact change, not
// assumed.
//
// Referenced from render_line_pixels() via the plain symbol name
// `nybbles`, NOT a hardcoded literal address the way the picture-buffer
// columns are, so the linker resolves it correctly wherever it actually
// lives -- moving it again in the future doesn't require touching
// render_line_pixels() at all. __align(256) is required, not
// decorative: render_line_pixels()'s `lda nybbles,x` must never cross a
// page boundary (confirmed earlier this session -- a page-crossing
// access there would add a data-dependent extra cycle, breaking this
// module's cycle-exact timing).
//
// Section names declared here (not just where the region is pragma'd
// further down) since #pragma code/data/bss(name) needs the section
// already known.
#pragma section(modcode, 0)
#pragma section(moddata, 0)
#pragma section(modlowbss, 0)
#pragma section(modbss, 0)
#pragma section(bssovl1, 0)
#pragma overlay(ovl_nybbles, 1)
#pragma bss(bssovl1)
static unsigned char nybbles[256];
#pragma align(nybbles, 256)
#pragma bss(bss)

#pragma code(modcode)
static void init_nybbles(void)
{
    unsigned i;
    for (i = 0; i < 256; i++)
        nybbles[i] = (unsigned char)(i >> 4);
}
#pragma code(code)

// ---------------------------------------------------------------
// Relocated picture columns 0..UPIC_RELOC_COLS-1: $E000-$EFFF exactly
// (see upic_viewer.h's doc comment for the full rationale). Its own
// tight region, separate from upiccode below, since render_line_pixels()
// hardcodes these bytes' addresses as literal `ldx $eXX0,y` operands --
// unlike modcode's other content, this data must land at an EXACT,
// predetermined address, not just "somewhere in $E000-$FFFF".
// ---------------------------------------------------------------
#pragma section(picreloc, 0)
#pragma region(picreloc, 0xe000, 0xe800, , , {picreloc})
#ifdef UPIC_EMBED_DRAGON
#pragma data(picreloc)
__export volatile char upic_buffer_reloc[UPIC_RELOC_BYTES] = {
    #embed 2048 0 "dragon3.upic"
};
#pragma data(data)
#elif defined(UPIC_EMBED_ICELAND)
#pragma data(picreloc)
__export volatile char upic_buffer_reloc[UPIC_RELOC_BYTES] = {
    #embed 2048 0 "diamond_beach.upic"
};
#pragma data(data)
#else
#pragma bss(picreloc)
volatile char upic_buffer_reloc[UPIC_RELOC_BYTES];
#pragma bss(bss)
#endif

// ---------------------------------------------------------------
// Render code: freed KERNAL ROM area, $F000-$FFFF now (shrunk from
// $E000-$FFFF -- the bottom 4KB is picreloc's above). Reachable only
// once MMAP_NO_ROM is active. Separate from "main" purely to leave low
// memory free for the picture buffer above -- see that region's own
// comment.
// ---------------------------------------------------------------
// modcode/moddata (declared here, used by modplay.c) share this same
// region for modplay's tick-processing call tree -- see modplay.c's
// own comment on modplay_tick for why that's safe (in short:
// modplay_irq stays in normal memory and temporarily re-banks to
// MMAP_NO_ROM around just its own `jsr modplay_tick`, rather than
// modplay_tick's whole call tree needing to live somewhere always
// valid regardless of banking). NOT safe for $A000-$BFFF instead --
// that's fully claimed by upic_buffer above, confirmed the hard way
// (silent data corruption, no build error) earlier this session.
// modcode/moddata/modbss already declared above, alongside nybbles[].
#pragma section(upiccode, 0)
#pragma region(upiccode, 0xe800, 0x10000, , , {upiccode, modcode, moddata, modbss})
#pragma code(upiccode)

// Renders all 192 pixel-pairs of a single scanline (Y = current row,
// set by render_frame before each call). Its own subroutine so
// render_frame's loop-back branch stays short -- see this file's own
// doc comment above.
static void render_line_pixels(void)
{
    __asm {
        ldx $e000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $e700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $1f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $2f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $3f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $4f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $5f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $6f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $7f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $8f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9a00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9b00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9c00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9d00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9e00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $9f00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $a900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $aa00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ab00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ac00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ad00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ae00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $af00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $b900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ba00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $bb00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $bc00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $bd00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $be00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $bf00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c000,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c100,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c200,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c300,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c400,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c500,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c600,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c700,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c800,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $c900,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ca00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $cb00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $cc00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $cd00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $ce00,y
        stx $d020
        lda nybbles,x
        sta $d020
        ldx $cf00,y
        stx $d020
        lda nybbles,x
        sta $d020
    }
}

// Renders one full PAL frame (256 scanlines) of upic_buffer via the
// border-color ($D020) raster trick. Must only be called while
// MMAP_NO_ROM is active (see upic_show_frame()) and turbo is enabled
// (see turbo_fast(), include/turbo.h) -- at 1 MHz this loop cannot
// keep up with the raster beam at all.
static void render_frame(void)
{
    __asm {
        lda #$00                 // let VIC-II rest: DEN=0 widens the
        sta $d011                // "border" to the whole visible area
                                  // (also zeroes the raster-IRQ compare
                                  // MSB; doesn't affect the read-back
                                  // MSB used by the sync waits below)

    f1:
        lda $d011
        bpl f1
    f2:
        lda $d011
        bmi f2

        lda #$18                // top of image (raster line 24)
    topwait:
        cmp $d012
        bne topwait

        ldy #$00
    line:
        lda $d012
    linewait:
        cmp $d012                // wait for the raster line to tick
        beq linewait

        lda #$80                 // resync: rewrite turbo control regs
        ldx #$8f
        sta $d031
        stx $d031

        ldx #$51                 // timing pad to start of visible area
    delay:
        dex
        bne delay

        jsr render_line_pixels

        lda #$00
        sta $d020

        iny
        cpy #$00                 // 256 rows -- Y wraps 255->0 to exit
        bne line
    }
}

#pragma code(code)

// ---------------------------------------------------------------
// C-level wrapper: stays in the normal, always-executable default
// region (below $A000) so it can safely perform the memory-map switch
// itself. See upic_viewer.h for the calling convention.
// ---------------------------------------------------------------
static char nybbles_ready = 0;

char upic_show_frame(void)
{
    // ROM banking is shared, demo-wide setup now -- see rombank.h.
    // Idempotent: whichever module (this one, modplay.c, ...) calls it
    // first does the real work, safe to call every frame. $E000-$FFF9
    // becomes ordinary, always-executable RAM for the picture-viewing
    // session as a result -- not toggled per frame like the old design.
    // Must happen BEFORE init_nybbles() below: nybbles[] itself now
    // lives at $E000 too (see its own declaration), only valid RAM
    // once ROM is actually banked out.
    rombank_out();

    if (!nybbles_ready) {
        init_nybbles();
        nybbles_ready = 1;
    }

    // No local SEI/CLI around the render itself (2026-09-11, was
    // removed here): interrupts are masked globally and permanently
    // from main()'s own top-level SEI now, root-causing a real-hardware
    // "any key press drops to text mode" crash -- see main.c's own
    // comment for the full story. A local re-enable here would have
    // undone that for the gap between frames.
    render_frame();

    keyb_poll();
    return key_pressed(KSCAN_SPACE);
}

void upic_restore_display(void)
{
    // Wait for the exit key to be physically RELEASED before doing
    // anything else -- upic_show_frame() exits its loop the instant it's
    // detected PRESSED, but the user's finger is still on the key for
    // some short but nonzero time after that; the KERNAL's real,
    // interrupt-driven keyboard scan could otherwise see it still held
    // as a fresh event and (re-)act on it before we've finished
    // restoring anything. keyb_poll()/key_pressed() do direct CIA
    // matrix scanning (see c64/keyboard.c), so this works regardless of
    // ROM banking or interrupt state.
    //
    // SPACE, not RUN/STOP, deliberately: RUN/STOP was the original exit
    // key, but real-hardware testing showed a persistent "BREAK IN 10"
    // on exit that survived every targeted fix tried -- clearing the
    // keyboard buffer ($C6/NDX), making this whole restore sequence
    // atomic (SEI/CLI), this release-wait, and resetting STKEY ($91,
    // the KERNAL's separate RUN/STOP-specific latch, read by the $FFE1
    // STOP-check routine BASIC calls between statements). Diagnosed via
    // a controlled swap: building with SPACE as the exit key instead
    // showed no break and no leftover character at all, proving the
    // issue is specific to RUN/STOP's own extra KERNAL-level handling
    // (it does something beyond both the buffer and STKEY that wasn't
    // identified), not a generic timing race. Pragmatic decision: use
    // SPACE, which works cleanly, rather than keep chasing RUN/STOP's
    // exact mechanism. Revisit only if RUN/STOP specifically becomes a
    // hard requirement later.
    do {
        keyb_poll();
    } while (key_pressed(KSCAN_SPACE));

    // Whole restore sequence is SEI'd -- keeps it atomic on top of the
    // release-wait above (a real interrupt landing mid-sequence could
    // otherwise re-populate the keyboard buffer after our clear below).
    __asm { sei }

    // Deliberately does NOT touch ROM banking. Earlier versions of this
    // function restored ROM here (mmap_set(MMAP_ROM)), on the
    // assumption a picture scene was the only thing needing it banked
    // out. Now that ROM stays banked out for the WHOLE demo's lifetime
    // (see rombank.h) -- required so there's enough RAM for real effect
    // code alongside the picture buffer and modplay.c -- restoring it
    // here would break every OTHER scene that also depends on
    // $A000-$BFFF/$E000-$FFF9 being usable. If a caller genuinely needs
    // to return to BASIC (a standalone test harness, not the real demo),
    // call rombank_restore() explicitly itself, separately from this
    // function -- see src/upic_test.c and friends.

    *(volatile char *)0xd011 = 0x1b;  // standard default: DEN=1, RSEL=1, YSCROLL=3
    // $D020 itself (not just DEN) needs restoring too -- the render loop
    // leaves it holding whatever raw index the last line's clear wrote
    // (0/black), which stays black under any palette until something
    // explicitly writes a normal border index back in. Confirmed on real
    // hardware: DEN restore alone still showed a black border over an
    // otherwise-correct, un-hung BASIC screen.
    *(volatile char *)0xd020 = 0x0e;  // standard default border: light blue

    // Clear the KERNAL keyboard buffer count ($C6/NDX) -- required, not
    // decorative. upic_show_frame() detects the exit key via keyb_poll()'s
    // direct CIA matrix scan, completely bypassing the KERNAL's own
    // keyboard-buffer feeding (which only runs from the KERNAL's IRQ-
    // driven scan, masked throughout the render loop). The KERNAL never
    // gets a chance to "consume" that keypress while we're reading it
    // ourselves -- so once interrupts resume and its own scan restarts,
    // it could otherwise see the still-fresh keypress and react to it.
    // $C6 is NDX, the count of characters currently queued in the
    // keyboard buffer ($0277-$0280) -- zeroing it discards anything
    // queued so nothing replays once normal KERNAL processing resumes.
    // (This was originally written against RUN/STOP as the exit key,
    // where it was necessary but not sufficient -- see the exit-key
    // doc comment above. Kept for SPACE too: harmless, still correct
    // general hygiene against whatever got queued during the session.)
    *(volatile char *)0xc6 = 0;

    // Also reset STKEY ($91) to $7F (not-pressed) -- a separate latch
    // from the keyboard buffer above, set by the KERNAL's own scan when
    // it sees RUN/STOP held alone, read by the KERNAL's STOP-check
    // routine ($FFE1) that BASIC calls between statements. Added while
    // chasing the RUN/STOP "BREAK IN 10" issue -- did NOT fully fix it
    // (see the exit-key doc comment above for what actually resolved
    // it: switching to SPACE). Kept anyway as harmless defensive
    // cleanup of stray KERNAL state, in case RUN/STOP was also pressed
    // by the user at some point during the session even though it's no
    // longer the exit key.
    *(volatile char *)0x91 = 0x7f;

    __asm { cli }
}
#pragma code(code)

// ---------------------------------------------------------------
// Shrink Oscar64's own default "main" region so it stops at $1000,
// leaving $1000-$d000 free for upic_buffer above. This is the hard
// constraint discovered while designing this module: everything else
// this project links in (UCI library calls actually reached, turbo
// control, this file's own wrapper code, stack) must fit in
// $0a00-$1000 -- under 1.5 KB. No heap section: this module doesn't
// use malloc(), and heap was the first thing to not fit. See
// docs/UPIC_VIEWER.md's "Memory budget" section for what that does
// and doesn't leave room for.
// ---------------------------------------------------------------
// Starts at $0853, not Oscar64's usual $0a00 default -- $0801-$0852 is
// the actual BASIC-stub/startup region's real usage (confirmed via
// .map: "0801 - 0880 : 0853, 0052, startup", i.e. the startup region's
// own declared bound only ever fills to $0853 of the $0880 it claims,
// and everything from $0880-$09FF was otherwise completely unclaimed
// address space, not part of any region at all). Reclaims 429 bytes
// total vs. the $0a00 default. Confirmed byte-exact against a known-
// good build (no placement conflict, no silent overlap corruption --
// see the modplayregion incident in docs/UPIC_VIEWER.md for why that
// check matters here).
// heap: uii_change_dir()/uii_open_file() (include/ultimate_dos_lib.c)
// no longer use malloc() as of 2026-09-09 -- switched to a shared
// static command buffer specifically because crt_malloc/crt_free's own
// code (~359 bytes, confirmed via the .map) was the difference between
// upicmodplay fitting at "main"'s natural region bounds and not, once
// modplay_load()/modplay_init() moved out to #pragma overlay functions
// (see modplay.h and upic_viewer.h's own notes) freed up everything
// ELSE it was possible to free. Every OTHER malloc-using function in
// ultimate_dos_lib.c/ultimate_common_lib.c is untouched (still mallocs
// normally if a future build actually calls one) -- heapsize/heap are
// kept declared here, tiny, as a defensive default for that case. Must
// stay in this main region's own section list or malloc() silently
// returns NULL (see oscar64manual.md's heap-placement gotcha) -- this
// project already has its own custom #pragma region(main, ...) below,
// so this is the safe case that gotcha describes, not the risky one.
// Upper bound extended $1000 -> $2000 (2026-09-09): the picture-buffer
// relocation above (see upic_viewer.h) frees $1000-$1FFF for ordinary
// low-memory use. This single extension replaces the ENTIRE "lowmem"
// compressed-inlay mechanism this session built and then abandoned --
// see git history / docs/UPIC_VIEWER.md for that mechanism's own
// story if it's ever needed again for a build where this relocation
// alone isn't enough (upicmodplay, most likely -- its modplay/dos_lib
// content still needs reassessing against this new budget). Ordinary
// region extension, not a compressed inlay: $1000 is safely ABOVE the
// BASIC-stub/startup region ($0801-$0853), so widening "main" upward
// into it does NOT touch the file's own lowest-address content the
// way widening down to $0200 did -- confirmed no $0801 load-address
// regression (see the picreloc/upiccode split above, and rebuild+
// verify `xxd -l2` on any test target after touching these bounds).
#pragma heapsize(32)
#pragma stacksize(210)
#pragma region(main, 0x0853, 0x1800, , , {code, data, bss, heap, stack})

// modlowbss: $0200-$0800, BSS ONLY -- confirmed by direct test (2026-09-09)
// that an uninitialized-only region below $0801 does NOT shift the
// compiled .prg's own load address the way lowmem's CODE/DATA did:
// BSS has no real content to store in the file at all (Oscar64's
// startup code just zeroes it at runtime), so there's nothing there to
// become the file's "lowest loaded address" in the first place. CODE
// or initialized DATA placed this low WOULD still shift it -- see the
// abandoned "lowmem" section above for why, and never place code/data
// here without first confirming the resulting .prg still loads at
// $0801 (`xxd -l2`) AND still runs via the Ultimate's own PRG-run
// mechanism (a clean compile is not sufficient evidence, as lowmem's
// own story shows). $0100-$01FF still excluded regardless of content
// type -- real 6502 hardware stack, not something any region pragma
// can safely claim. Used for modplay.c's largest pure-BSS globals
// (the struct + scratch buffers), freeing that space from competing
// with modcode/upiccode's own tight $F000-$FFFF budget -- see
// modplay.c's own comments on what's moved here and why.
//
// FINDING (2026-09-09): a plain #pragma region below $0801 holding real
// content -- modlowbss, exactly as declared here -- corrupts
// upicmodplay.prg's own load-address header ($0002 instead of $0801).
// Reproduced the SAME class of bug the earlier nybbles-in-modlowbss
// regression hit, but this time for the whole modplay struct,
// independent of main's width or mod_hdr_buf. Isolated via direct
// bisection against two alternatives that both stayed correct: (a)
// the same content in plain default `bss` inside "main" itself (no
// separate low region at all), and (b) the same low address range
// declared as an Oscar64-native #pragma overlay region instead (see
// ovl1 below) -- only the plain-#pragma-region-below-$0801 case
// breaks. modlowbss abandoned entirely as a strategy; section kept
// declared (empty, unused) only because modplay.c's own comments still
// reference it pending a docs cleanup pass -- do not section anything
// into it again without re-verifying `xxd -l2` on the result.
#pragma section(modlowbss, 0)

// ovl1: NOT a swappable overlay in this build -- permanently holds
// nybbles[] instead (see its own declaration above for why). Was
// modplay_load()'s overlay before that function was removed entirely
// (2026-09-09) in favor of calling rombank.h's overlay_stage()
// directly -- see modplay.h's own note. If a real swappable overlay is
// ever needed again alongside nybbles, it needs its OWN window (nybbles
// must stay resident, never overwritten) -- do not reuse ovl1 for both.
#pragma section(codeovl1, 0)
#pragma section(dataovl1, 0)
#pragma section(bssovl1, 0)
#pragma region(ovl1, 0x0200, 0x0800, , 1, { codeovl1, dataovl1, bssovl1 })

// ovl2: second overlay sharing the SAME address window as ovl1 (same
// pattern as ~/VDCScreenEditor2's vdcseovl1/vdcseovl3, which also share
// one address at different ids) -- for modplay_init(), one-time,
// non-resident code.
#pragma section(codeovl2, 0)
#pragma section(dataovl2, 0)
#pragma section(bssovl2, 0)
#pragma region(ovl2, 0x0200, 0x0800, , 2, { codeovl2, dataovl2, bssovl2 })
