# Turbo Control Library Manual

**Ultimate 64 CPU Speed Control for Oscar64**

Library files: `include/turbo.h` / `include/turbo.c`

---

## Contents

1. [Overview](#1-overview)
2. [Hardware Background](#2-hardware-background)
3. [Firmware Prerequisites](#3-firmware-prerequisites)
4. [Register Reference](#4-register-reference)
5. [Constants and Defines](#5-constants-and-defines)
6. [Function Reference](#6-function-reference)
7. [Detection Method](#7-detection-method)
8. [Typical Usage Patterns](#8-typical-usage-patterns)
9. [Limitations and Known Issues](#9-limitations-and-known-issues)

---

## 1. Overview

The turbo control library provides a simple API to:

- **Detect** whether the U64 turbo speed registers are active and classify the speed.
- **Set** any of the 16 speed steps from 1 MHz to the hardware maximum.
- **Get** the current speed register value.

The primary control register is `$D031`. Detection is measurement-based: `turbo_detect()` ramps the CPU to maximum speed, runs a calibrated timing loop, and measures elapsed real time via CIA1 TOD (Time Of Day), which advances at the real 50/60 Hz mains frequency regardless of CPU speed. See §7 for the full method.

---

## 2. Hardware Background

### Ultimate 64 CPU speed

The Ultimate 64 (U64) FPGA replaces the C64's 6510 CPU with a re-implementation that can run at higher clock multiples. The actual maximum depends on the hardware revision:

| Hardware | Maximum CPU speed |
|----------|------------------|
| Ultimate 64 (original) | ~48 MHz |
| Ultimate 64 Elite I | ~48 MHz |
| Ultimate 64 Elite II | ~64 MHz |
| C64U | ~64 MHz |

The FPGA presents a standard 6510-compatible 8-bit CPU to software; timing-sensitive code (SID register timing, raster IRQs, etc.) must be rewritten for turbo speeds.

### Why most timers do not work for speed detection on U64

CIA1 timer A and B, and the VIC-II raster counter, are clocked at the CPU frequency on U64.  A tight benchmark loop always takes the same number of timer ticks regardless of turbo setting — the ratio is 1:1 and carries no speed information.

**CIA1 TOD (Time Of Day) does work.** TOD advances at the real 50/60 Hz mains frequency, independent of the CPU clock.  A deliberately slow, unoptimised loop (`benchmark_delay()`) runs long enough in real time for TOD tenths to accumulate, even at turbo speed.  See §7 for the full method.

### Badlines

At 1 MHz the VIC-II "steals" the CPU bus for ~40 cycles every 8 raster lines (badlines). Setting bit 7 of `$D031` suppresses badline CPU stalls. Strongly recommended when running at turbo speed. The VIC-II continues to render normally; only the CPU stall is removed.

---

## 3. Firmware Prerequisites

The U64 firmware exposes turbo control through a **menu setting**:

**Menu path:** `F2` → `C64 and cartridge settings` → `Turbo Mode`

| Setting | Behaviour |
|---------|-----------|
| `Off` | Turbo disabled; `$D031` reads `$FF` |
| `Turbo Enable Bit` | `$D030` bit 0 enables turbo at the firmware-configured speed |
| `U64 Turbo Registers` | `$D031` bits 0–3 directly set the speed index |

**Recommended: "U64 Turbo Registers" mode.**

In "Turbo Enable Bit" mode, `$D031` reads as `0x00` and the speed is controlled by `$D030` bit 0 + the firmware CPU Speed menu. `turbo_detect()` handles this case by checking `$D030` bit 0 as a fallback when `$D031 == 0x00`.

---

## 4. Register Reference

### `$D031` — U64 Turbo Control (read/write)

| Bits | Name | Description |
|------|------|-------------|
| 3–0 | Speed index | 0 = 1 MHz, 1–14 = intermediate speeds, 15 = maximum |
| 6–4 | Reserved | Write 0 |
| 7 | Badline mask | 0 = normal VIC-II badline CPU stalls; 1 = suppress |

**Detection:** Reading `$D031` returns `$FF` when turbo registers are unavailable. Any non-`$FF` value confirms U64 turbo registers are present.

**Speed index approximate frequencies (both Elite-I and Elite-II):**

| Index | Approximate speed |
|-------|------------------|
| 0 | 1 MHz |
| 1–13 | 2–40 MHz (intermediate) |
| 14 | 48 MHz |
| 15 | 48 MHz (Elite I) or 64 MHz (Elite II / C64U) |

Index 15 is the hardware maximum. On Elite-I this is ~48 MHz; on Elite-II / C64U it is ~64 MHz. Software cannot distinguish these cases from the register value alone — both report index 15.

### `$D030` — Turbo Enable Bit (conditional)

| Bit | Description |
|-----|-------------|
| 0 | In "Turbo Enable Bit" firmware mode: 1 = enable turbo at firmware-configured speed |

In "U64 Turbo Registers" mode, `$D030` reads as `$FF` (open bus) and bit 0 is always 1.

---

## 5. Constants and Defines

All constants are in `include/turbo.h`.

### Detection result constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `TURBO_NOT_PRESENT` | 0 | Turbo not active ($D031==$FF, or speed index 0 with $D030 bit 0 clear) |
| `TURBO_48MHZ` | 1 | Turbo active at speed index 0x01–0x0E, or Turbo-Enable-Bit mode |
| `TURBO_64MHZ` | 2 | Speed index 0x0F (maximum — assumed 64 MHz on Elite-II/C64U) |

### Speed index constants

| Constant | `$D031` bits 0–3 | Speed |
|----------|-----------------|-------|
| `TURBO_SPEED_1MHZ` | `0x00` | 1 MHz |
| `TURBO_SPEED_2MHZ` | `0x01` | 2 MHz |
| … | … | … |
| `TURBO_SPEED_48MHZ` | `0x0E` | 48 MHz |
| `TURBO_SPEED_MAX` | `0x0F` | Hardware maximum |

### Badline and convenience constants

| Constant | Value | Effect |
|----------|-------|--------|
| `TURBO_BADLINES_ON` | `0x00` | Normal badline stalls |
| `TURBO_BADLINES_OFF` | `0x80` | Suppress badline stalls |
| `TURBO_FULL` | `0x8F` | `TURBO_SPEED_MAX \| TURBO_BADLINES_OFF` |

### Detection calibration constants

| Constant | Value | Description |
|----------|-------|-------------|
| `ITERS` | 1000 | Outer loop iteration count passed to `benchmark_delay()` |
| `THRESHOLD_FAST` | 2 | Elapsed tenths below which the CPU is classified as 64 MHz |
| `THRESHOLD_SLOW` | 70 | Elapsed tenths at or above which the CPU is classified as 1 MHz (no turbo) |

Values between `THRESHOLD_FAST` and `THRESHOLD_SLOW` classify as `TURBO_48MHZ`. If `turbo_detect()` misclassifies your hardware, increase `ITERS` to widen the measured range, then adjust the thresholds to bracket the observed values.

---

## 6. Function Reference

### `benchmark_delay`

```c
int benchmark_delay(int iters);
```

Run a deliberately slow CPU loop and return elapsed time in CIA1 TOD tenths of a second. Used internally by `turbo_detect()`.

The loop uses `#pragma optimize(0)` and `__noinline` with `volatile int` counters and inline `nop` instructions to prevent compiler optimisation. Each of the `iters × 200` iterations takes far more CPU cycles than optimised code, giving CIA TOD enough real time to advance.

Resets CIA1 TOD to `00:00.0` on entry and reads it on exit. Wraps with SEI/CLI.

**Direct use:** call `benchmark_delay(ITERS)` and print the return value to calibrate `THRESHOLD_FAST` and `THRESHOLD_SLOW` for your hardware.

---

### `turbo_detect`

```c
char turbo_detect(void);
```

Detect turbo status via CIA TOD timing. Sets CPU to maximum speed, then calls `benchmark_delay(ITERS)` twice — once to let the firmware clock stabilise, once to measure. The elapsed tenths are compared against the calibration thresholds.

**Returns:** `TURBO_NOT_PRESENT`, `TURBO_48MHZ`, or `TURBO_64MHZ`.

| Result | Condition |
|--------|-----------|
| `TURBO_64MHZ` | elapsed < `THRESHOLD_FAST` (2 tenths — very fast turbo) |
| `TURBO_48MHZ` | `THRESHOLD_FAST` ≤ elapsed < `THRESHOLD_SLOW` (intermediate turbo) |
| `TURBO_NOT_PRESENT` | elapsed ≥ `THRESHOLD_SLOW` (70 tenths — running at ~1 MHz) |

Restores `$D031` to 1 MHz after measuring. Call once at startup; at 1 MHz the two benchmark passes take a few seconds. See §7 for full method description and threshold calibration.

---

### `turbo_set`

```c
void turbo_set(char control);
```

Write a control byte to `$D031` and enable via `$D030`.

```c
turbo_set(TURBO_FULL);                           // max speed, no badlines
turbo_set(TURBO_SPEED_24MHZ | TURBO_BADLINES_ON);// 24 MHz, keep badlines
turbo_set(TURBO_SPEED_1MHZ);                     // 1 MHz
```

Speed change takes effect on the very next CPU cycle.

---

### `turbo_fast`

```c
void turbo_fast(void);
```

Shorthand: `turbo_set(TURBO_SPEED_MAX | TURBO_BADLINES_OFF)`.

---

### `turbo_slow`

```c
void turbo_slow(void);
```

Shorthand: `turbo_set(TURBO_SPEED_1MHZ)`.

---

### `turbo_get`

```c
unsigned char turbo_get(void);
```

Read the current `$D031` value. Returns `0xFF` if registers unavailable.

---

## 7. Detection Method

### Overview

`turbo_detect()` sets the CPU to maximum speed, then runs `benchmark_delay(ITERS)` twice (once to let the firmware stabilise, once to measure). The result is compared against empirical thresholds:

| Result | Condition |
|--------|-----------|
| `TURBO_64MHZ` | elapsed < `THRESHOLD_FAST` (2 tenths, < 0.2 s) |
| `TURBO_48MHZ` | `THRESHOLD_FAST` ≤ elapsed < `THRESHOLD_SLOW` |
| `TURBO_NOT_PRESENT` | elapsed ≥ `THRESHOLD_SLOW` (70 tenths, ≥ 7 s) |

### Why simple timers do not work on U64

CIA timer B and VIC raster counter are both clocked at the CPU frequency on U64: they track CPU *cycles*, not real time.  A tight benchmark loop always takes the same number of timer ticks regardless of turbo setting, making them useless for speed measurement.

### Why CIA TOD timing does work

CIA1 TOD (Time Of Day) advances at the real 50/60 Hz mains rate.  The key is that the measured loop must run long enough in *real time* for TOD tenths to accumulate.

`benchmark_delay()` uses `#pragma optimize(0)` and `__noinline` with `volatile int` loop variables.  This forces the compiler to produce heavy unoptimised 6502 code for the loop body — each of the 200,000 iterations (1000 outer × 200 inner) takes far more CPU cycles than optimised code.  At turbo speed the loop completes in a fraction of a second; at 1 MHz it takes several seconds.  CIA TOD therefore advances measurably during the loop at any speed.

### Threshold calibration

The defaults (`ITERS=1000`, `THRESHOLD_FAST=2`, `THRESHOLD_SLOW=70`) are calibrated for the Ultimate 64 Elite-II.  If `turbo_detect()` misclassifies your hardware, change `ITERS` (more iterations → larger elapsed values, easier to separate) or adjust the thresholds to bracket your measured values.

To inspect raw values, call `benchmark_delay(ITERS)` directly and print the return value.

---

## 8. Typical Usage Patterns

### Basic detect-and-run

```c
#include "turbo.h"

void main(void) {
    char cls = turbo_detect();
    if (cls != TURBO_NOT_PRESENT) {
        turbo_fast();
        run_demo();
        turbo_slow();
    } else {
        show_error_no_turbo();
    }
}
```

### Speed-adaptive code paths

```c
char cls = turbo_detect();
if (cls == TURBO_64MHZ) {
    // 64 MHz path
} else if (cls == TURBO_48MHZ) {
    // 48 MHz / intermediate turbo path
} else {
    // 1 MHz fallback
}
```

### Selective turbo (computation fast, I/O at 1 MHz)

```c
void update_frame(void) {
    turbo_fast();
    compute_effects();

    turbo_slow();          // drop before timing-sensitive I/O
    update_sid();

    turbo_fast();
    render_screen();
    turbo_slow();
}
```

### Speed label from $D031 index

```c
unsigned char idx = turbo_get() & 0x0F;
// idx 0x0F → "64 MHz", 0x0E → "48 MHz", 0x09 → "24 MHz", etc.
```

---

## 9. Limitations and Known Issues

### Firmware setting dependency

- **Off**: `$D031 == $FF` → `TURBO_NOT_PRESENT`.
- **Turbo Enable Bit**: `$D031 == 0x00`; detection uses `$D030` bit 0 fallback.
- **U64 Turbo Registers**: full support, recommended.

### 48 vs 64 MHz indistinguishable from software

Speed index `0x0F` is the maximum on all U64 variants but maps to different absolute frequencies. `TURBO_64MHZ` is reported as a best-effort classification. UCI hardware info (`uii_get_hwinfo`) may help distinguish variants at the application level.

### Speed changes are instantaneous

`turbo_set()` takes effect on the very next cycle. Arrange timing-critical code so `turbo_slow()` is called *before* entering the critical section.

### Non-U64 hardware

On standard C64/C128/SuperCPU, `$D031 == $FF` → `TURBO_NOT_PRESENT` immediately.
