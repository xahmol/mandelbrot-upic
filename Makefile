
# Mandelbrot Upic
# Commodore 64 Ultimate demo -- generates a Mandelbrot fractal on-device
# at 64 MHz turbo, packed directly into Upic format, and displays it live
# via the border-color raster technique. Requires Ultimate 64 / U64
# Elite 2, firmware 3.15 or newer (uses the fw 3.15+ UCI auto-enable
# sequence and the GET_PALETTE/SET_PALETTE/SET_PALETTE_COLOR/
# RESET_PALETTE control commands -- see UCILIBMANUAL.md).

# Target platform
SYS = c64

# Cross-platform shell detection
ifneq ($(shell echo),)
  CMD_EXE = 1
endif

ifdef CMD_EXE
  NULLDEV = nul:
  DEL     = -del /f
  RMDIR   = rmdir /s /q
  MKDIR   = mkdir
else
  NULLDEV = /dev/null
  DEL     = $(RM)
  RMDIR   = $(RM) -r
  MKDIR   = mkdir -p
endif

# Toolchain -- override the path if oscar64 lives elsewhere:
#   make CC=/path/to/oscar64/bin/oscar64
# (plain '=', not '?=' -- CC is a Make built-in with a default of 'cc',
# which is never "unset", so '?=' would silently never take effect)
CC = /home/xahmol/oscar64/bin/oscar64

# Application name
MAIN = mandelupic

# Build versioning
VERSION_MAJOR     = 1
VERSION_MINOR     = 0
VERSION_PATCH     = 0
VERSION_TIMESTAMP = $(shell date "+%Y%m%d-%H%M")
VERSION           = v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)-$(VERSION_TIMESTAMP)

# Compile flags
#   -i=include       : add include/ to header search path
#   -tm=c64          : target Commodore 64
#   -tf=prg          : output standard .prg file
#   -O2              : optimise
#   -dNOFLOAT        : disable float support (saves space; also matches
#                      the fixed-point design -- see docs/MANDELBROT_ALGORITHM.md)
#   -dHEAPCHECK      : catch heap corruption in debug builds
#   -dVERSION        : pass version string to source
#   -dDATA_QUEUE_SZ / -dSTATUS_QUEUE_SZ : shrunk UCI queues -- the Upic
#                      viewer's fixed $1800-$D000 picture buffer shrinks
#                      Oscar64's own program region substantially (see
#                      include/upic_viewer.c's #pragma region(main, ...)),
#                      which the library's normal 512/256-byte queues
#                      don't fit alongside. A palette push only ever
#                      transfers 50 bytes, so 64/16 is plenty -- same
#                      values proven working in landoficeandfire.
CFLAGS = -i=include \
         -tm=$(SYS) \
         -tf=prg \
         -O2 \
         -dNOFLOAT \
         -dHEAPCHECK \
         -dDATA_QUEUE_SZ=52 \
         -dSTATUS_QUEUE_SZ=12 \
         -dVERSION="\"$(VERSION)\""

# Main source (Oscar64 follows #pragma compile chains from here)
MAINSRC = src/main.c

# All sources that Oscar64 compiles via #pragma compile chains.
# Listed here so make rebuilds when any of them change.
ALLSRCS = $(MAINSRC) \
          include/upic_viewer.c include/upic_viewer.h \
          include/rombank.c include/rombank.h \
          include/turbo.c include/turbo.h \
          include/ultimate_common_lib.c include/ultimate_common_lib.h \
          include/mandelbrot.c include/mandelbrot.h \
          include/zoom.c include/zoom.h

# Output
TARGET = build/$(MAIN).prg

########################################

# Demo install path on SD/USB (must match any path baked into src/main.c)
INSTALL_PATH = idi8b/mandelupic

# Ultimate device deployment target. Store only the IP in .env (gitignored,
# never committed); everything else is derived here.
-include .env
ULTIP1  ?= <set_ULTIP1_in_.env>
ULTUSB  ?= usb0
ULTPATH  = /$(ULTUSB)/$(INSTALL_PATH)/
ULTFTP1  = ftp://$(ULTIP1)$(ULTPATH)

# Versioned release ZIP
ZIPFILE  = build/$(MAIN)-$(VERSION).zip
README   = README.pdf

.SUFFIXES:
.PHONY: all clean deploy check-deploy zip docs

all: $(TARGET) $(README) zip

$(TARGET): $(ALLSRCS)
	@$(MKDIR) build 2>$(NULLDEV) ; true
	$(CC) $(CFLAGS) -n -o=$(TARGET) $<

clean:
	$(DEL) build/*.prg 2>$(NULLDEV) ; true
	$(DEL) build/*.map 2>$(NULLDEV) ; true
	$(DEL) build/*.asm 2>$(NULLDEV) ; true
	$(DEL) build/*.lbl 2>$(NULLDEV) ; true
	$(DEL) build/*.zip 2>$(NULLDEV) ; true

# Regenerate README.pdf from README.md (requires pandoc + texlive-xetex).
# Install: sudo apt install pandoc texlive-xetex
# Warns and skips (does not fail the build) if pandoc is unavailable, since
# README.pdf is committed to git and only needs regenerating when docs change.
docs: $(README)

$(README): README.md pandoc-defaults.yaml pandoc-header.tex
	@if which pandoc >/dev/null 2>&1; then \
		pandoc --defaults=pandoc-defaults.yaml README.md -o $(README); \
	else \
		echo "WARNING: pandoc not found -- $(README) not updated (install: sudo apt install pandoc texlive-xetex)"; \
	fi

zip: $(TARGET)
	$(MKDIR) build/$(INSTALL_PATH) 2>$(NULLDEV) ; true
	cp $(TARGET) build/$(INSTALL_PATH)/$(MAIN).prg
	cp README.md build/$(INSTALL_PATH)/README.md
	cd build && zip -r $(MAIN)-$(VERSION).zip idi8b/
	$(RMDIR) build/idi8b 2>$(NULLDEV) ; true

# Safety check before deploy: make sure the Ultimate device is actually reachable
check-deploy:
	@curl -s --connect-timeout 3 $(ULTFTP1)/ >/dev/null 2>&1 || \
		(echo "ERROR: Cannot reach Ultimate device at $(ULTIP1) -- check ULTIP1 in .env" && false)

deploy: check-deploy $(TARGET)
	wput -u $(TARGET) $(ULTFTP1)$(MAIN).prg
