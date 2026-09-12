/*****************************************************************
Mandelbrot Upic -- interactive zoom-target selection (implementation)
See zoom.h for API documentation and current status.
******************************************************************/

#include <c64/keyboard.h>
#include "upic_viewer.h"
#include "mandelbrot.h"
#include "zoom.h"

// "main"'s own budget has no room for this file's functions either
// (same story as mandelbrot.c/upic_viewer.c's own precedent) -- placed
// in upiccode instead.
#pragma code(upiccode)

// Plain shift-subtract unsigned 16-bit division, NOT the C library's
// generic divmod32 (crt.c) -- this project's own math (mandelbrot.c)
// deliberately never needs division at all (fixed_mul()/fixed_sqr()
// are multiply/shift only), so divmod32 was never linked in before
// this file's zoom-bounds computation below needed one, and it
// compiles to "main"-region code by default, which has no room. This
// one small division is cheaper to write by hand than to fight
// placing the generic one. 16-bit, not 32: the dividend here is always
// (box width or height, cells) times mandel_dx/dy, and both factors
// are bounded well within 16 bits regardless of zoom level -- width/
// height max 384 (the whole picture), mandel_dx/dy max
// MANDEL_DEFAULT_DX/DY (16, the widest the per-pixel step ever gets --
// zooming in only ever shrinks it), so the product tops out at
// 384*16=6144. Only ever called with a non-negative dividend here --
// no signed-division handling needed.
static unsigned zoom_udiv16(unsigned dividend, unsigned divisor)
{
    unsigned quotient = 0;
    unsigned remainder = 0;
    signed char bit;

    for (bit = 15; bit >= 0; bit--)
    {
        remainder = (remainder << 1) | ((dividend >> bit) & 1);
        if (remainder >= divisor)
        {
            remainder -= divisor;
            quotient |= (1U << bit);
        }
    }
    return quotient;
}

// Clamps mandel_x0/y0 (the CURRENT mandel_dx/dy already set) into the
// original default overview's own extent -- so browse-mode panning can
// never see anything the default view itself didn't already contain.
// Operates directly on the globals, no parameters.
static void zoom_clamp_view(void)
{
    if (mandel_x0 < MANDEL_DEFAULT_X0) mandel_x0 = MANDEL_DEFAULT_X0;
    if (mandel_y0 < MANDEL_DEFAULT_Y0) mandel_y0 = MANDEL_DEFAULT_Y0;
    if (mandel_x0 + UPIC_WIDTH  * mandel_dx > MANDEL_DEFAULT_X1)
        mandel_x0 = MANDEL_DEFAULT_X1 - UPIC_WIDTH  * mandel_dx;
    if (mandel_y0 + UPIC_HEIGHT * mandel_dy > MANDEL_DEFAULT_Y1)
        mandel_y0 = MANDEL_DEFAULT_Y1 - UPIC_HEIGHT * mandel_dy;
}

// Placed in "main" (the default code region), not upiccode (2026-09-11)
// -- the shared upiccode/moddata/modbss pool had almost no room left
// after this file's 4-corner/outlined-marker upgrade, while "main" had
// real headroom. Deliberately does NOT call zoom_clamp_view() above
// (still in upiccode) -- tried that first, confirmed via the .map's
// own per-object address audit (this project's own established habit
// near this boundary) that the cross-region call's proxy overhead
// actually GREW upiccode's own usage enough to wrap marker_backup into
// zero page -- a real, silent corruption, not just a theoretical risk.
// Duplicates the same 4-line clamp inline instead, entirely within
// "main", to avoid the cross-region call altogether. (A second attempt
// at recovering the still-remaining ~34 bytes by factoring the 6
// edge-detected key checks in zoom_select() into a shared helper was
// ALSO tried and reverted -- the helper's own pointer-parameter call
// overhead cost more than the code deduplication saved, same class of
// false economy as "convert parameters to pointers" from this project's
// own documented history of unreliable micro-optimizations. The 34
// bytes were found in mandelbrot.c instead -- see its own comment.)
#pragma code(code)

// Zoom OUT -- widens the CURRENT view by ZOOM_OUT_FACTOR around its own
// centre, clamped to the original default overview, i.e. one notch
// out, not an instant jump back to the default. Also floors mandel_dx/
// dy implicitly via the MANDEL_DEFAULT_DX/DY cap below -- the zoom-IN
// boundary check (confirm/RETURN, further down this file) has its own
// floor at 1 raw Q5.11 unit; between the two, neither direction can
// ever push mandel_dx/dy outside [1, MANDEL_DEFAULT_DX/DY].
#define ZOOM_OUT_FACTOR 2

static void zoom_out_view(void)
{
    fixed_t old_dx = mandel_dx;
    fixed_t old_dy = mandel_dy;

    mandel_dx *= ZOOM_OUT_FACTOR;
    mandel_dy *= ZOOM_OUT_FACTOR;
    if (mandel_dx > MANDEL_DEFAULT_DX) mandel_dx = MANDEL_DEFAULT_DX;
    if (mandel_dy > MANDEL_DEFAULT_DY) mandel_dy = MANDEL_DEFAULT_DY;

    // Centre stays fixed: new_x0 = (x0 + halfwidth*old_dx) - halfwidth*new_dx
    //                            = x0 - halfwidth*(new_dx - old_dx)
    // -- one multiply per axis instead of two.
    mandel_x0 -= (UPIC_WIDTH  / 2) * (mandel_dx - old_dx);
    mandel_y0 -= (UPIC_HEIGHT / 2) * (mandel_dy - old_dy);

    // Same bounds check as zoom_clamp_view() (upiccode, used by
    // zoom_pan()) -- duplicated here rather than called, see this
    // function's own opening comment for why.
    if (mandel_x0 < MANDEL_DEFAULT_X0) mandel_x0 = MANDEL_DEFAULT_X0;
    if (mandel_y0 < MANDEL_DEFAULT_Y0) mandel_y0 = MANDEL_DEFAULT_Y0;
    if (mandel_x0 + UPIC_WIDTH  * mandel_dx > MANDEL_DEFAULT_X1)
        mandel_x0 = MANDEL_DEFAULT_X1 - UPIC_WIDTH  * mandel_dx;
    if (mandel_y0 + UPIC_HEIGHT * mandel_dy > MANDEL_DEFAULT_Y1)
        mandel_y0 = MANDEL_DEFAULT_Y1 - UPIC_HEIGHT * mandel_dy;
}

#pragma code(upiccode)

// Corner markers are drawn DIRECTLY INTO THE PACKED PICTURE BUFFER, not
// VIC-II hardware sprites -- confirmed via two isolated, controlled
// standalone tests (built and run on real hardware, not just reasoned
// about) that hardware sprites cannot be composited at all while this
// project's Upic border-racing technique holds DEN=0 across the whole
// frame: a plain DEN=1 text-screen sprite test showed a sprite exactly
// as expected, but the SAME sprite setup (same shape, same enable/
// color/position, all independently confirmed correct via live
// register reads) under a MINIMAL standalone version of the DEN=0/
// per-line-$D020-racing technique -- no Mandelbrot math, no UCI,
// nothing else -- was invisible. Every sprite register always read
// back correct; the display technique itself is just fundamentally
// incompatible with hardware sprites, contrary to the "sprites show
// fine in open-border tricks" lore this project's own zoom-feature
// history started from (which describes a DIFFERENT technique --
// briefly toggling RSEL per line to prevent the border flip-flop
// latching -- not holding DEN low across an entire multi-frame
// session).
//
// A marker is a SINGLE pixel of ZOOM_MARKER_COLOR_INDEX (zoom.h)
// written straight into upic_buffer/upic_buffer_reloc, restored from a
// 1-byte-per-marker backup before the next move so it doesn't leave a
// permanent dot where it used to be.

// One pixel's address in the packed picture buffer -- mirrors
// mandelbrot_generate()'s own dst[] choice between upic_buffer_reloc
// (columns 0..UPIC_RELOC_COLS-1) and upic_buffer (the rest), see its
// own comment (mandelbrot.c) for why the split exists at all.
static volatile char *zoom_pixel_addr(int col, int row)
{
    unsigned bytecol = (unsigned)col >> 1;
    if (bytecol < UPIC_RELOC_COLS)
        return &upic_buffer_reloc[bytecol * UPIC_HEIGHT + (unsigned)row];
    return &upic_buffer[(bytecol - UPIC_RELOC_COLS) * UPIC_HEIGHT + (unsigned)row];
}

// Even column -> low nibble, odd column -> high nibble -- matches
// mandelbrot_generate()'s own packing (`(odd << 4) | even`, where
// "even"/"odd" name the two pixel columns sharing one byte, not
// anything about the row).
static unsigned char zoom_get_pixel(int col, int row)
{
    unsigned char b = (unsigned char)*zoom_pixel_addr(col, row);
    return (col & 1) ? (unsigned char)(b >> 4) : (unsigned char)(b & 0x0f);
}

static void zoom_set_pixel(int col, int row, unsigned char color)
{
    volatile char *p = zoom_pixel_addr(col, row);
    unsigned char b = (unsigned char)*p;
    if (col & 1)
        b = (unsigned char)((b & 0x0f) | (color << 4));
    else
        b = (unsigned char)((b & 0xf0) | color);
    *p = (char)b;
}

// Each marker is a 2x2 solid white block (ZOOM_MARKER_COLOR_INDEX).
// All 4 corners get one (2026-09-11, upgraded from 2 single-pixel
// markers -- real memory headroom now that sprites/joystick/histogram
// are gone, see zoom.h). A 4x4 version with a 1-pixel black outline
// was tried first and confirmed working on real hardware, but reverted
// the same day to make room for 'O' (zoom-out) once the shared
// upiccode/moddata/modbss pool ran short again -- 4 markers and a
// visible 2x2 core were kept (the user's own priority order put the
// outline last: "if possible"), see zoom_out_view()'s own comment for
// the fuller budget story. Outline can come back once more room is
// found elsewhere.
#define ZOOM_MARKER_SIZE 2

// Per-marker state: where it's CURRENTLY drawn (top-left of its 4x4
// block, already clamped into the picture) and the real pixel values
// it's covering, so they can be put back before the marker moves
// again. markers_valid guards the very first draw of a session
// (nothing to restore yet) and gets cleared whenever the underlying
// view is about to change entirely (a confirmed zoom -- the backup
// would refer to a view that's about to be regenerated from scratch
// anyway).
#pragma bss(modbss)
static char marker_backup[4][ZOOM_MARKER_SIZE * ZOOM_MARKER_SIZE];
static int marker_col[4];
static int marker_row[4];
static unsigned char markers_valid = 0;
#pragma bss(bss)

static void zoom_marker_restore(unsigned char idx)
{
    int basecol = marker_col[idx];
    int baserow = marker_row[idx];
    unsigned char dr, dc;

    for (dr = 0; dr < ZOOM_MARKER_SIZE; dr++)
        for (dc = 0; dc < ZOOM_MARKER_SIZE; dc++)
            zoom_set_pixel(basecol + dc, baserow + dr,
                (unsigned char)marker_backup[idx][dr * ZOOM_MARKER_SIZE + dc]);
}

// col/row here are the TRUE corner point -- the 2x2 block is centered
// on it (top-left of the block is 1 cell up/left of the corner), then
// clamped so the WHOLE block stays inside the picture (a corner can
// legitimately sit right at column 0/383 or row 0/255 --
// zoom_move_box()'s own clamp).
static void zoom_marker_draw(unsigned char idx, int col, int row)
{
    int basecol = col - 1;
    int baserow = row - 1;
    unsigned char dr, dc;

    if (basecol < 0) basecol = 0;
    if (basecol > UPIC_WIDTH - ZOOM_MARKER_SIZE) basecol = UPIC_WIDTH - ZOOM_MARKER_SIZE;
    if (baserow < 0) baserow = 0;
    if (baserow > UPIC_HEIGHT - ZOOM_MARKER_SIZE) baserow = UPIC_HEIGHT - ZOOM_MARKER_SIZE;

    for (dr = 0; dr < ZOOM_MARKER_SIZE; dr++)
    {
        for (dc = 0; dc < ZOOM_MARKER_SIZE; dc++)
        {
            marker_backup[idx][dr * ZOOM_MARKER_SIZE + dc] =
                (char)zoom_get_pixel(basecol + dc, baserow + dr);
            zoom_set_pixel(basecol + dc, baserow + dr, ZOOM_MARKER_COLOR_INDEX);
        }
    }
    marker_col[idx] = basecol;
    marker_row[idx] = baserow;
}

static void zoom_markers_restore_all(void)
{
    unsigned char i;
    if (markers_valid)
        for (i = 0; i < 4; i++)
            zoom_marker_restore(i);
}

// Positions all 4 corner markers at the box's actual corners.
// left/top/right/bottom must already be in the right order (the box is
// always moved/resized as a whole, so ccol-half_w <= ccol+half_w and
// crow-half_h <= crow+half_h always hold). Restores the PREVIOUS
// frame's marker positions first (if any).
static void zoom_markers_update(int left, int top, int right, int bottom)
{
    zoom_markers_restore_all();

    zoom_marker_draw(0, left,  top);
    zoom_marker_draw(1, right, top);
    zoom_marker_draw(2, left,  bottom);
    zoom_marker_draw(3, right, bottom);
    markers_valid = 1;
}

static void zoom_markers_hide(void)
{
    zoom_markers_restore_all();
    markers_valid = 0;
}

// Reads WASD/cursor-key input into a 4-bit direction mask -- shared by
// zoom_move_box() and zoom_pan(), both of which need the exact same
// "read WASD/cursor, shift-qualify the cursor keys" logic, just
// applying the result differently. Keyboard only for now -- no
// joystick (see zoom.h's own STATUS comment).
//
// - W/A/S/D -- the primary scheme, no shift-qualifier complications.
// - Cursor keys, shift-qualified -- convenience for muscle memory. The
//   C64 keyboard matrix only has physical DOWN/RIGHT cursor positions;
//   UP/LEFT are the SAME two matrix positions read with shift held
//   (true in hardware, not just a software convention), so unshifted
//   cursor-right/down move right/down and shift+cursor-right/down move
//   left/up. KSCAN_SHIFT_LOCK is, perhaps surprisingly, the correct
//   scan code to check for a physically held LEFT shift too, not just
//   the shift-lock switch -- they share the exact same matrix position
//   in real C64 keyboard hardware (shift-lock is a latching switch
//   that shorts that same line). KSCAN_RSHIFT covers right shift.
#define ZOOM_DIR_UP    0x01
#define ZOOM_DIR_DOWN  0x02
#define ZOOM_DIR_LEFT  0x04
#define ZOOM_DIR_RIGHT 0x08

static unsigned char zoom_read_direction(void)
{
    unsigned char shift = key_pressed(KSCAN_SHIFT_LOCK) || key_pressed(KSCAN_RSHIFT);
    unsigned char csr_right = key_pressed(KSCAN_CSR_RIGHT);
    unsigned char csr_down  = key_pressed(KSCAN_CSR_DOWN);
    unsigned char dir = 0;

    if (key_pressed(KSCAN_A) || (csr_right && shift))  dir |= ZOOM_DIR_LEFT;
    if (key_pressed(KSCAN_D) || (csr_right && !shift)) dir |= ZOOM_DIR_RIGHT;
    if (key_pressed(KSCAN_W) || (csr_down && shift))   dir |= ZOOM_DIR_UP;
    if (key_pressed(KSCAN_S) || (csr_down && !shift))  dir |= ZOOM_DIR_DOWN;

    return dir;
}

// Cells moved per frame while a direction key is held -- plain
// level-triggered repeat (key_pressed() reflects the CURRENT held
// state, polled once per upic_show_frame() call, ~20ms/poll), not
// separately debounced like RETURN/Z/C/+/- below -- continuous
// movement while held is the whole point here.
#define ZOOM_MOVE_STEP 4

// Box size tracked in UNITS, not cells directly: the selection box
// must always keep the picture's own 384:256 = 3:2 aspect ratio. One
// unit = 6 cells wide / 4 cells tall (both exact factors of 384:256's
// own 3:2 ratio), so box_w/box_h are always exact integers with NO
// division ever needed at runtime -- this project deliberately never
// links the generic divmod32 library (see zoom_udiv16's own comment),
// and plain `/3`-type expressions would silently pull it back in.
#define ZOOM_UNIT_MIN     4    // box 24x16
#define ZOOM_UNIT_MAX     64   // box 384x256 -- the whole picture, i.e. no zoom at all
#define ZOOM_UNIT_DEFAULT 32   // box 192x128
#define ZOOM_UNIT_STEP    2    // +/- change per keypress: box changes by 12x8 cells

// zoom_select()'s own selection state -- ALL static: a 'C' press
// returns to main() (see zoom_pending_palette's own comment for why)
// so main() can push the palette from its own context, then calls
// zoom_select() again -- this state needs to survive that round-trip
// so the user's in-progress box picks up exactly where it left off,
// rather than resetting every time 'C' is pressed. needs_reset
// distinguishes "resuming after a palette round-trip" (leave
// everything alone) from "starting fresh for a newly generated view"
// (back to browse mode) -- set whenever zoom_select() is about to
// return ZOOM_CONFIRMED, since ccol/crow/size_units are cell
// coordinates in the view that's about to stop being current.
//
// box_mode: 0 = "browse" -- WASD/cursor PANS the current view (see
// zoom_pan()) and 'Z' enters box mode; 1 = "box" -- the corner markers
// are shown, WASD/cursor MOVES THE WHOLE BOX (zoom_move_box()), '+'/'-'
// resize it (aspect ratio always locked), RETURN confirms (zooms in),
// and 'Z' cancels back to browse mode without zooming.
static unsigned char box_mode = 0;
static int ccol, crow;
static int size_units;
static unsigned char return_was_down = 0; // edge-detect confirm (RETURN)
static unsigned char c_was_down = 0;      // edge-detect palette cycling ('C')
static unsigned char z_was_down = 0;      // edge-detect box-mode toggle ('Z')
static unsigned char o_was_down = 0;      // edge-detect zoom-out ('O')
static unsigned char plus_was_down = 0;   // edge-detect grow ('+')
static unsigned char minus_was_down = 0;  // edge-detect shrink ('-')
static unsigned char palette_index = 0;   // persists across zoom levels too
static unsigned char needs_reset = 1;     // starts true: first call needs browse mode

static void zoom_resize(int delta)
{
    int new_units = size_units + delta;
    if (new_units >= ZOOM_UNIT_MIN && new_units <= ZOOM_UNIT_MAX)
        size_units = new_units;
}

// Moves the WHOLE box by ZOOM_MOVE_STEP per held direction, clamped so
// it stays fully within the picture's own cell bounds at the CURRENT
// size.
//
// Max bound is UPIC_WIDTH - half_w, NOT UPIC_WIDTH - 1 - half_w
// (2026-09-12 fix): the box's true INCLUSIVE right edge is
// ccol+half_w-1 (width = 2*half_w cells starting at left=ccol-half_w,
// see the RETURN/confirm code below, which already uses exactly this
// left+width-1 range), so the correct bound is ccol+half_w-1 <=
// UPIC_WIDTH-1, i.e. ccol <= UPIC_WIDTH-half_w. The old, off-by-one
// bound was harmless for ordinary box sizes (it just let ccol settle
// one cell short of where it should), but at size_units==ZOOM_UNIT_MAX
// (64, "the whole picture") it made the min bound (half_w=192) and old
// max bound (UPIC_WIDTH-1-half_w=191) simultaneously unsatisfiable --
// confirmed by exhaustively simulating every reachable size_units in
// Python, not assumed -- so ccol always settled on whichever clamp ran
// last (191), one cell short of the true center (192), making
// left=ccol-half_w go negative (-1). zoom_marker_draw()'s own >=0
// clamp absorbed that specific case without visible corruption, but
// see zoom_markers_update()'s own call site below for the OTHER half
// of this fix -- the right/bottom marker position, which had the same
// off-by-one at every box size, not just the max one.
static void zoom_move_box(void)
{
    unsigned char dir = zoom_read_direction();
    int half_w = size_units * 3;
    int half_h = size_units * 2;

    if (dir & ZOOM_DIR_LEFT)  ccol -= ZOOM_MOVE_STEP;
    if (dir & ZOOM_DIR_RIGHT) ccol += ZOOM_MOVE_STEP;
    if (dir & ZOOM_DIR_UP)    crow -= ZOOM_MOVE_STEP;
    if (dir & ZOOM_DIR_DOWN)  crow += ZOOM_MOVE_STEP;

    if (ccol < half_w) ccol = half_w;
    if (ccol > UPIC_WIDTH - half_w) ccol = UPIC_WIDTH - half_w;
    if (crow < half_h) crow = half_h;
    if (crow > UPIC_HEIGHT - half_h) crow = UPIC_HEIGHT - half_h;
}

// Browse-mode movement: pans the CURRENT view -- shifts mandel_x0/y0
// by 1/ZOOM_PAN_FRACTION of the view's own current width/height, same
// mandel_dx/dy (zoom level unchanged) -- rather than moving a
// selection box. Proportional to the current zoom depth by
// construction (a deep zoom's mandel_dx/dy are small, so the same
// fractional step is a small absolute move; the default view's are
// large, so it's a big one). No-op at the default overview (nothing to
// pan to) and clamped so panning can never see anything the default
// view itself didn't already contain. Returns 1 if the view actually
// changed (caller should regenerate), 0 otherwise.
//
// Plain int (fixed_t), not long: every intermediate here is bounded by
// UPIC_WIDTH/HEIGHT times the DEFAULT step (max 384*16=6144), same
// reasoning as zoom_udiv16's own comment -- comfortably inside a
// 16-bit signed range without needing wider arithmetic.
#define ZOOM_PAN_FRACTION 4

static unsigned char zoom_pan(void)
{
    unsigned char dir;
    fixed_t step_x, step_y;

    if (mandel_dx == MANDEL_DEFAULT_DX && mandel_dy == MANDEL_DEFAULT_DY)
        return 0;   // already fully zoomed out -- nothing to pan to

    dir = zoom_read_direction();
    if (!dir)
        return 0;

    step_x = (UPIC_WIDTH  / ZOOM_PAN_FRACTION) * mandel_dx;
    step_y = (UPIC_HEIGHT / ZOOM_PAN_FRACTION) * mandel_dy;

    if (dir & ZOOM_DIR_LEFT)  mandel_x0 -= step_x;
    if (dir & ZOOM_DIR_RIGHT) mandel_x0 += step_x;
    if (dir & ZOOM_DIR_UP)    mandel_y0 -= step_y;
    if (dir & ZOOM_DIR_DOWN)  mandel_y0 += step_y;

    zoom_clamp_view();
    return 1;
}

const char *zoom_pending_palette;

unsigned char zoom_select(void)
{
    if (needs_reset)
    {
        // A freshly generated view always starts in browse mode.
        box_mode = 0;
        needs_reset = 0;
    }

    for (;;)
    {
        if (box_mode)
        {
            int half_w = size_units * 3;
            int half_h = size_units * 2;
            // right/bottom are ccol+half_w-1 / crow+half_h-1, NOT
            // ccol+half_w / crow+half_h (2026-09-12 fix, see
            // zoom_move_box()'s own comment on the same root cause):
            // the box's true INCLUSIVE right/bottom edge is one cell
            // less than the un-adjusted sum -- matches exactly what the
            // RETURN/confirm code below computes as left+width-1. The
            // old version drew the right/bottom corner markers one cell
            // past the box's actual edge, at every box size (not just
            // the max-size case zoom_move_box() fixes).
            zoom_markers_update(ccol - half_w, crow - half_h, ccol + half_w - 1, crow + half_h - 1);
        }

        // Keeps redrawing the CURRENT (already-complete) picture via
        // the border-flash technique -- same call the old plain
        // `while (!upic_show_frame());` loop made, just with our own
        // extra key handling and marker updates interleaved between
        // calls instead of nothing. Its own SPACE-detection return
        // value is irrelevant here -- SPACE isn't one of this screen's
        // own controls.
        upic_show_frame();

        keyb_poll();

        // Cycle the base color gradient (mandelbrot.h's
        // mandel_palettes[]) -- edge-detected like confirm below, not
        // level-triggered like movement, so a held 'C' advances once
        // per press instead of racing through all options in one go.
        // Does NOT push the palette itself -- returns to main() to do
        // that instead; see zoom_pending_palette's own comment (in
        // zoom.h) for why. Markers are NOT hidden here (unlike confirm
        // below) -- this screen reappears immediately once main()
        // pushes the palette and calls back in, same selection in
        // progress.
        if (key_pressed(KSCAN_C))
        {
            if (!c_was_down)
            {
                c_was_down = 1;
                palette_index = (unsigned char)((palette_index + 1) % MANDEL_PALETTE_COUNT);
                zoom_pending_palette = mandel_palettes[palette_index];
                return ZOOM_PALETTE_CHANGED;
            }
        }
        else
        {
            c_was_down = 0;
        }

        // Zoom OUT -- widens by one notch, clamped to the default
        // overview; see zoom_out_view()'s own comment. Works from
        // EITHER mode -- an "abandon whatever I was doing, step back
        // out" escape hatch. No-op at the default overview already
        // (nothing to widen to -- same check zoom_pan() uses for its
        // own no-op case), so it doesn't trigger a pointless regenerate
        // of an identical picture. Returns ZOOM_CONFIRMED (main() reacts
        // identically to a zoom-in confirm) rather than a dedicated
        // return code.
        if (key_pressed(KSCAN_O))
        {
            if (!o_was_down)
            {
                o_was_down = 1;
                if (!(mandel_dx == MANDEL_DEFAULT_DX && mandel_dy == MANDEL_DEFAULT_DY))
                {
                    zoom_out_view();
                    box_mode = 0;
                    needs_reset = 1;
                    zoom_markers_hide();
                    return ZOOM_CONFIRMED;
                }
            }
        }
        else
        {
            o_was_down = 0;
        }

        // Box-mode toggle -- browse mode is the default after
        // generation; WASD/cursor PANS the current view there instead
        // of moving a selection box (see zoom_pan()) -- 'Z' enters box
        // mode to actually pick a zoom target, and pressing it AGAIN
        // while already in box mode cancels back to browse without
        // zooming.
        if (key_pressed(KSCAN_Z))
        {
            if (!z_was_down)
            {
                z_was_down = 1;
                if (!box_mode)
                {
                    box_mode = 1;
                    size_units = ZOOM_UNIT_DEFAULT;
                    ccol = UPIC_WIDTH / 2;
                    crow = UPIC_HEIGHT / 2;
                    // Fresh edge-detect state for the box-mode-only
                    // keys below -- guards against a stray already-
                    // held key (e.g. RETURN held from some earlier
                    // action) misfiring the instant box mode starts.
                    return_was_down = 0;
                    plus_was_down = 0;
                    minus_was_down = 0;
                }
                else
                {
                    box_mode = 0;
                    zoom_markers_hide();
                }
            }
        }
        else
        {
            z_was_down = 0;
        }

        if (box_mode)
        {
            // Grow/shrink the box, aspect ratio always preserved (both
            // dimensions scale together via size_units) -- edge-
            // detected, one step per press, same reasoning as
            // everywhere else here.
            if (key_pressed(KSCAN_PLUS))
            {
                if (!plus_was_down)
                    zoom_resize(ZOOM_UNIT_STEP);
                plus_was_down = 1;
            }
            else
            {
                plus_was_down = 0;
            }

            if (key_pressed(KSCAN_MINUS))
            {
                if (!minus_was_down)
                    zoom_resize(-ZOOM_UNIT_STEP);
                minus_was_down = 1;
            }
            else
            {
                minus_was_down = 0;
            }

            zoom_move_box();

            // Confirm: RETURN -- edge-detected, single step (move the
            // box where you want it, size it how you want it, press
            // once to confirm).
            if (key_pressed(KSCAN_RETURN))
            {
                if (!return_was_down)
                {
                    int half_w = size_units * 3;
                    int half_h = size_units * 2;
                    int left   = ccol - half_w;
                    int top    = crow - half_h;
                    int width  = half_w * 2;
                    int height = half_h * 2;

                    // New view: the selected sub-rectangle of the
                    // CURRENT view, mapped through its own
                    // mandel_x0/y0/dx/dy -- so repeated zooms compose
                    // correctly (each zoom is always relative to what's
                    // currently displayed, not the original default
                    // overview).
                    fixed_t new_x0 = (fixed_t)(mandel_x0 + (long)left * mandel_dx);
                    fixed_t new_y0 = (fixed_t)(mandel_y0 + (long)top * mandel_dy);
                    fixed_t new_dx = (fixed_t)zoom_udiv16((unsigned)(width * mandel_dx), UPIC_WIDTH);
                    fixed_t new_dy = (fixed_t)zoom_udiv16((unsigned)(height * mandel_dy), UPIC_HEIGHT);

                    // Max-zoom-in boundary check: floor both steps at
                    // 1 raw Q5.11 unit -- a computed 0 would sample
                    // every column/row at the same coordinate (a
                    // degenerate, solid-color result -- see
                    // mandelbrot.h's own precision-limit comment).
                    // Flooring instead of rejecting the confirm
                    // outright still zooms in, just not by quite as
                    // much as the box implied.
                    if (new_dx < 1) new_dx = 1;
                    if (new_dy < 1) new_dy = 1;

                    mandel_x0 = new_x0;
                    mandel_y0 = new_y0;
                    mandel_dx = new_dx;
                    mandel_dy = new_dy;

                    // Next zoom_select() call is for a newly generated
                    // view -- ccol/crow/size_units (cell coordinates in
                    // the view that's ending) don't carry over, unlike
                    // a palette-cycle round-trip.
                    box_mode = 0;
                    needs_reset = 1;

                    zoom_markers_hide();
                    return ZOOM_CONFIRMED;
                }
                return_was_down = 1;
            }
            else
            {
                return_was_down = 0;
            }
        }
        else
        {
            // Browse mode: WASD/cursor pans the current view instead
            // of moving a box -- see zoom_pan()'s own comment.
            if (zoom_pan())
            {
                needs_reset = 1;
                return ZOOM_CONFIRMED;
            }
        }
    }
}

#pragma code(code)
