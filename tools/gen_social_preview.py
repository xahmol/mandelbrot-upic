#!/usr/bin/env python3
"""
Generate GitHub social preview image for Mandelbrot Upic.
Same layout as locifilemanager-v2/OricScreenEditorLOCI/oricdemo2026/
heartbeat-demo/vdcmaniac/ArchiLudo's own screenshots/social-preview.png.

Output: screenshots/social-preview.png (1280x640)
"""

import os
from PIL import Image, ImageDraw, ImageFont

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

# --- Paths ---
SCREEN_SHOT    = os.path.join(REPO_ROOT, 'screenshots', 'default-palette.png')
FM_PREVIEW     = '/home/xahmol/git/locifilemanager-v2/screenshots/social-preview.png'
OUT_PATH       = os.path.join(REPO_ROOT, 'screenshots', 'social-preview.png')
FONT_PATH      = '/usr/share/fonts/truetype/ubuntu/UbuntuMono[wght].ttf'

# --- Canvas ---
W, H = 1280, 640
BG   = (16, 16, 16)

# --- Colors ---
C_CYAN   = (16, 235, 235)
C_YELLOW = (235, 235, 16)
C_WHITE  = (235, 235, 235)
C_GREEN  = (15, 235, 16)

# --- Layout (mirroring locifilemanager-v2/OricScreenEditorLOCI/oricdemo2026/
# heartbeat-demo/vdcmaniac/ArchiLudo layout) ---
# Left panel: screenshot with border
SHOT_X1, SHOT_Y1 = 32, 95
SHOT_X2, SHOT_Y2 = 727, 543   # 695 x 448 px

# Right panel: logo + text, x starts at ~760
TEXT_X = 760

# Logo: extracted from FM social preview, region x=768..1243, y=95..275
LOGO_FM_X1, LOGO_FM_Y1 = 768, 95
LOGO_FM_X2, LOGO_FM_Y2 = 1243, 275

# Text positions (y baselines matching FM/OSE/oricdemo2026/heartbeat-demo/vdcmaniac layout)
TITLE_Y   = 320    # large title
VERS_Y    = 390    # version
DESC_Y    = 447    # description line 1 (line spacing ~34px)
URL_Y     = 593    # URL

BORDER_COLOR = C_GREEN

# --- Load the main screenshot ---
shot = Image.open(SCREEN_SHOT).convert('RGB')

# Scale to fit SHOT area (695 x 448), preserving aspect ratio, centered
panel_w = SHOT_X2 - SHOT_X1    # 695
panel_h = SHOT_Y2 - SHOT_Y1    # 448
scale = min(panel_w / shot.width, panel_h / shot.height)
new_w = int(shot.width  * scale)
new_h = int(shot.height * scale)
shot_scaled = shot.resize((new_w, new_h), Image.LANCZOS)

# Center within panel
off_x = SHOT_X1 + (panel_w - new_w) // 2
off_y = SHOT_Y1 + (panel_h - new_h) // 2

# --- Extract idi8b logo from FM social preview ---
fm = Image.open(FM_PREVIEW)
logo = fm.crop((LOGO_FM_X1, LOGO_FM_Y1, LOGO_FM_X2, LOGO_FM_Y2))

# Scale logo to fit right panel width minus margin
logo_max_w = 1260 - TEXT_X   # 500 px
logo_max_h = 200
logo_scale = min(logo_max_w / logo.width, logo_max_h / logo.height)
logo_w = int(logo.width  * logo_scale)
logo_h = int(logo.height * logo_scale)
logo_scaled = logo.resize((logo_w, logo_h), Image.LANCZOS)

# --- Fonts ---
try:
    font_title = ImageFont.truetype(FONT_PATH, 46)
    font_vers  = ImageFont.truetype(FONT_PATH, 40)
    font_desc  = ImageFont.truetype(FONT_PATH, 26)
    font_url   = ImageFont.truetype(FONT_PATH, 24)
except Exception as e:
    print(f"Font load error: {e}; falling back to default")
    font_title = font_vers = font_desc = font_url = ImageFont.load_default()

# --- Build canvas ---
canvas = Image.new('RGB', (W, H), BG)
draw   = ImageDraw.Draw(canvas)

# Paste main screenshot (with 4px green border)
border = 4
draw.rectangle(
    [off_x - border, off_y - border, off_x + new_w + border, off_y + new_h + border],
    outline=BORDER_COLOR, width=border
)
canvas.paste(shot_scaled, (off_x, off_y))

# Paste logo
logo_x = TEXT_X
logo_y = 80
canvas.paste(logo_scaled, (logo_x, logo_y))

# --- Text ---
draw.text((TEXT_X, TITLE_Y),  "Mandelbrot Upic",        font=font_title, fill=C_CYAN)
draw.text((TEXT_X, VERS_Y),   "v1.0.1",                 font=font_vers,  fill=C_YELLOW)
draw.text((TEXT_X, DESC_Y),        "Mandelbrot fractal",      font=font_desc, fill=C_WHITE)
draw.text((TEXT_X, DESC_Y + 34),   "generator + interactive", font=font_desc, fill=C_WHITE)
draw.text((TEXT_X, DESC_Y + 68),   "zoom for Ultimate 64",    font=font_desc, fill=C_WHITE)
draw.text((TEXT_X, URL_Y),    "github.com/xahmol/mandelbrot-upic", font=font_url, fill=C_GREEN)

# --- Save ---
canvas.save(OUT_PATH, 'PNG')
print(f"Saved: {OUT_PATH}")
