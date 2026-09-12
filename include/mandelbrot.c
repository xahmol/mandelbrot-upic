/*****************************************************************
Mandelbrot Upic -- fractal generator (implementation)
See mandelbrot.h for API documentation and current status.
******************************************************************/

#include <c64/cia.h>
#include "mandelbrot.h"

// Set at the end of mandelbrot_generate() from CIA1's TOD clock
// (reset to 0 at the start) -- read these back via
// ultimate_read_memory after a run to get an objective generation-time
// measurement, rather than guessing from how it looks/feels. Same
// simple reset+poll pattern ultimate_common_lib.c's uii_wait_for_uci()
// already uses successfully.
volatile unsigned char mandel_gen_mins = 0;
volatile unsigned char mandel_gen_secs = 0;
volatile unsigned char mandel_gen_tenths = 0;

// ---------------------------------------------------------------
// Quarter-square multiply table: sq_table[n] = floor(n*n/4) for
// n=0..510 (entry 511 unused padding, to a clean 512 entries).
// Backs the classic identity a*b = floor((a+b)^2/4) - floor((a-b)^2/4)
// -- EXACT for any integers a,b, since a+b and a-b always share parity
// (their sum is 2a), so whichever square loses a remainder to the
// floor, both lose the same amount and it cancels in the subtraction.
// Independently verified exhaustively in Python (all 65,536 8-bit
// unsigned pairs, all 65,536 values for the squaring composition,
// 200,000+ random pairs for the full 16x16 signed composition) before
// writing any of the code below -- see CREDITS.md.
//
// Placed in `moddata` (2026-09-09), NOT the default `data` section --
// this 1024-byte table doesn't fit in "main"'s own data budget (only
// ~300 bytes free there -- confirmed by a real build error, not a
// guess) but $E800-$FFFF's upiccode region (modcode/moddata/modbss,
// reserved for modplay's audio code in the sibling upicmodplay build,
// entirely unused by this one) has ~3.7KB genuinely free, confirmed by
// finding zero objects placed past $F159 in the .map. Safe to read
// during mandelbrot_generate() specifically because rombank_out()
// (MMAP_NO_ROM) already runs before it, for the same reason
// upic_buffer_reloc ($E000-$EFFF) already needs and gets that -- see
// upic_viewer.h. moddata/modcode already declared via #pragma
// section(...) in upic_viewer.c, compiled first in main.c's #pragma
// compile chain -- no need to redeclare here, just place into it.
// ---------------------------------------------------------------
#pragma data(moddata)
static const unsigned sq_table[512] = {
    0,0,1,2,4,6,9,12,16,20,25,30,36,42,49,56,64,72,81,90,100,110,121,132,144,156,169,182,196,210,225,
    240,256,272,289,306,324,342,361,380,400,420,441,462,484,506,529,552,576,600,625,650,676,702,729,
    756,784,812,841,870,900,930,961,992,1024,1056,1089,1122,1156,1190,1225,1260,1296,1332,1369,1406,
    1444,1482,1521,1560,1600,1640,1681,1722,1764,1806,1849,1892,1936,1980,2025,2070,2116,2162,2209,
    2256,2304,2352,2401,2450,2500,2550,2601,2652,2704,2756,2809,2862,2916,2970,3025,3080,3136,3192,
    3249,3306,3364,3422,3481,3540,3600,3660,3721,3782,3844,3906,3969,4032,4096,4160,4225,4290,4356,
    4422,4489,4556,4624,4692,4761,4830,4900,4970,5041,5112,5184,5256,5329,5402,5476,5550,5625,5700,
    5776,5852,5929,6006,6084,6162,6241,6320,6400,6480,6561,6642,6724,6806,6889,6972,7056,7140,7225,
    7310,7396,7482,7569,7656,7744,7832,7921,8010,8100,8190,8281,8372,8464,8556,8649,8742,8836,8930,
    9025,9120,9216,9312,9409,9506,9604,9702,9801,9900,10000,10100,10201,10302,10404,10506,10609,
    10712,10816,10920,11025,11130,11236,11342,11449,11556,11664,11772,11881,11990,12100,12210,12321,
    12432,12544,12656,12769,12882,12996,13110,13225,13340,13456,13572,13689,13806,13924,14042,14161,
    14280,14400,14520,14641,14762,14884,15006,15129,15252,15376,15500,15625,15750,15876,16002,16129,
    16256,16384,16512,16641,16770,16900,17030,17161,17292,17424,17556,17689,17822,17956,18090,18225,
    18360,18496,18632,18769,18906,19044,19182,19321,19460,19600,19740,19881,20022,20164,20306,20449,
    20592,20736,20880,21025,21170,21316,21462,21609,21756,21904,22052,22201,22350,22500,22650,22801,
    22952,23104,23256,23409,23562,23716,23870,24025,24180,24336,24492,24649,24806,24964,25122,25281,
    25440,25600,25760,25921,26082,26244,26406,26569,26732,26896,27060,27225,27390,27556,27722,27889,
    28056,28224,28392,28561,28730,28900,29070,29241,29412,29584,29756,29929,30102,30276,30450,30625,
    30800,30976,31152,31329,31506,31684,31862,32041,32220,32400,32580,32761,32942,33124,33306,33489,
    33672,33856,34040,34225,34410,34596,34782,34969,35156,35344,35532,35721,35910,36100,36290,36481,
    36672,36864,37056,37249,37442,37636,37830,38025,38220,38416,38612,38809,39006,39204,39402,39601,
    39800,40000,40200,40401,40602,40804,41006,41209,41412,41616,41820,42025,42230,42436,42642,42849,
    43056,43264,43472,43681,43890,44100,44310,44521,44732,44944,45156,45369,45582,45796,46010,46225,
    46440,46656,46872,47089,47306,47524,47742,47961,48180,48400,48620,48841,49062,49284,49506,49729,
    49952,50176,50400,50625,50850,51076,51302,51529,51756,51984,52212,52441,52670,52900,53130,53361,
    53592,53824,54056,54289,54522,54756,54990,55225,55460,55696,55932,56169,56406,56644,56882,57121,
    57360,57600,57840,58081,58322,58564,58806,59049,59292,59536,59780,60025,60270,60516,60762,61009,
    61256,61504,61752,62001,62250,62500,62750,63001,63252,63504,63756,64009,64262,64516,64770,65025,
    0
};
#pragma data(data)

// a*b for UNSIGNED 8-bit a,b, via the quarter-square identity above --
// O(1) (two table lookups + a subtract) instead of a shift-add loop.
// `static inline` (a hint, not a guarantee) so this collapses directly
// into fixed_mul()/fixed_sqr()'s own generated code where possible --
// deliberately PLAIN C, no inline asm anywhere in this file (see
// fixed_mul()'s comment below for why).
static inline unsigned qmul8u(unsigned char a, unsigned char b)
{
    unsigned sum = (unsigned)a + b;
    unsigned char diff = (a > b) ? (unsigned char)(a - b) : (unsigned char)(b - a);
    return (unsigned)(sq_table[sum] - sq_table[diff]);
}

// a*b for two Q5.11 values, composed from 4 unsigned 8x8 quarter-
// square multiplies (the classic byte-split long-multiplication
// decomposition: a*b = al*bl + (al*bh+ah*bl)<<8 + ah*bh<<16), then
// sign-corrected and rescaled back to Q5.11.
//
// A quarter-square-table multiply was tried here TWICE before
// (2026-09-09, see git history -- the first attempt's include/
// fastmul.c was never actually committed, so isn't recoverable, see
// CREDITS.md):
//
// Attempt 1: several separate hand-written __asm functions (multiply
// -> muls16 -> mulu16 -> 4x qmul8 -> 2x qsub8, using raw
// indirect-indexed table addressing). Independently verified correct
// in Python and in an isolated single hardware call, but gave silently
// WRONG results when called repeatedly with caller-side locals that
// needed to stay live across several calls. Traced (via a delegated
// research pass -- see CREDITS.md) to a documented, currently-open
// Oscar64 whole-program `-O2` register-allocator bug class (upstream
// issues #318/#361 on drmortalwombat/oscar64) -- and this project's OWN
// Oscar64 reference (oscar64manual.md) independently documents THREE
// separate instances of this exact bug class found in other projects.
// Reverted to lmul16s()/lmul16u() (fixmath.h), which avoid it by being
// ONE flat __native asm function each -- no nested C calls at all.
//
// Attempt 2 (this one): same PLAIN-C, no-inline-asm approach as now
// (table lookups via ordinary array indexing, no hand-written asm
// anywhere, everything inlined directly into this function's own body
// rather than through wrapper functions -- avoids BOTH what attempt 1
// hit AND the general "deep whole-program call chain" version of that
// bug class, not just the asm-specific one). Algorithm re-verified
// exhaustively in Python again (same coverage as attempt 1). This
// attempt's actual blocker turned out to be a completely different,
// mundane one: the 1024-byte sq_table[] above didn't fit in "main"'s
// own data budget -- see its own comment for how that got solved
// (placement, not a size/algorithm change).
// SATURATES instead of wrapping when the true product's magnitude
// exceeds what a 16-bit fixed_t can hold (2026-09-12 -- real bug,
// found via a forum challenge to this file's earlier "it's the add/
// sub, not the multiply" diagnosis of visible noise speckles, then
// root-caused by reading real Ultimate 64 hardware's actual picture
// buffer and bisecting exactly which truncation point was needed to
// reproduce it byte-for-byte, see CREDITS.md). The quarter-square
// decomposition above is exact -- this isn't a precision problem in
// the multiply algorithm itself, it's that `product >> FIXED_SHIFT`
// can legitimately be a value Q5.11 has no room for (magnitude
// >= 16.0, raw >= 32768), and casting that down to fixed_t (16-bit
// signed) previously wrapped it into essentially-garbage output
// instead. Concretely hit inside mandel_in_cardioid_or_bulb() below:
// at extreme-|cx| pixels (near the view's own left edge) `q` and
// `q+xm` can EACH be several units wide, and their PRODUCT can exceed
// 16.0 even though neither operand alone does -- confirmed via direct
// hardware trace, this silently flipped the cardioid/bulb test's
// result at those pixels (wrapped to a large negative value, which
// then falsely compared as "inside the set"), rendering them solid
// black instead of their correct fast-escape color. Saturating to
// FIXED_MAX (rather than clamping to 0 or leaving it wrapped) keeps
// the result's SIGN correct and, since FIXED_MAX vastly exceeds any
// legitimate comparison threshold this function's two call sites ever
// use it against, it always resolves comparisons the same way an
// unbounded-range result would -- see fixed_sqr()'s own comment for
// the other, more frequently-hit call site this same technique fixes.
static fixed_t fixed_mul(fixed_t a, fixed_t b)
{
    unsigned char neg = 0;
    unsigned char al, ah, bl, bh;
    unsigned long p0, p1, p2, p3, product, mag;

    if (a < 0) { a = (fixed_t)(-a); neg ^= 1; }
    if (b < 0) { b = (fixed_t)(-b); neg ^= 1; }

    al = (unsigned char)a;
    ah = (unsigned char)((unsigned)a >> 8);
    bl = (unsigned char)b;
    bh = (unsigned char)((unsigned)b >> 8);

    p0 = qmul8u(al, bl);
    p1 = qmul8u(al, bh);
    p2 = qmul8u(ah, bl);
    p3 = qmul8u(ah, bh);
    product = p0 + ((p1 + p2) << 8) + (p3 << 16);
    mag = product >> FIXED_SHIFT;
    if (mag > (unsigned long)FIXED_MAX)
        mag = (unsigned long)FIXED_MAX;

    return (fixed_t)(neg ? -(long)mag : (long)mag);
}

// x*x, for the (common -- two of three multiplies per iteration are
// squares) case where sign-correction is pure waste (negate first,
// same as fixed_mul()) and one of the 4 partial products is
// redundant: with a=b, al*bh and ah*bl (p1/p2 above) are the exact
// same value, computed once and doubled here instead of computed
// twice -- 3 qmul8u() calls instead of 4.
//
// SATURATES instead of wrapping (2026-09-12) -- see fixed_mul()'s own
// comment for the full story and how this was found/verified; this is
// the more frequently-hit of the two call sites. mandel_iterate()'s
// escape check (zx2+zy2 > FIXED4) only ever runs on zx/zy values that
// already passed the SAME check one iteration ago, but that still
// allows |zx|/|zy| up to about 6.0 real by the time they're squared
// again next iteration (zx2<=4.0, zy2<=4.0 individually, so
// zx_new=zx2-zy2+cx can reach roughly [-6,+5] for this project's
// coordinate ranges) -- comfortably inside what a 16-bit fixed_t can
// STORE (+-16.0), but its SQUARE (up to 36.0) is not, so the old
// wrapping return corrupted zx2/zy2 right before the very check meant
// to catch escape, occasionally shifting a pixel's escape iteration
// (and so its color) by a few steps. Saturating to FIXED_MAX guarantees
// zx2+zy2 always correctly exceeds FIXED4 (4.0) in this case --
// confirmed byte-for-byte against real hardware AND an independent
// double-precision reference render, 0 pixels differing across the
// full default view once both this and fixed_mul()'s saturation are
// in place (see CREDITS.md).
//
// This region's own budget is razor-thin (see mandelbrot.h's FIXED_MAX
// comment) -- an early-out version tried here first (`if (a>=FIXED4)
// return FIXED_MAX;` before ever computing the product, skipping the
// qmul8u() calls entirely in the overflow case) actually compiled
// LARGER (+33 bytes) than this compute-then-clamp version (+15 bytes),
// somewhat counter to the "less work done" intuition -- measured via
// the .map, not assumed; kept the smaller one. Don't re-"optimize"
// this into an early-out without re-measuring against the .map.
static fixed_t fixed_sqr(fixed_t a)
{
    unsigned char al, ah;
    unsigned long p0, p1, p3, product, mag;

    if (a < 0)
        a = (fixed_t)(-a);

    al = (unsigned char)a;
    ah = (unsigned char)((unsigned)a >> 8);

    p0 = qmul8u(al, al);
    p1 = qmul8u(al, ah);
    p3 = qmul8u(ah, ah);
    product = p0 + (p1 << 9) + (p3 << 16);   // (p1+p1)<<8 == p1<<9
    mag = product >> FIXED_SHIFT;

    return (mag > (unsigned long)FIXED_MAX) ? FIXED_MAX : (fixed_t)(long)mag;
}

// Default per-pixel step in both axes: EXACTLY 16 (= 1/128 in Q5.11),
// chosen so the default view's bounds divide evenly by 384/256 pixels
// with zero accumulated rounding error -- see mandelbrot.h's own
// comment on the view (now mutable variables, not #define constants --
// 2026-09-10, see there for why). Real axis: -4096 (-2.0) + x*16, x in
// 0..383, reaching +2048 (1.0) - 16 at the right edge.
//
// Imaginary axis: -2040 (not -2048/-1.0 -- see below) + y*16, y in
// 0..255. Shifted by half a step (8 = 1/256 in Q5.11) off the "true"
// -1.0..+1.0 framing so the 256-row grid is EXACTLY symmetric about
// cy=0: row y and row 255-y are exact negatives of each other in
// Q5.11, bit for bit (cy(255-y) = -2040+16*(255-y) = 2040-16y =
// -(-2040+16y) = -cy(y)). The Mandelbrot set is symmetric under
// complex conjugation (mandel_iterate(cx,cy) == mandel_iterate(cx,-cy)
// always), so mandelbrot_generate() below iterates only the top half
// (y=0..127) and mirrors each row's result into its exact opposite
// -- roughly halves the dominant cost (iteration count) for free.
// Costs an imperceptible ~0.4% crop off both the top and bottom edges
// (true range becomes about -0.996..+0.996 instead of -1.0..+0.996)
// -- found during the same research pass as the lmul16s() swap above
// (see CREDITS.md), not previously noticed.
//
// THIS SYMMETRY IS SPECIFIC TO THE DEFAULT VIEW (2026-09-10): it's a
// property of THESE PARTICULAR bounds, not of the Mandelbrot set in
// general -- an arbitrary user-selected zoom target (see zoom.c)
// generally will NOT straddle cy=0 the way this one deliberately does
// (most interesting zoom targets don't happen to sit on the real
// axis). mandelbrot_generate() below detects this at runtime (checking
// whether the CURRENT mandel_y0/mandel_dy actually produce a symmetric
// view) and only takes the mirror-and-halve shortcut when it's valid
// -- a typical zoom falls back to computing all 256 rows directly,
// roughly doubling generation time relative to a symmetric view (still
// benefiting from the multiply/hoisting speedups elsewhere in this
// file).
fixed_t mandel_x0 = -4096;
fixed_t mandel_y0 = -2040;
fixed_t mandel_dx = 16;
fixed_t mandel_dy = 16;

// ---------------------------------------------------------------
// Cardioid / period-2-bulb early-skip (2026-09-09, moved up from
// planned Phase 3 -- real hardware timing showed this project's
// straightforward 3-multiplies-per-iteration approach, with no
// squaring table (see mandelbrot.h for why), needs it now, not later).
// Points inside either region are ALWAYS in the set and would
// otherwise burn the full MANDEL_MAX_ITER budget one iteration at a
// time -- a huge fraction of the visible image (everything near the
// origin) falls in these two closed-form-testable regions. Standard
// formulas (see the Wikipedia Mandelbrot set article's
// "Optimizations" section):
//   main cardioid: q = (cx-1/4)^2 + cy^2 ; in set if q*(q+(cx-1/4)) < cy^2/4
//   period-2 bulb: (cx+1)^2 + cy^2 < 1/16
//
// Takes xm/xm2/xp1_2/cy2 as ALREADY-COMPUTED arguments rather than raw
// cx/cy (2026-09-09) -- xm=cx-1/4, xm2=xm^2 and xp1_2=(cx+1)^2 depend
// only on the column, cy2=cy^2 only on the row, but this function used
// to be called once per PIXEL, recomputing all of them from scratch
// every time: cy2 alone was being recomputed 192x more often than
// needed (once per column instead of once for the whole row), and
// xm2/xp1_2 128x more often (once per row instead of once per
// column). mandelbrot_generate() now hoists these into a per-row table
// (cy2_table[]) and per-column locals, computed once each -- see its
// own comments. Pure loop-invariant hoisting, no new arithmetic or
// asm, so none of the fixed_mul()/fixed_sqr() risk above applies.
// ---------------------------------------------------------------
static char mandel_in_cardioid_or_bulb(fixed_t xm, fixed_t xm2, fixed_t xp1_2, fixed_t cy2)
{
    fixed_t q = (fixed_t)(xm2 + cy2);

    if ((long)fixed_mul(q, (fixed_t)(q + xm)) < ((long)cy2 >> 2))
        return 1;

    if ((long)(xp1_2 + cy2) < (long)(FIXED_ONE / 16))
        return 1;

    return 0;
}

// ---------------------------------------------------------------
// Escape-time iteration for one point. Returns the iteration count at
// which |z| exceeded 2 (escape radius, tested as |z|^2 > 4 to avoid a
// square root), or MANDEL_MAX_ITER if it never escaped (considered
// "in the set").
//
// z starts at 0; c = (cx, cy) is this pixel's coordinate. Standard
// z = z^2 + c update, expanded into real/imaginary parts:
//   zx' = zx^2 - zy^2 + cx
//   zy' = 2*zx*zy + cy
//
// No squaring-table (deliberately not used at all, see mandelbrot.h)
// -- this is the straightforward 3-multiplies-per-iteration version,
// just with the cardioid/bulb check above skipping the loop entirely
// for points that would otherwise run it to completion every time.
//
// xm/xm2/xp1_2/cy2: the cardioid/bulb check's already-hoisted terms
// (see mandel_in_cardioid_or_bulb()'s own comment) -- passed straight
// through, not recomputed here.
// ---------------------------------------------------------------
static unsigned char mandel_iterate(fixed_t cx, fixed_t cy, fixed_t xm, fixed_t xm2, fixed_t xp1_2, fixed_t cy2)
{
    fixed_t zx = 0, zy = 0;
    unsigned char i;

    if (mandel_in_cardioid_or_bulb(xm, xm2, xp1_2, cy2))
        return MANDEL_MAX_ITER;

    for (i = 0; i < MANDEL_MAX_ITER; i++)
    {
        fixed_t zx2 = fixed_sqr(zx);
        fixed_t zy2 = fixed_sqr(zy);

        // long, not fixed_t: zx2+zy2 can transiently approach the
        // Q5.11 range's own ceiling right at the escape threshold --
        // widen before adding to avoid 16-bit overflow at the compare.
        if ((long)zx2 + (long)zy2 > (long)FIXED4)
            return i;

        {
            fixed_t zxy = fixed_mul(zx, zy);
            zy = zxy + zxy + cy;   // 2*zx*zy + cy
            zx = zx2 - zy2 + cx;
        }
    }
    return MANDEL_MAX_ITER;
}

// Iteration count -> 4-bit color index, via a fixed lookup table (not
// a per-generation histogram remap -- that was tried and dropped, see
// git history/mandelbrot.h's own comment: it adapted to each view's
// own iteration distribution and looked worse, not better). 0 = in
// the set (black), 1-15 spread across escaping iteration counts (low
// count = escaped fast = far from the set = start of the gradient).
//
// Colors 1-14 went through two rounds already (see below); this third
// round (2026-09-12) extends the gradient into color 15 too, which
// used to be reserved exclusively for the zoom feature's corner
// markers and never emitted here -- see zoom.h's own
// ZOOM_MARKER_COLOR_INDEX comment for the current story on that
// tradeoff. Doing it this way (DDT/0x444454's own approach: "I don't
// reserve white either, I just pick the most white color of the
// iterations palette") needed the corner markers to survive landing on
// a genuinely white picture pixel -- an outlined marker (proven
// working on real hardware before, see zoom.c's own comment) would
// have solved that cleanly, but the shared upiccode/modcode/moddata/
// modbss pool has only 18 bytes free as of this build (see the
// project's own .map file), nowhere near enough to revive it. Accepted
// as a conscious, real (not hypothetical) risk instead: a marker can
// now land on a matching white pixel and be hard to spot there.
//
// Two earlier issues, still relevant to how this table is built:
// - A straight-line formula (color = 1 + iter*14/32) was uniform in
//   principle but, because 32 iteration counts don't divide evenly
//   into 14 colors, silently merged THREE different counts into a
//   single color at four separate points -- the first of those four
//   fell on colors 1 and 2, the two lowest, most common counts
//   covering most of any view's background. Real-world confirmation
//   (Lemon64 forum, DDT/0x444454): comparing against their own
//   reference renderer showed a visibly missing shade in exactly that
//   spot, and it explained an otherwise-puzzling earlier symptom too
//   -- isolated "noise island" pixels (from the since-fixed
//   fixed_sqr()/fixed_mul() overflow, see their own comments) that sat
//   in the middle of a supposedly flat color region rather than at a
//   visible boundary, because that region secretly spanned iter 0-2
//   merged into one color, so only the wrongly-computed pixels showed
//   any color change at all.
// - The replacement table first pushed every unavoidable multi-count
//   merge to the TOP of the range instead (reasoning the busy detail
//   band right at the fractal boundary would hide it better) -- DDT's
//   own follow-up, a contour overlay of this project's output against
//   mandelbr8's, showed that was wrong: clustering every merge point
//   in one place loses more real detail there than spreading them out
//   ever would.
//
// With a 15th usable color, 32 counts into 15 colors needs only two
// 3-count bins instead of four (32 = 13*2 + 2*3) -- spread evenly
// rather than bunched, same reasoning as before. Monotonic and uses
// every one of the 15 colors.
//
// This table only decides WHICH iteration counts share a color, not
// which RGB value each color holds -- that's each palette array's own
// concern below. White itself moved again shortly after this round
// (round 3, still 2026-09-12): each palette's own comment covers why,
// but in short, white is no longer color 15 specifically -- it's
// whichever color index every palette agrees to put its own peak at
// (index 8, chosen so `ZOOM_MARKER_COLOR_INDEX` can be one constant).
// The marker-collision tradeoff described above is unaffected by
// which numbered color white happens to be.
static const unsigned char mandel_color_table[MANDEL_MAX_ITER] = {
    1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  5,  6,  6,  7,  7,  8,
    8,  9,  9,  9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
};

static unsigned char mandel_color(unsigned char iter)
{
    if (iter >= MANDEL_MAX_ITER)
        return 0;
    return mandel_color_table[iter];
}

// One entry per row -- sized for the worst case (a full, non-mirrored
//256-row generation, see mandelbrot_generate()'s own comment on the
// symmetry check) rather than the 128 a symmetric view actually needs.
// 256 * 2 bytes = 512 bytes -- doesn't fit "main"'s own data/bss
// budget (same wall sq_table hit -- see its own comment above), placed
// in modbss alongside it instead (2026-09-10).
#pragma bss(modbss)
static fixed_t cy2_table[UPIC_HEIGHT];
#pragma bss(bss)


void mandelbrot_generate(void)
{
    unsigned bytecol, y;
    unsigned char symmetric;
    unsigned half_height;

    // Live build-up via upic_show_frame() once per column, tried
    // 2026-09-09, REVERTED same day: confirmed on real hardware as
    // persistent flicker even after forcing the inter-column border
    // color to black (see git history) -- render_frame() takes a fixed
    // ~20ms (one real PAL frame, NOT sped up by turbo, since it's
    // synced to the actual raster beam), but computing one column took
    // considerably longer than that (~129ms/column average at the
    // time), so every call was a brief flash of the picture-so-far
    // followed by a long static hold, regardless of the hold's color.
    // RETRIED, same day, after the multiply/row-mirroring speedups
    // below cut generation to ~8.7s total (~45ms/column average) --
    // still longer than one frame, but under half of what it was.
    // CONFIRMED on real hardware: still flickers, but the user
    // explicitly preferred watching the picture build over a static
    // wait, flicker included -- kept this way deliberately, not an
    // oversight. mandel_gen_mins/secs/tenths (see top of file) still
    // gives an objective measurement -- read back via
    // ultimate_read_memory after a run. A plain-text progress screen
    // (progress.c) was added for the earlier precalculate-then-show
    // design and removed once live rendering made it redundant (see
    // git history).
    //
    // Columns 0..UPIC_RELOC_COLS-1 -> upic_buffer_reloc[] ($E000),
    // the rest -> upic_buffer[] ($1800-territory) -- see
    // upic_viewer.h's own doc comment for why the picture is split
    // this way. Each byte-column covers 2 pixel columns (x=2*bytecol,
    // x=2*bytecol+1), packed low/high nibble -- see upic_viewer.h.
    //
    // Fixed on real hardware (2026-09-09, confirmed correct): the
    // original version of this loop -- indexing both buffers with a
    // fresh `bytecol * UPIC_HEIGHT + y` multiply-add every row, and
    // accumulating `cy` via `cy += mandel_dy` in the for-loop's own
    // increment clause -- produced a picture that repeated the same
    // ~16-row band down the whole 256-row height instead of a smooth
    // gradient. Root cause not fully isolated (Oscar64 codegen issue,
    // not an algorithm bug -- the same fixed-point math was
    // independently verified correct in Python first). This version --
    // column base pointer computed ONCE (a single multiply per column,
    // not per pixel), `cy` recomputed directly from `y` each row
    // (`mandel_y0 + y*mandel_dy`, not an accumulator) -- renders a
    // correct, full-detail Mandelbrot set on real hardware.
    cia1.todt = 0;
    cia1.tods = 0;
    cia1.todm = 0;

    // A view is mirror-eligible only if row 0 and row (HEIGHT-1) are
    // exact negatives of each other in Q5.11 -- true BY CONSTRUCTION
    // for the default view's mandel_y0 (see its own comment), but a
    // user-selected zoom target (zoom.c) generally will NOT satisfy
    // this (2026-09-10) -- most interesting zoom targets don't happen
    // to straddle the real axis. Checked at runtime rather than
    // assumed, so both cases stay correct through repeated zooms.
    symmetric = (mandel_y0 == (fixed_t)(-(((long)(UPIC_HEIGHT - 1) * mandel_dy) / 2)));
    half_height = symmetric ? (UPIC_HEIGHT / 2) : UPIC_HEIGHT;

    // cy^2 depends only on the row, not the column -- precompute it
    // once here instead of leaving mandel_in_cardioid_or_bulb() (via
    // mandel_iterate()) to recompute it from scratch for every one of
    // the 192 columns that share the same row (see that function's own
    // comment). Still worth doing regardless of symmetry -- only the
    // ROW COUNT (half_height) differs.
    for (y = 0; y < half_height; y++)
    {
        fixed_t cy = (fixed_t)(mandel_y0 + (long)y * mandel_dy);
        cy2_table[y] = fixed_sqr(cy);
    }

    for (bytecol = 0; bytecol < UPIC_WIDTH / 2; bytecol++)
    {
        fixed_t cx0 = (fixed_t)(mandel_x0 + (long)(bytecol * 2) * mandel_dx);
        fixed_t cx1 = (fixed_t)(cx0 + mandel_dx);
        volatile char *dst = (bytecol < UPIC_RELOC_COLS)
            ? &upic_buffer_reloc[(unsigned)bytecol * UPIC_HEIGHT]
            : &upic_buffer[(unsigned)(bytecol - UPIC_RELOC_COLS) * UPIC_HEIGHT];

        // Cardioid/bulb terms that depend only on this column's cx,
        // not on the row -- same hoisting idea as cy2_table above, the
        // other axis. Computed once per column (twice -- cx0 and cx1
        // are two separate pixel columns packed into this byte-column,
        // see upic_viewer.h) instead of once per pixel.
        fixed_t xm0    = (fixed_t)(cx0 - FIXED_ONE / 4);
        fixed_t xm0_2  = fixed_sqr(xm0);
        fixed_t xp10_2 = fixed_sqr((fixed_t)(cx0 + FIXED_ONE));
        fixed_t xm1    = (fixed_t)(cx1 - FIXED_ONE / 4);
        fixed_t xm1_2  = fixed_sqr(xm1);
        fixed_t xp11_2 = fixed_sqr((fixed_t)(cx1 + FIXED_ONE));

        // Only half_height rows are actually iterated -- when the
        // view is symmetric (see above), each row's result is also
        // mirrored straight into its exact opposite (HEIGHT-1-y),
        // roughly halving this loop's cost; otherwise half_height is
        // the full UPIC_HEIGHT and the mirror write below is skipped
        // (every row genuinely is unique data).
        for (y = 0; y < half_height; y++)
        {
            fixed_t cy = (fixed_t)(mandel_y0 + (long)y * mandel_dy);
            fixed_t cy2 = cy2_table[y];
            unsigned char even = mandel_color(mandel_iterate(cx0, cy, xm0, xm0_2, xp10_2, cy2));
            unsigned char odd  = mandel_color(mandel_iterate(cx1, cy, xm1, xm1_2, xp11_2, cy2));
            unsigned char packed = (unsigned char)((odd << 4) | even);
            dst[y] = packed;
            if (symmetric)
                dst[UPIC_HEIGHT - 1 - y] = packed;
        }

        // Live picture build-up -- see this function's own comment
        // above. Return value ignored here on purpose: SPACE exits the
        // FINAL display loop in main() (after this function returns),
        // not generation itself -- a SPACE press mid-generation is
        // just consumed/ignored by this call, same as any other key.
        upic_show_frame();
    }

    mandel_gen_tenths = cia1.todt;
    mandel_gen_secs   = cia1.tods;
    mandel_gen_mins   = cia1.todm;
}

// "Sunset": black -> indigo -> blue -> pale -> gold -> orange -> deep
// red. This is the ORIGINAL default's own theme (2026-09-12: briefly
// swapped out for a blue-to-white gradient in the same session, then
// reverted -- see git history/CREDITS.md if that detour matters to
// future-you), redesigned twice over since:
//
// 1) The original version's first 9 entries turned out to be an almost
//    exact copy of what was then mandel_palette_ice's own blue ramp
//    (both hand-picked separately, never compared side by side) --
//    since the fractal's exterior "lake" is dominated by exactly these
//    low-iteration/early-index colors, the two palettes looked nearly
//    identical for most of the picture. Fixed by widening this
//    gradient's own hue path (an indigo/violet-leaning start rather
//    than pure navy blue) and giving the other palette a genuinely
//    different theme instead (see mandel_palette_amethyst below).
// 2) Index 1 was only ~30-60 RGB units from pure black -- barely
//    perceptible as a distinct step from "in the set" on real
//    hardware/video capture, the same class of problem as the pale-end
//    crowding below, just at the other end (forum feedback: "I count
//    11 shades, shouldn't that be 14?" -- confirmed by mapping a real
//    screenshot's pixels to nearest palette index: indices 1-2 were
//    both under 1% of any visible pixels, i.e. present but essentially
//    invisible). Every step here, including both boundaries against
//    true black (0->1 and 15->0, see point 3 below for why 15 borders
//    black and not 8), is now a deliberately-checked >=37 RGB units
//    apart (most are 50-330) -- verified by generating swatch renders
//    and measuring adjacent distances, not eyeballed.
// 3) White moved from a boundary endpoint to a genuine mid-gradient
//    peak (2026-09-12, user request/round 2): reaching this gradient's
//    own brightest point used to happen right where a picture pixel
//    borders the true black interior, giving a stark white-to-black
//    edge everywhere the fractal boundary is -- not the "brightest a
//    little in from the edge, dimming again right at the boundary"
//    look a sunset (or any of these four themes) actually wants. Also
//    settled where every gradient's own peak lands: the SAME index
//    (8) in all four, so `ZOOM_MARKER_COLOR_INDEX` (zoom.h) can be one
//    constant instead of varying per palette. For this gradient
//    specifically, that meant reshaping into two different arcs either
//    side of index 8 -- cool ascent (indigo/blue/pale, indices 1-7)
//    up to white, then a DIFFERENT warm descent (gold/orange/red,
//    indices 9-15) back down toward black, preserving the original
//    "sunset" identity (brightest sky glow, then the sun's own red
//    disc, not a monotonic white climb) instead of mirroring the same
//    arc both ways. Built from two keyframe sequences resampled at
//    equal RGB arc-length, same technique as every gradient below.
const char mandelbrot_palette[48] = {
    0x00,0x00,0x00,    //  0: black (in the set)
    0x19,0x04,0x27,    //  1
    0x1d,0x22,0x5e,    //  2
    0x1d,0x48,0x8d,    //  3
    0x42,0x73,0xab,    //  4
    0x6b,0x9c,0xc5,    //  5
    0x9a,0xc4,0xd7,    //  6
    0xce,0xe5,0xe3,    //  7
    0xff,0xff,0xff,    //  8: this gradient's own peak, also the zoom feature's corner-marker color (ZOOM_MARKER_COLOR_INDEX)
    0xf4,0xe8,0xc6,    //  9
    0xff,0xd1,0x8c,    // 10
    0xff,0xb5,0x54,    // 11
    0xfc,0x86,0x2c,    // 12
    0xdd,0x52,0x18,    // 13
    0xb6,0x28,0x09,    // 14
    0x7a,0x15,0x05,    // 15
};

// Black -> deep red -> orange -> gold -> white (2026-09-12, round 2:
// symmetric "mountain" shape peaking at index 8, same shared-peak-index
// reasoning as mandelbrot_palette's own comment above) -> back down
// through gold -> orange -> deep red before black. A literal mirror of
// its own ascending arc (indices 9-15 = indices 7-1 in reverse) --
// unlike mandelbrot_palette's two-different-arcs design, a symmetric
// "hot core cooling back to embers on both sides" reads correctly for
// fire specifically, and guarantees identical gap sizes on both slopes
// by construction. Same index-1-too-close-to-black issue as
// mandelbrot_palette had, fixed the same way (every step >=28 RGB
// units apart, including 0->1 and 15->0).
const char mandel_palette_fire[48] = {
    0x00,0x00,0x00,    //  0: black (in the set)
    0x24,0x00,0x00,    //  1
    0x77,0x07,0x00,    //  2
    0xc5,0x26,0x00,    //  3
    0xf2,0x68,0x00,    //  4
    0xff,0xb7,0x10,    //  5
    0xff,0xdb,0x5b,    //  6
    0xff,0xed,0xad,    //  7
    0xff,0xff,0xff,    //  8: white-hot -- this gradient's own peak, also the zoom feature's corner-marker color
    0xff,0xed,0xad,    //  9
    0xff,0xdb,0x5b,    // 10
    0xff,0xb7,0x10,    // 11
    0xf2,0x68,0x00,    // 12
    0xc5,0x26,0x00,    // 13
    0x77,0x07,0x00,    // 14
    0x24,0x00,0x00,    // 15
};

// "Amethyst": black -> deep violet -> vivid magenta -> hot pink ->
// white (2026-09-12, round 2: symmetric mountain shape peaking at
// index 8, mirrored back down through pink -> magenta -> violet before
// black -- same reasoning as mandel_palette_fire's own comment above,
// a bright gem-flash surrounded symmetrically by deepening violet on
// both sides). Replaced the teal/mint "glacier" gradient (2026-09-12,
// round 1, same session) on direct feedback: glacier read as flat/
// boring next to the others, and an earlier from-scratch purple/
// magenta design (also called "amethyst" before it got swapped out for
// glacier) was preferred -- brought back, pushed further from its
// muted violet start toward more saturated, brighter magenta/pink
// throughout per that feedback ("move from the purples to the more
// brights"). Built from keyframes resampled at equal RGB arc-length so
// the color spread stays even regardless of how the hue curves --
// every interior step is >=62 RGB units from its neighbor. Index 1 was
// deliberately placed to stay clear of both mandelbrot_palette's
// indigo start and mandel_palette_fire's pure-red start at once (their
// shared dark corner leaves little room: pushing further from one
// pushes closer to the other) -- best achievable while still reading
// as violet/magenta rather than navy or maroon was ~32 RGB units from
// each.
const char mandel_palette_amethyst[48] = {
    0x00,0x00,0x00,    //  0: black (in the set)
    0x37,0x00,0x32,    //  1
    0x68,0x00,0x62,    //  2
    0x9f,0x00,0x8c,    //  3
    0xd5,0x0f,0xa8,    //  4
    0xfe,0x46,0xa7,    //  5
    0xff,0x87,0xbc,    //  6
    0xff,0xc1,0xe1,    //  7
    0xff,0xff,0xff,    //  8: this gradient's own peak, also the zoom feature's corner-marker color
    0xff,0xc1,0xe1,    //  9
    0xff,0x87,0xbc,    // 10
    0xfe,0x46,0xa7,    // 11
    0xd5,0x0f,0xa8,    // 12
    0x9f,0x00,0x8c,    // 13
    0x68,0x00,0x62,    // 14
    0x37,0x00,0x32,    // 15
};

// Classic fractal-viewer rainbow band: red -> orange -> yellow ->
// green -> cyan, white flash, blue -> violet -> magenta -> pink,
// back toward red. The "white cap" this array always ended on
// (comment predates the 2026-09-12 palette work) moved to a genuine
// mid-gradient flash at index 8 instead (round 2, shared-peak-index
// reasoning -- see mandelbrot_palette's own comment above), splitting
// the original 14 fully-saturated hues into two 7-color arcs on either
// side rather than mirroring or reshaping either arc -- the full
// original hue sequence survives unchanged, just interrupted once by
// white in the middle, exactly like a literal rainbow's own bright
// central flash. Every other color stays a fully-saturated hue by
// design (unlike the other three gradients, this one doesn't
// monotonically brighten toward either end -- it cycles through hues
// at roughly constant brightness on both sides of the flash).
const char mandel_palette_rainbow[48] = {
    0x00,0x00,0x00,    //  0: black (in the set)
    0xff,0x00,0x00,    //  1
    0xff,0x60,0x00,    //  2
    0xff,0xc0,0x00,    //  3
    0xd0,0xff,0x00,    //  4
    0x60,0xff,0x00,    //  5
    0x00,0xff,0x40,    //  6
    0x00,0xff,0xc0,    //  7
    0xff,0xff,0xff,    //  8: mid-cycle white flash, also the zoom feature's corner-marker color
    0x00,0xc0,0xff,    //  9
    0x00,0x60,0xff,    // 10
    0x40,0x00,0xff,    // 11
    0xa0,0x00,0xff,    // 12
    0xff,0x00,0xe0,    // 13
    0xff,0x00,0x80,    // 14
    0xff,0x40,0x40,    // 15
};

// Selectable gradients -- see mandelbrot.h's own comment. An array of
// pointers rather than a 2D array so mandelbrot_palette itself stays
// its own named symbol (main.c already references it directly) while
// also being reachable through this table for cycling.
const char *const mandel_palettes[MANDEL_PALETTE_COUNT] = {
    mandelbrot_palette,
    mandel_palette_fire,
    mandel_palette_amethyst,
    mandel_palette_rainbow,
};
