# Mandelbrot Upic

A Commodore 64 Ultimate demo that generates a Mandelbrot fractal
on-device at 64 MHz turbo, packs it directly into Upic format (a
16-color, 384x256 border-color raster picture technique), and displays
it live.

Spun off from [landoficeandfire](https://github.com/xahmol/landoficeandfire),
which built and hardware-validated the Upic viewer this project reuses.
See `CREDITS.md` for full attribution and `docs/ARCHITECTURE.md`/
`docs/MANDELBROT_ALGORITHM.md` for how it works and what's planned.

**Status**: fixed-point escape-time generator implemented and confirmed
working on real hardware, live-updating the picture as it renders.
Interactive zoom (pick a region of the current picture, regenerate at
that scale) is implemented on the `zoom-feature` branch -- not yet
confirmed on real hardware, see its own section below for current
caveats. See `docs/MANDELBROT_ALGORITHM.md` for the full design.

## Contents

- [Controls](#controls)
- [Building from source](#building-from-source)
- [Credits](CREDITS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Mandelbrot algorithm design](docs/MANDELBROT_ALGORITHM.md)

## Controls

Generation starts automatically on launch and the picture builds up
live. Once it completes, 4 corner-marker sprites appear over the
picture, outlining a zoom-target rectangle:

| Input | Action |
|---|---|
| `W`/`A`/`S`/`D` | Move the active corner |
| Cursor keys (hold Shift for up/left) | Same, alternative for muscle memory |
| Joystick, port 2 | Same, directional + fire |
| `RETURN` or joystick fire | Confirm the active corner, then the other |
| `Q` | Quit to BASIC |

Picking a zoom target is a two-step process: move the first corner
into place and confirm, then move the second (opposite) corner and
confirm again -- this regenerates the fractal at the newly selected
region, and the same corner-selection screen appears again once it
completes, so zooming can be repeated. The selection must span at
least 16 cells in both directions; confirming a smaller one is simply
ignored so adjustment can continue.

Joystick **port 2** (`$DC00`) is used deliberately, not port 1 --
port 1 shares hardware lines with the keyboard matrix and gives
unreliable readings while keys are also being read, which happens
every frame on this screen.

**Zoom precision limit**: the fractal coordinates use a fixed-point
format with a finite number of fractional bits, which caps how far
repeated zooming can go (roughly 16x total from the initial view)
before individual pixel steps round down to zero and the picture stops
changing with further zoom. This is a hard limit of the current
implementation, not a bug.

**Not yet confirmed on real hardware** (`zoom-feature` branch, as of
2026-09-10): the corner sprites' on-screen position relative to the
picture is a best-effort calibration guess (see `ZOOM_SPRITE_X0`/
`ZOOM_SPRITE_Y0` in `include/zoom.c`) -- if the markers don't visually
line up with the picture, those two constants are the first (and
should be the only) thing to adjust.

## Building from source

### Prerequisites

| Tool | Purpose | Install |
|---|---|---|
| [Oscar64](https://github.com/drmortalwombat/oscar64) | C cross-compiler targeting 6502/C64 | build from source, see their README |
| `wput` | FTP deploy to the Ultimate device | `sudo apt install wput` |
| `pandoc` + `texlive-xetex` | Generate `README.pdf` (optional) | `sudo apt install pandoc texlive-xetex` |

### Deploy setup

Copy `.env.example` to `.env` and set `ULTIP1` to your Ultimate
device's IP address:
```
cp .env.example .env
```

### Make targets

| Target | Effect |
|---|---|
| `make` / `make all` | Compile to `build/mandelupic.prg`, regenerate `README.pdf`, build the release ZIP |
| `make deploy` | FTP the compiled `.prg` to the Ultimate device set in `.env` |
| `make docs` | Regenerate `README.pdf` via pandoc |
| `make clean` | Remove build outputs |

Requires **Ultimate firmware 3.15 or newer** (UCI auto-enable, palette
control commands -- see `UCILIBMANUAL.md`).
