/*****************************************************************
Ultimate 64 Turbo Control Library — implementation
See turbo.h for API documentation.
******************************************************************/

#include "turbo.h"
#include <c64/cia.h>

// ---------------------------------------------------------------
// Register addresses
// ---------------------------------------------------------------
#define TURBO_D030  (*(volatile unsigned char *)0xD030)
#define TURBO_D031  (*(volatile unsigned char *)0xD031)

#pragma optimize(0);
// Measues in CIA TOD time units (1/10th of a second).  With turbo off, 1500 iters ≈ 1 second.
// Input: iters = number of loop iterations to burn CPU cycles.
// Output: elapsed time in 1/10ths of a second.  With turbo off, result ≈ iters / 1500.
__noinline int benchmark_delay(int iters)
{
    volatile int i,j;
    __asm{sei};
    cia1.tods = 0;
    cia1.todt = 0;
    for (i = 0; i < iters; i++)
    {
        // Burn CPU cycles in a way that won't be optimized out.
        // The loop overhead is negligible compared to the delay from the iterations.
        for(j = 0; j < 200; j++)
        {
            __asm { nop }
        }
    }
    __asm{cli};

    return cia1.tods*10 + cia1.todt;
}
#pragma optimize(1);

// ---------------------------------------------------------------
// turbo_detect
//
// Measures CPU speed via CIA1 TOD timing using benchmark_delay().
// See turbo.h for threshold definitions and TURBOCONTROLMANUAL.md
// for a full explanation of the detection method.
// ---------------------------------------------------------------
char turbo_detect(void)
{
    unsigned int elapsed;

    if (TURBO_D031 == (char)0xFF) return TURBO_NOT_PRESENT;

    // Set to max speed
    turbo_set(TURBO_SPEED_MAX);

    // Do first delay to give Ultimate firmware time to apply the new speed setting and stabilize.
    benchmark_delay(ITERS);

    // Do second delay to measure the speed.  The elapsed time will be much lower with a real turbo than without.
    elapsed = benchmark_delay(ITERS);

    // Restore original speed settings to avoid side effects.
    turbo_set(TURBO_SPEED_1MHZ);

    // Interpret results.  With turbo off, elapsed should be around 60–70 ticks per 1000 iterations.
    if(elapsed < THRESHOLD_FAST) {
        return TURBO_64MHZ;
    } else if (elapsed < THRESHOLD_SLOW) {
        return TURBO_48MHZ;
    }

    return TURBO_NOT_PRESENT;
}

// ---------------------------------------------------------------
// turbo_set — sets speed via $D030 + $D031 (both firmware modes)
//
// Placed in the shared $E000 modcode region -- only valid once
// rombank_out() has run (see rombank.h), which is the very first
// action of the demo's lifetime. I/O at $D000-$DFFF is unaffected
// by ROM banking, so this is safe regardless of bank state as long
// as it's never called before that first rombank_out().
// ---------------------------------------------------------------
#pragma section(modcode, 0)
#pragma code(modcode)
void turbo_set(char control)
{
    if (control == TURBO_SPEED_1MHZ) {
        TURBO_D031 = control;
        TURBO_D030 &= ~0x01;
    } else {
        TURBO_D030 |= 0x01;
        TURBO_D031 = control;
    }
}
#pragma code(code)

void turbo_fast(void) { turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF); }
void turbo_slow(void) { turbo_set(TURBO_SPEED_1MHZ); }

unsigned char turbo_get(void) { return TURBO_D031; }
