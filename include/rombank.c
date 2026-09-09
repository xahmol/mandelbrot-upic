/*****************************************************************
Mandelbrot Upic -- shared permanent ROM-banking setup
See rombank.h for API documentation.
******************************************************************/

#include <oscar.h>
#include <c64/memmap.h>
#include "rombank.h"

static char rombank_ready = 0;

void rombank_out(void)
{
    if (!rombank_ready)
    {
        mmap_trampoline();
        mmap_set(MMAP_NO_ROM);
        rombank_ready = 1;
    }
}

void rombank_restore(void)
{
    mmap_set(MMAP_ROM);
    rombank_ready = 0;
}
