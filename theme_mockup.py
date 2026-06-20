#!/usr/bin/env python3
"""Mehrunes Dagon mining-screen mockup at 480x320 (matches LCD panel).
Generates a PNG preview of the planned LVGL layout + art so we can iterate
on the aesthetic without flashing the device."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math

W, H = 480, 320

# ---- molten palette ----
BG        = (10, 5, 3)
MOLTEN    = (255, 106, 0)
MOLTEN_HI = (255, 176, 90)
DEEP_RED  = (196, 32, 0)
EMBER     = (255, 58, 0)
ASH       = (150, 120, 110)
RUNE_DIM  = (120, 38, 8)

SERIF   = "/usr/share/fonts/truetype/noto/NotoSerifDisplay-BlackItalic.ttf"
SERIF2  = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
MONO    = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
MONOB   = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

def F(path, sz): return ImageFont.truetype(path, sz)

def radial_molten():
    """Oblivion-gate molten glow rising from the bottom center."""
    img = Image.new("RGB", (W, H), BG)
    px = img.load()
    cx, cy = W/2, H + 40   # source below the screen
    maxd = math.hypot(W/2, H+40)
    for y in range(H):
        for x in range(W):
            d = math.hypot(x-cx, y-cy) / maxd      # 0 near source .. 1 far
            t = max(0.0, 1.0 - d)                   # intensity
            t = t ** 1.7
            # blend BG -> deep red -> molten near bottom
            r = int(BG[0] + (DEEP_RED[0]-BG[0])*t + (MOLTEN[0]-DEEP_RED[0])*(t**2.2))
            g = int(BG[1] + (DEEP_RED[1]-BG[1])*t + (MOLTEN[1]-DEEP_RED[1])*(t**2.6))
            b = int(BG[2] + (DEEP_RED[2]-BG[2])*t + (MOLTEN[2]-DEEP_RED[2])*(t**3.0))
            px[x, y] = (min(r,255), min(g,255), min(b,255))
    return img

def glow_text(base, xy, text, font, fill, glow=MOLTEN, anchor="la", grow=6):
    """Draw text with an outer molten glow."""
    layer = Image.new("RGBA", base.size, (0,0,0,0))
    d = ImageDraw.Draw(layer)
    d.text(xy, text, font=font, fill=glow+(255,), anchor=anchor)
    blur = layer.filter(ImageFilter.GaussianBlur(grow))
    base.paste(Image.alpha_composite(base.convert("RGBA"), blur).convert("RGB"), (0,0))
    d2 = ImageDraw.Draw(base)
    d2.text(xy, text, font=font, fill=fill, anchor=anchor)

def rune(d, cx, cy, s, color, kind):
    """Procedural angular Daedric-ish glyphs."""
    if kind == 0:
        d.line([(cx,cy-s),(cx,cy+s)], fill=color, width=2)
        d.line([(cx-s*0.6,cy-s*0.4),(cx,cy),(cx+s*0.6,cy-s*0.4)], fill=color, width=2)
        d.line([(cx-s*0.6,cy+s*0.6),(cx+s*0.6,cy+s*0.6)], fill=color, width=2)
    elif kind == 1:
        d.polygon([(cx,cy-s),(cx+s*0.7,cy),(cx,cy+s),(cx-s*0.7,cy)], outline=color, width=2)
        d.line([(cx,cy-s),(cx,cy+s)], fill=color, width=1)
    elif kind == 2:
        d.line([(cx-s*0.7,cy-s),(cx+s*0.7,cy-s)], fill=color, width=2)
        d.line([(cx,cy-s),(cx,cy+s)], fill=color, width=2)
        d.line([(cx-s*0.5,cy+s),(cx+s*0.5,cy+s*0.2)], fill=color, width=2)
    else:
        d.line([(cx-s*0.6,cy-s),(cx+s*0.6,cy-s)], fill=color, width=2)
        d.line([(cx-s*0.6,cy-s),(cx-s*0.6,cy+s)], fill=color, width=2)
        d.line([(cx-s*0.6,cy+s),(cx+s*0.6,cy+s*0.3)], fill=color, width=2)
        d.ellipse([cx+s*0.3,cy-s*0.3,cx+s*0.7,cy+s*0.1], outline=color, width=2)

def label(d, x, y, text, font, color, anchor="lm"):
    d.text((x,y), text, font=font, fill=color, anchor=anchor)

# ---------- build mining screen ----------
img = radial_molten()
d = ImageDraw.Draw(img)

# Daedric rune frame
for i, x in enumerate(range(28, W-20, 52)):
    rune(d, x, 12, 7, RUNE_DIM, i % 4)
    rune(d, x, H-12, 7, RUNE_DIM, (i+2) % 4)
# corner brackets (molten)
for (cx,cy,dx,dy) in [(8,8,1,1),(W-8,8,-1,1),(8,H-8,1,-1),(W-8,H-8,-1,-1)]:
    d.line([(cx,cy),(cx+22*dx,cy)], fill=MOLTEN, width=3)
    d.line([(cx,cy),(cx,cy+22*dy)], fill=MOLTEN, width=3)

# Title
glow_text(img, (W/2, 34), "LORD DAGON", F(SERIF, 30), MOLTEN_HI, glow=EMBER, anchor="mm", grow=7)
d = ImageDraw.Draw(img)
label(d, W/2, 58, "M E H R U N E S   ·   B M 1 3 7 0", F(SERIF2, 11), ASH, anchor="mm")

# Hero hashrate
glow_text(img, (W/2, 112), "4.76", F(MONOB, 78), MOLTEN_HI, glow=MOLTEN, anchor="mm", grow=10)
d = ImageDraw.Draw(img)
label(d, W/2, 158, "TH/s", F(MONOB, 20), MOLTEN, anchor="mm")

# Stat panels (3 across)
def panel(x0, y0, x1, y1):
    d.rectangle([x0,y0,x1,y1], outline=RUNE_DIM, width=1)
panels = [
    (20, 178, 158, 250, "TEMP",  "62", "C", EMBER),
    (171,178, 309, 250, "POWER", "73", "W", MOLTEN_HI),
    (322,178, 460, 250, "BEST",  "12.4", "M", MOLTEN_HI),
]
for (x0,y0,x1,y1,cap,val,unit,col) in panels:
    panel(x0,y0,x1,y1)
    label(d, (x0+x1)/2, y0+16, cap, F(SERIF2,12), ASH, anchor="mm")
    label(d, (x0+x1)/2, y0+44, val, F(MONOB,30), col, anchor="mm")
    label(d, x1-12, y0+50, unit, F(MONO,13), ASH, anchor="rm")

# lower stat row
lows = [
    (30,  "VCORE", "1150mV"),
    (175, "CURR",  "13.2A"),
    (320, "EFF",   "15.3 J/T"),
]
for (x,cap,val) in lows:
    label(d, x, 268, cap, F(SERIF2,11), ASH, anchor="lm")
    label(d, x, 284, val, F(MONOB,15), MOLTEN_HI, anchor="lm")

# footer: uptime + ip + fan
label(d, 24,  302, "UP 2d 14h 09m", F(MONO,11), ASH, anchor="lm")
label(d, W/2, 302, "FAN 2800RPM",   F(MONO,11), ASH, anchor="mm")
label(d, W-24,302, "192.168.1.50", F(MONO,11), MOLTEN, anchor="rm")

img.save("dagon_mining_mockup.png")
print("saved dagon_mining_mockup.png")
