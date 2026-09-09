# Mandelbrot Upic

A Commodore 64 Ultimate demo that generates a Mandelbrot fractal
on-device at 64 MHz turbo, packs it directly into Upic format (a
16-color, 384x256 border-color raster picture technique), and displays
it live.

Spun off from [landoficeandfire](https://github.com/xahmol/landoficeandfire),
which built and hardware-validated the Upic viewer this project reuses.
See `CREDITS.md` for full attribution and `docs/ARCHITECTURE.md`/
`docs/MANDELBROT_ALGORITHM.md` for how it works and what's planned.

**Status**: buildchain scaffolded, runs a placeholder test pattern in
place of the real fractal generator -- see `docs/MANDELBROT_ALGORITHM.md`
for the implementation plan.

## Contents

- [Building from source](#building-from-source)
- [Credits](CREDITS.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Mandelbrot algorithm design](docs/MANDELBROT_ALGORITHM.md)

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
