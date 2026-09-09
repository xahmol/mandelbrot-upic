/*****************************************************************
Ultimate 64 Turbo Control Library

Targets the U64-specific $D031 turbo speed register.
Firmware menu must have "Turbo Mode" set to "U64 Turbo Registers"
for detection to work correctly.

Detection method — CIA TOD timing with deliberate loop overhead:
  turbo_detect() calls benchmark_delay() which uses CIA1 TOD
  (Time Of Day) as a real-time reference.  The measured function
  runs a deliberately unoptimised double loop (#pragma optimize(0),
  volatile int variables, __noinline) so each iteration takes many
  more CPU cycles than a typical optimised loop.  This makes the
  loop long enough in real time for CIA TOD tenths to advance even
  at turbo speed.  The result (tenths of a second) is compared
  against empirical thresholds to classify the speed class.

Note: simple CIA timer B or VIC raster measurements do NOT work on
U64 because both are clocked at the CPU frequency — they track CPU
cycles, not real time.  CIA TOD advances at real 50/60 Hz when the
deliberately slow (unoptimised) loop runs long enough.

Supported hardware:
  Ultimate 64 original / Elite I  — max ~48 MHz
  Ultimate 64 Elite II / C64U     — max ~64 MHz
******************************************************************/

#ifndef _TURBO_H_
#define _TURBO_H_

// ---------------------------------------------------------------
// Detection result constants
// ---------------------------------------------------------------

#define TURBO_NOT_PRESENT  0
// $D031 reads $FF (no U64, or turbo registers not enabled in
// firmware), OR speed index == 0 with $D030 bit 0 clear (1 MHz,
// turbo not active).

#define TURBO_48MHZ        1
// Turbo active at speed index 0x01–0x0E (any speed below max).
// Also returned when $D031 == 0 but $D030 bit 0 is set
// (Turbo-Enable-Bit mode).

#define TURBO_64MHZ        2
// Speed index == 0x0F (maximum).  Assumed to be 64 MHz on
// Elite-II / C64U; will show "64 MHz" on Elite-I (48 MHz) too.

// ---------------------------------------------------------------
// $D031 control byte composition
//
//   bits 0-3 : speed index  (0 = 1 MHz, 15 = max)
//   bit  7   : badline mask (0 = normal stalls, 1 = suppress)
//   bits 4-6 : reserved, write 0
// ---------------------------------------------------------------

#define TURBO_SPEED_1MHZ    0x00   // Standard 1 MHz
#define TURBO_SPEED_2MHZ    0x01
#define TURBO_SPEED_3MHZ    0x02
#define TURBO_SPEED_4MHZ    0x03
#define TURBO_SPEED_6MHZ    0x04
#define TURBO_SPEED_8MHZ    0x05
#define TURBO_SPEED_12MHZ   0x06
#define TURBO_SPEED_16MHZ   0x07
#define TURBO_SPEED_20MHZ   0x08
#define TURBO_SPEED_24MHZ   0x09
#define TURBO_SPEED_28MHZ   0x0A
#define TURBO_SPEED_32MHZ   0x0B
#define TURBO_SPEED_36MHZ   0x0C
#define TURBO_SPEED_40MHZ   0x0D
#define TURBO_SPEED_48MHZ   0x0E
#define TURBO_SPEED_MAX     0x0F   // Highest supported by this hardware

#define TURBO_BADLINES_ON   0x00   // Normal VIC-II badline CPU stalls
#define TURBO_BADLINES_OFF  0x80   // Suppress badline stalls

// Convenience: full-speed control byte
#define TURBO_FULL  (TURBO_SPEED_MAX | TURBO_BADLINES_OFF)

// ---------------------------------------------------------------
// benchmark_delay() calibration constants
//
// ITERS: outer loop count for benchmark_delay().
// THRESHOLD_FAST: elapsed tenths of a second below which the CPU
//   is classified as running at 64 MHz (very fast turbo).
// THRESHOLD_SLOW: elapsed tenths of a second at or above which
//   the CPU is running at ~1 MHz (no turbo or turbo disabled).
//   Values between THRESHOLD_FAST and THRESHOLD_SLOW indicate
//   ~48 MHz turbo.
// ---------------------------------------------------------------
#define ITERS            1000
#define THRESHOLD_FAST      2   // < 2 tenths → 64 MHz
#define THRESHOLD_SLOW     70   // ≥ 70 tenths → no turbo / 1 MHz

// ---------------------------------------------------------------
// Function prototypes
// ---------------------------------------------------------------

int benchmark_delay(int iters);
/*
  Run a deliberately slow CPU loop and return elapsed time in
  CIA1 TOD tenths of a second (10ths; range 0–99 for <10 s).

  The function uses #pragma optimize(0) and __noinline with
  volatile int loop counters and inline __asm{nop} to prevent
  the compiler from shortening the loop.  This makes each
  iteration take significantly more CPU cycles than optimised
  code, which gives CIA TOD enough time to advance.

  Resets CIA1 TOD to 00:00.0 on entry and reads it on exit.
  SEI/CLI wraps the measurement.
*/

char turbo_detect(void);
/*
  Detect turbo status via CIA TOD timing.

  Sets CPU to maximum speed, then calls benchmark_delay(ITERS)
  twice — once to stabilise, once to measure.  The elapsed time
  (CIA1 TOD tenths) is compared against thresholds:

  Returns:
    TURBO_NOT_PRESENT  — elapsed ≥ THRESHOLD_SLOW (≈1 MHz)
    TURBO_48MHZ        — THRESHOLD_FAST ≤ elapsed < THRESHOLD_SLOW
    TURBO_64MHZ        — elapsed < THRESHOLD_FAST (very fast)

  Restores $D031 to 1 MHz after measuring.
  Call once at startup; takes a few seconds at 1 MHz.
*/

void turbo_set(char control);
/*
  Write `control` directly to $D031 and enable via $D030.

  `control` = speed_index | badlines_flag, e.g.:
      turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF);
      turbo_set(TURBO_FULL);
      turbo_set(TURBO_SPEED_1MHZ);

  Change takes effect on the very next CPU cycle.
*/

void turbo_fast(void);
// Shorthand: turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF).

void turbo_slow(void);
// Shorthand: turbo_set(TURBO_SPEED_1MHZ).

unsigned char turbo_get(void);
// Read the current $D031 value (0xFF if registers not available).

#pragma compile("turbo.c")

#endif
