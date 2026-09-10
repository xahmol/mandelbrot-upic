/*****************************************************************
Mandelbrot Upic -- interactive zoom-target selection (implementation)
See zoom.h for API documentation and current (untested) status.
******************************************************************/

#include <c64/keyboard.h>
#include "upic_viewer.h"
#include "mandelbrot.h"
#include "ultimate_common_lib.h"
#include "zoom.h"

// Direct VIC-II register pokes for sprite setup, NOT Oscar64's own
// sprite library (<c64/sprites.h>) -- tried first (2026-09-10),
// reverted: its int-arithmetic internals pull in crt.c's generic
// divmod32/bitshift runtime support, which compiles to "main"-region
// code/data by default -- confirmed via a real build error ("main"
// region completely full: 0 bytes available in code, data, AND bss
// simultaneously). Redirecting the ambient #pragma code/data/bss
// target around the library's #include didn't help either -- its
// #pragma compile("sprites.c") apparently defers actual compilation
// past where the redirect had already been reset back. Writing the
// handful of register pokes sprite setup actually needs directly
// avoids the whole problem, and matches this project's own established
// style throughout upic_viewer.c/turbo.c (hardcoded register addresses,
// no library abstraction) rather than introducing a new one just for
// this feature.
#define VIC_SPR0_X     (*(volatile unsigned char *)0xD000)
#define VIC_SPR0_Y     (*(volatile unsigned char *)0xD001)
#define VIC_SPR1_X     (*(volatile unsigned char *)0xD002)
#define VIC_SPR1_Y     (*(volatile unsigned char *)0xD003)
#define VIC_SPR2_X     (*(volatile unsigned char *)0xD004)
#define VIC_SPR2_Y     (*(volatile unsigned char *)0xD005)
#define VIC_SPR3_X     (*(volatile unsigned char *)0xD006)
#define VIC_SPR3_Y     (*(volatile unsigned char *)0xD007)
#define VIC_SPR_MSBX   (*(volatile unsigned char *)0xD010)
#define VIC_SPR_ENABLE (*(volatile unsigned char *)0xD015)
#define VIC_SPR0_COLOR (*(volatile unsigned char *)0xD027)
#define VIC_SPR1_COLOR (*(volatile unsigned char *)0xD028)
#define VIC_SPR2_COLOR (*(volatile unsigned char *)0xD029)
#define VIC_SPR3_COLOR (*(volatile unsigned char *)0xD02A)
// Sprite pointer bytes: last 8 bytes of whatever "screen memory" the
// VIC is configured for -- $0400 (this project never changes $D018
// from its power-on default), so $0400+$3F8 = $07F8. Same convention
// spr_init()/UBoot64-v2 use, just applied as plain literal addresses.
#define VIC_SPR_PTR0   (*(volatile unsigned char *)0x07F8)
#define VIC_SPR_PTR1   (*(volatile unsigned char *)0x07F9)
#define VIC_SPR_PTR2   (*(volatile unsigned char *)0x07FA)
#define VIC_SPR_PTR3   (*(volatile unsigned char *)0x07FB)

#define VIC_COLOR_WHITE 1

// ---------------------------------------------------------------
// Corner-marker sprite shape: a plain 8x8 filled square, roughly
// centred in the 24x21 sprite canvas (middle byte of each of rows
// 7-14 set to 0xFF, everything else blank). All 4 corner sprites
// share this ONE shape (same image pointer) -- simple, symmetric, no
// need for 4 distinct rotated shapes.
//
// NOT loaded directly at $0340 (a first attempt at that -- a
// dedicated fixed-address region, matching picreloc's own pattern --
// broke the whole .prg: confirmed via `xxd -l2` that Oscar64's linker
// picks the LOWEST address with any real content as the file's own
// load address, and $0340 is below the standard $0801, silently
// shifting it there. Exactly the documented "lowmem" gotcha from
// landoficeandfire's own history: any real content below $0801 does
// this, unconditionally -- not something specific to this placement).
// Stored as normal const data instead (moddata, same as sq_table --
// see its own comment for why "main" has no room), and copied into
// place at $0340 at RUNTIME instead (zoom_sprites_setup(), once) --
// $0340 itself is just plain, always-writable RAM (the cassette
// buffer), no load-time content needed there at all, only a runtime
// one. See zoom.h's own comment for why $0340 specifically (matches
// UBoot64-v2's own sprite placement, confirmed via this session's
// research that upic_buffer/upic_buffer_reloc between them cover part
// of every single VIC bank, making this the only genuinely free spot
// for sprite data that doesn't corrupt live picture data).
#pragma data(moddata)
static const char zoom_corner_shape[63] = {
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0xff,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
    0x00,0x00,0x00,
};
#pragma data(data)

#define ZOOM_SPRITE_IMAGE (0x0340 / 64)

// NEEDS HARDWARE CALIBRATION (2026-09-10) -- not yet verified against
// real hardware. Standard C64 sprite Y=50 is well documented as the
// top of the NORMAL 25-row display (raster line ~$33=51, i.e.
// Y_reg = raster_line - 1); render_frame() (upic_viewer.c) starts the
// Upic picture earlier, at raster line $18=24, since this project's
// DEN=0 technique widens the display into the whole normally-border
// area -- so 24-1=23 is this file's best first guess for
// ZOOM_SPRITE_Y0, by the same relationship. ZOOM_SPRITE_X0 is the
// standard documented left-edge X value (24) with no project-specific
// adjustment basis found -- also unverified. If the corner markers
// don't visually line up with the picture on first test, these two
// additive offsets are the first (and should be the ONLY) thing to
// adjust -- the scale (1 sprite pixel = 1 picture pixel) should
// already be correct since both are native, unscaled coordinates.
#define ZOOM_SPRITE_X0 24
#define ZOOM_SPRITE_Y0 23

// Minimum selection size, in picture cells -- guards against a
// degenerate (near-zero) zoom target that would round mandel_dx/dy
// down to 0 in Q5.11 (11 fractional bits -- see mandelbrot.h), which
// would sample every column/row at the same coordinate. 16 cells is
// arbitrary but comfortably clear of that floor even at this
// project's default per-pixel step.
#define ZOOM_MIN_SIZE 16

// Cells moved per frame while a direction key is held -- plain
// level-triggered repeat (key_pressed() reflects the CURRENT held
// state, polled once per upic_show_frame() call, ~20ms/poll), not
// separately debounced like RETURN/Q below -- continuous movement
// while held is the whole point here.
#define ZOOM_MOVE_STEP 4

// "main"'s own budget has no room left for this file's functions
// either (confirmed the same way as the sprite-library attempt above:
// a real build error, "main" region completely full) -- placed in
// upiccode instead, matching sq_table's/mandelbrot.c's own precedent.
#pragma code(upiccode)

// Plain shift-subtract unsigned long division, NOT the C library's
// generic divmod32 (crt.c) -- this project's own math (mandelbrot.c)
// deliberately never needed division at all (fixed_mul()/fixed_sqr()
// are multiply/shift only), so divmod32 was never linked in before
// this file's zoom-bounds computation below needed one -- and, same
// problem as the sprite library above, it compiles to "main"-region
// code by default, which has no room (confirmed: real build error,
// "Size 231 Available 0"). This one small division is cheaper to
// write by hand (32 shift/compare/subtract steps, called exactly
// twice per confirmed zoom) than to fight placing the generic one.
// Only ever called with a non-negative dividend here (a selection
// width/height times a positive per-pixel step, both always >= 0) --
// no signed-division handling needed.
static unsigned long zoom_udiv32(unsigned long dividend, unsigned divisor)
{
    unsigned long quotient = 0;
    unsigned long remainder = 0;
    signed char bit;

    for (bit = 31; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((dividend >> bit) & 1);
        if (remainder >= divisor)
        {
            remainder -= divisor;
            quotient |= (1UL << bit);
        }
    }
    return quotient;
}

static void zoom_sprites_setup(void)
{
    unsigned char i;

    // Copy the shape into place at $0340 -- see zoom_corner_shape's
    // own comment for why this happens here (runtime) instead of
    // being loaded there directly.
    for (i = 0; i < 63; i++)
        *(volatile char *)(0x0340 + i) = zoom_corner_shape[i];

    VIC_SPR_PTR0 = ZOOM_SPRITE_IMAGE;
    VIC_SPR_PTR1 = ZOOM_SPRITE_IMAGE;
    VIC_SPR_PTR2 = ZOOM_SPRITE_IMAGE;
    VIC_SPR_PTR3 = ZOOM_SPRITE_IMAGE;
    VIC_SPR0_COLOR = VIC_COLOR_WHITE;
    VIC_SPR1_COLOR = VIC_COLOR_WHITE;
    VIC_SPR2_COLOR = VIC_COLOR_WHITE;
    VIC_SPR3_COLOR = VIC_COLOR_WHITE;
    VIC_SPR_MSBX = 0;   // all 4 corners stay well under 256 -- MSB never needed
    VIC_SPR_ENABLE = 0x0f;   // sprites 0-3 on, 4-7 (unused) stay off
}

static void zoom_sprites_hide(void)
{
    VIC_SPR_ENABLE = 0x00;
}

// Positions the 4 corner sprites at the actual 4 corners of the
// rectangle spanned by (col0,row0)-(col1,row1) -- order-independent,
// either point can be the top-left or bottom-right at any moment
// while the user is still adjusting them.
static void zoom_sprites_update(int col0, int row0, int col1, int row1)
{
    int left   = (col0 < col1) ? col0 : col1;
    int right  = (col0 < col1) ? col1 : col0;
    int top    = (row0 < row1) ? row0 : row1;
    int bottom = (row0 < row1) ? row1 : row0;

    VIC_SPR0_X = (unsigned char)(ZOOM_SPRITE_X0 + left);
    VIC_SPR0_Y = (unsigned char)(ZOOM_SPRITE_Y0 + top);
    VIC_SPR1_X = (unsigned char)(ZOOM_SPRITE_X0 + right);
    VIC_SPR1_Y = (unsigned char)(ZOOM_SPRITE_Y0 + top);
    VIC_SPR2_X = (unsigned char)(ZOOM_SPRITE_X0 + left);
    VIC_SPR2_Y = (unsigned char)(ZOOM_SPRITE_Y0 + bottom);
    VIC_SPR3_X = (unsigned char)(ZOOM_SPRITE_X0 + right);
    VIC_SPR3_Y = (unsigned char)(ZOOM_SPRITE_Y0 + bottom);
}

// Moves (col,row) by ZOOM_MOVE_STEP per held direction input, clamped
// to the picture's own cell bounds. Three equivalent input sources,
// all checked every call:
//
// - W/A/S/D -- the primary scheme, no shift-qualifier complications.
// - Cursor keys, shift-qualified -- convenience for muscle memory
//   (2026-09-10, requested). The C64 keyboard matrix only has physical
//   DOWN/RIGHT cursor positions; UP/LEFT are the SAME two matrix
//   positions read with shift held (true in hardware, not just a
//   software convention), so unshifted cursor-right/down move
//   right/down and shift+cursor-right/down move left/up.
//   KSCAN_SHIFT_LOCK is, perhaps surprisingly, the correct scan code
//   to check for a physically held LEFT shift too, not just the
//   shift-lock switch -- they share the exact same matrix position in
//   real C64 keyboard hardware (shift-lock is a latching switch that
//   shorts that same line). KSCAN_RSHIFT covers right shift.
// - Joystick port 2 ($DC00, CIA1 port A) -- port 1 ($DC01, port B) is
//   deliberately NOT used: it's shared electrically with the keyboard
//   matrix's own column reads, a well-documented C64 quirk that makes
//   joystick-1 readings unreliable while keys are also being scanned
//   (which this screen does, every frame). Read directly (this
//   project's own established style throughout, see the VIC register
//   macros above) rather than via <c64/joystick.h> -- avoids repeating
//   the same "library placement doesn't fit main, and redirecting it
//   via the #include point doesn't work" problem already hit with the
//   sprite library (see zoom_corner_shape's own comment history/the
//   commit that added this file). All 5 joystick lines are ACTIVE LOW.
#define CIA1_JOY2 (*(volatile unsigned char *)0xDC00)

static void zoom_move(int *col, int *row)
{
    unsigned char shift = key_pressed(KSCAN_SHIFT_LOCK) || key_pressed(KSCAN_RSHIFT);
    unsigned char csr_right = key_pressed(KSCAN_CSR_RIGHT);
    unsigned char csr_down  = key_pressed(KSCAN_CSR_DOWN);
    unsigned char joy = CIA1_JOY2;

    if (key_pressed(KSCAN_A) || (csr_right && shift)  || !(joy & 0x04))
        *col -= ZOOM_MOVE_STEP;
    if (key_pressed(KSCAN_D) || (csr_right && !shift) || !(joy & 0x08))
        *col += ZOOM_MOVE_STEP;
    if (key_pressed(KSCAN_W) || (csr_down && shift)   || !(joy & 0x01))
        *row -= ZOOM_MOVE_STEP;
    if (key_pressed(KSCAN_S) || (csr_down && !shift)  || !(joy & 0x02))
        *row += ZOOM_MOVE_STEP;

    if (*col < 0) *col = 0;
    if (*col > UPIC_WIDTH - 1) *col = UPIC_WIDTH - 1;
    if (*row < 0) *row = 0;
    if (*row > UPIC_HEIGHT - 1) *row = UPIC_HEIGHT - 1;
}

unsigned char zoom_select(void)
{
    // Starting box: the middle half of the current picture -- as
    // reasonable a default as any, and immediately visible/movable
    // rather than a degenerate single point.
    int col0 = UPIC_WIDTH / 4;
    int row0 = UPIC_HEIGHT / 4;
    int col1 = (3 * UPIC_WIDTH) / 4;
    int row1 = (3 * UPIC_HEIGHT) / 4;
    unsigned char phase = 0;          // 0 = moving corner A, 1 = moving corner B
    unsigned char return_was_down = 0; // edge-detect confirm (RETURN/fire) -- see below
    unsigned char c_was_down = 0;      // edge-detect palette cycling ('C') -- see below

    // `static` so the selected palette persists across zoom levels --
    // zoom_select() is called once per generation, and the palette
    // itself already stays active on the device regardless (only ever
    // pushed again here, on request) -- this just remembers WHICH one
    // is current so 'C' continues cycling forward from there instead
    // of resetting to the default each time.
    static unsigned char palette_index = 0;

    zoom_sprites_setup();

    for (;;)
    {
        zoom_sprites_update(col0, row0, col1, row1);

        // Keeps redrawing the CURRENT (already-complete) picture via
        // the border-flash technique -- same call the old plain
        // `while (!upic_show_frame());` loop made, just with our own
        // extra key handling and sprite updates interleaved between
        // calls instead of nothing. Its own SPACE-detection return
        // value is irrelevant here (SPACE isn't one of this screen's
        // own controls) -- ignored on purpose, same as during
        // mandelbrot_generate()'s live build-up.
        upic_show_frame();

        keyb_poll();

        if (key_pressed(KSCAN_Q))
        {
            zoom_sprites_hide();
            return 0;
        }

        // Cycle the base color gradient (mandelbrot.h's
        // mandel_palettes[]) -- edge-detected like confirm below, not
        // level-triggered like movement, so a held 'C' advances once
        // per press instead of racing through all options in one go.
        // Plain uii_setpalette(), not main.c's setpalette_retry()
        // wrapper -- that retry loop exists for the specific "right
        // after a cold device boot" race (see its own comment); by
        // the time this screen is showing a completed picture, UCI has
        // already accepted at least one palette push successfully.
        if (key_pressed(KSCAN_C))
        {
            if (!c_was_down)
            {
                palette_index = (unsigned char)((palette_index + 1) % MANDEL_PALETTE_COUNT);
                uii_setpalette(mandel_palettes[palette_index]);
            }
            c_was_down = 1;
        }
        else
        {
            c_was_down = 0;
        }

        if (phase == 0)
            zoom_move(&col0, &row0);
        else
            zoom_move(&col1, &row1);

        // Confirm: RETURN or joystick fire (either works, same action)
        // -- edge-detected (only acts on the down-transition), not
        // level-triggered like movement -- unlike holding a direction
        // key (where continuous repeat is the whole point), a held
        // confirm input must advance phase 0->1 and then confirm
        // phase 1 exactly ONCE each, not race through both within the
        // same physical press (this is polled once per ~20ms frame,
        // so a normal press stays "down" for several polls).
        if (key_pressed(KSCAN_RETURN) || !(CIA1_JOY2 & 0x10))
        {
            if (!return_was_down)
            {
                if (phase == 0)
                {
                    phase = 1;
                }
                else
                {
                    int left   = (col0 < col1) ? col0 : col1;
                    int right  = (col0 < col1) ? col1 : col0;
                    int top    = (row0 < row1) ? row0 : row1;
                    int bottom = (row0 < row1) ? row1 : row0;

                    if (right - left >= ZOOM_MIN_SIZE && bottom - top >= ZOOM_MIN_SIZE)
                    {
                        // New view: the selected sub-rectangle of the
                        // CURRENT view, mapped through its own
                        // mandel_x0/y0/dx/dy -- so repeated zooms
                        // compose correctly (each zoom is always
                        // relative to what's currently displayed, not
                        // the original default overview). Same
                        // range/WIDTH convention the default view
                        // itself uses (see mandelbrot.h) rather than
                        // range/(WIDTH-1), for consistency.
                        fixed_t new_x0 = (fixed_t)(mandel_x0 + (long)left * mandel_dx);
                        fixed_t new_y0 = (fixed_t)(mandel_y0 + (long)top * mandel_dy);
                        fixed_t new_dx = (fixed_t)zoom_udiv32((unsigned long)((long)(right - left) * mandel_dx), UPIC_WIDTH);
                        fixed_t new_dy = (fixed_t)zoom_udiv32((unsigned long)((long)(bottom - top) * mandel_dy), UPIC_HEIGHT);

                        mandel_x0 = new_x0;
                        mandel_y0 = new_y0;
                        mandel_dx = new_dx;
                        mandel_dy = new_dy;

                        zoom_sprites_hide();
                        return 1;
                    }
                    // Selection too small -- ignore the confirm and
                    // keep adjusting phase 1 rather than accepting a
                    // degenerate zoom.
                }
            }
            return_was_down = 1;
        }
        else
        {
            return_was_down = 0;
        }
    }
}

#pragma code(code)
