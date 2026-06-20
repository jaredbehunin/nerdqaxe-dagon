#!/usr/bin/env python3
"""Mehrunes Dagon theme generator for NerdQAxe++ 480x320 ST7796.
Produces art-only background PNGs (static art + captions baked in) for all 7
theme screens, plus a coordinate-annotated preview of the mining screen so we
can confirm live-value label placement before flashing.

Live values (hashrate, temp, power, ...) are NOT drawn here -- they are LVGL
labels overlaid by the firmware at the COORDS table below. Keep this table in
sync with ui.cpp."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math, os

W, H = 480, 320
OUT = "theme_build"  # run from repo root; regenerated, gitignored
os.makedirs(OUT, exist_ok=True)

BG        = (10, 5, 3)
MOLTEN    = (255, 106, 0)
MOLTEN_HI = (255, 176, 90)
DEEP_RED  = (196, 32, 0)
EMBER     = (255, 58, 0)
ASH       = (165, 130, 118)
RUNE_DIM  = (120, 38, 8)

SERIF  = "/usr/share/fonts/truetype/noto/NotoSerifDisplay-BlackItalic.ttf"
SERIF2 = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
MONO   = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
MONOB  = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
def F(p,s): return ImageFont.truetype(p,s)

# ---- live-value label placement (TOP_LEFT absolute coords for ui.cpp) ----
# anchor 'lm'/'mm'/'rm' = how the VALUE text is justified at (x,y)
COORDS = {
    "hashrate": (240, 112, "mm"),   # hero, DigitalNumbers28-ish
    "temp":     (89,  222, "mm"),
    "power":    (240, 222, "mm"),
    "best":     (391, 222, "mm"),
    "vcore":    (30,  284, "lm"),
    "curr":     (175, 284, "lm"),
    "eff":      (320, 284, "lm"),
    "uptime":   (24,  302, "lm"),
    "fan":      (240, 302, "mm"),
    "ip":       (456, 302, "rm"),
}

def radial_molten():
    # dark obsidian with a contained molten glow rising from the bottom edge.
    # steep falloff keeps most of the panel near-black so stats stay readable.
    OBS=(8,6,8)
    img = Image.new("RGB",(W,H),OBS); px=img.load()
    cx,cy=W/2,H+55; maxd=math.hypot(W/2,H+55)
    for y in range(H):
        for x in range(W):
            d=math.hypot(x-cx,y-cy)/maxd; t=max(0.,1.-d)**2.7
            r=int(OBS[0]+(150-OBS[0])*t+(255-150)*(t**3.0))
            g=int(OBS[1]+(26-OBS[1])*t+(110-26)*(t**3.7))
            b=int(OBS[2]+(10-OBS[2])*t+(18-10)*(t**4.2))
            px[x,y]=(min(r,255),min(g,255),min(b,255))
    return img

def glow_text(base,xy,text,font,fill,glow=MOLTEN,anchor="la",grow=6):
    layer=Image.new("RGBA",base.size,(0,0,0,0))
    ImageDraw.Draw(layer).text(xy,text,font=font,fill=glow+(255,),anchor=anchor)
    blur=layer.filter(ImageFilter.GaussianBlur(grow))
    base.paste(Image.alpha_composite(base.convert("RGBA"),blur).convert("RGB"),(0,0))
    ImageDraw.Draw(base).text(xy,text,font=font,fill=fill,anchor=anchor)

def rune(d,cx,cy,s,color,kind):
    if kind==0:
        d.line([(cx,cy-s),(cx,cy+s)],fill=color,width=2)
        d.line([(cx-s*.6,cy-s*.4),(cx,cy),(cx+s*.6,cy-s*.4)],fill=color,width=2)
        d.line([(cx-s*.6,cy+s*.6),(cx+s*.6,cy+s*.6)],fill=color,width=2)
    elif kind==1:
        d.polygon([(cx,cy-s),(cx+s*.7,cy),(cx,cy+s),(cx-s*.7,cy)],outline=color,width=2)
        d.line([(cx,cy-s),(cx,cy+s)],fill=color,width=1)
    elif kind==2:
        d.line([(cx-s*.7,cy-s),(cx+s*.7,cy-s)],fill=color,width=2)
        d.line([(cx,cy-s),(cx,cy+s)],fill=color,width=2)
        d.line([(cx-s*.5,cy+s),(cx+s*.5,cy+s*.2)],fill=color,width=2)
    else:
        d.line([(cx-s*.6,cy-s),(cx+s*.6,cy-s)],fill=color,width=2)
        d.line([(cx-s*.6,cy-s),(cx-s*.6,cy+s)],fill=color,width=2)
        d.line([(cx-s*.6,cy+s),(cx+s*.6,cy+s*.3)],fill=color,width=2)
        d.ellipse([cx+s*.3,cy-s*.3,cx+s*.7,cy+s*.1],outline=color,width=2)

def frame_and_title(img, subtitle):
    d=ImageDraw.Draw(img)
    for i,x in enumerate(range(28,W-20,52)):
        rune(d,x,12,7,RUNE_DIM,i%4); rune(d,x,H-12,7,RUNE_DIM,(i+2)%4)
    for (cx,cy,dx,dy) in [(8,8,1,1),(W-8,8,-1,1),(8,H-8,1,-1),(W-8,H-8,-1,-1)]:
        d.line([(cx,cy),(cx+22*dx,cy)],fill=MOLTEN,width=3)
        d.line([(cx,cy),(cx,cy+22*dy)],fill=MOLTEN,width=3)
    glow_text(img,(W/2,34),"LORD DAGON",F(SERIF,30),MOLTEN_HI,glow=EMBER,anchor="mm",grow=7)
    ImageDraw.Draw(img).text((W/2,58),subtitle,font=F(SERIF2,11),fill=ASH,anchor="mm")

# ----- mining background (art + captions only) -----
def mining_bg():
    img=radial_molten()
    frame_and_title(img, "M E H R U N E S   ·   B M 1 3 7 0")
    d=ImageDraw.Draw(img)
    d.text((W/2,158),"TH/s",font=F(MONOB,20),fill=MOLTEN,anchor="mm")
    panels=[(20,178,158,250,"TEMP"),(171,178,309,250,"POWER"),(322,178,460,250,"BEST")]
    for (x0,y0,x1,y1,cap) in panels:
        d.rectangle([x0,y0,x1,y1],outline=RUNE_DIM,width=1)
        d.text(((x0+x1)/2,y0+16),cap,font=F(SERIF2,12),fill=ASH,anchor="mm")
    for (x,cap) in [(30,"VCORE"),(175,"CURR"),(320,"EFF")]:
        d.text((x,268),cap,font=F(SERIF2,11),fill=ASH,anchor="lm")
    return img

def preview(img):
    """overlay sample live values at COORDS to check alignment"""
    p=img.copy(); d=ImageDraw.Draw(p)
    samp={"hashrate":("4.76",MONOB,46,MOLTEN_HI),"temp":("62",MONOB,28,EMBER),
          "power":("73",MONOB,28,MOLTEN_HI),"best":("12.4",MONOB,26,MOLTEN_HI),
          "vcore":("1150mV",MONOB,15,MOLTEN_HI),"curr":("13.2A",MONOB,15,MOLTEN_HI),
          "eff":("15.3J/T",MONOB,15,MOLTEN_HI),"uptime":("UP 2d 14h",MONO,11,ASH),
          "fan":("FAN 2800",MONO,11,ASH),"ip":("192.168.1.50",MONO,11,MOLTEN)}
    for k,(x,y,a) in COORDS.items():
        t,fnt,sz,col=samp[k]; d.text((x,y),t,font=F(fnt,sz),fill=col,anchor=a)
    return p

# ----- generic background for the other screens -----
def generic_bg(title_sub):
    img=radial_molten(); frame_and_title(img,title_sub); return img

if __name__=="__main__":
    # Two images only (flash budget): full mining art + one shared generic bg.
    m=mining_bg()
    m.save(f"{OUT}/miningscreen2.png")
    preview(m).save("dagon_mining_preview.png")
    generic_bg("M E H R U N E S   D A G O N").save(f"{OUT}/generic.png")
    print("backgrounds ->", OUT, "(miningscreen2.png, generic.png)")
    print("preview -> dagon_mining_preview.png")
