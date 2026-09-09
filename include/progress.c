/*****************************************************************
Mandelbrot Upic -- welcome/progress screen (implementation)
See progress.h for API documentation and why this uses plain POKEs.
******************************************************************/

#include "progress.h"

#define SCREEN_BASE ((volatile char *)0x0400)
#define COLOR_BASE  ((volatile char *)0xD800)
#define VIC_BORDER  (*(volatile unsigned char *)0xD020)
#define VIC_BG      (*(volatile unsigned char *)0xD021)

#define BAR_ROW   14
#define BAR_COL   5
#define BAR_WIDTH 30
#define PCT_ROW   14
#define PCT_COL   (BAR_COL + BAR_WIDTH + 2)

// How much of the bar is already filled -- tracked across calls so
// progress_update() only needs to POKE newly-filled cells, not redraw
// the whole bar every time.
static unsigned char progress_filled = 0;

// Screen-code conversion for direct POKE text -- standard unshifted
// C64 charset: space..'?' (0x20-0x3F, includes digits and '%') share
// the same code as ASCII; 'A'-'Z' map to screen codes 1-26
// (ASCII - 0x40). Only letters need the translation below.
static void screen_puts(unsigned char row, unsigned char col, unsigned char color, const char *s)
{
    volatile char *scr = SCREEN_BASE + (unsigned)row * 40 + col;
    volatile char *clr = COLOR_BASE + (unsigned)row * 40 + col;
    unsigned char c;

    while ((c = (unsigned char)*s++) != 0)
    {
        if (c >= 'A' && c <= 'Z')
            c = (unsigned char)(c - 'A' + 1);
        *scr++ = (char)c;
        *clr++ = (char)color;
    }
}

void progress_init(void)
{
    unsigned i;

    VIC_BORDER = 0;
    VIC_BG = 0;
    for (i = 0; i < 1000; i++)
    {
        SCREEN_BASE[i] = 0x20;
        COLOR_BASE[i] = 14;   // light blue
    }

    screen_puts(4, 12, 1, "MANDELBROT UPIC");
    screen_puts(6, 3, 14, "GENERATING FRACTAL AT 64MHZ...");
    screen_puts(BAR_ROW, BAR_COL - 1, 1, "[");
    screen_puts(BAR_ROW, BAR_COL + BAR_WIDTH, 1, "]");
    screen_puts(PCT_ROW, PCT_COL, 1, "  0%");

    progress_filled = 0;
}

void progress_update(unsigned char done, unsigned char total)
{
    unsigned char filled = (unsigned char)(((unsigned)done * BAR_WIDTH) / total);
    unsigned char pct = (unsigned char)(((unsigned)done * 100) / total);
    unsigned char h = (unsigned char)(pct / 100);
    unsigned char t = (unsigned char)((pct / 10) % 10);
    unsigned char u = (unsigned char)(pct % 10);
    volatile char *p;

    while (progress_filled < filled)
    {
        SCREEN_BASE[(unsigned)BAR_ROW * 40 + BAR_COL + progress_filled] = (char)0xA0;  // reverse-space: solid block
        COLOR_BASE[(unsigned)BAR_ROW * 40 + BAR_COL + progress_filled] = 5;            // green
        progress_filled++;
    }

    p = SCREEN_BASE + (unsigned)PCT_ROW * 40 + PCT_COL;
    p[0] = h ? (char)('0' + h) : ' ';
    p[1] = (h || t) ? (char)('0' + t) : ' ';
    p[2] = (char)('0' + u);
    p[3] = '%';
}
