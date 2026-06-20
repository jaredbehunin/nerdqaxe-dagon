#!/usr/bin/env python3
"""Mehrunes Dagon theme using the gpt-dagon-plains lava-fields art as the
background. Keeps the SAME stat layout coords as theme_dagon_gen.py so ui.cpp
needs no changes. Adds darkening + translucent obsidian panels for readability."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageEnhance
import math

W, H = 480, 320
OUT = "theme_build"  # run from repo root; regenerated, gitignored
SRC = "art/gpt-dagon-plains-1.png"  # your source art (not committed) -- see TROUBLESHOOTING.md

GOLD   = (255, 196, 92)
EMBER  = (255, 78, 0)
MOLTEN = (255, 120, 12)
ASH    = (170, 140, 130)
DIM    = (150, 60, 30)

SERIF  = "/usr/share/fonts/truetype/noto/NotoSerifDisplay-BlackItalic.ttf"
SERIF2 = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
MONOB  = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
def F(p,s): return ImageFont.truetype(p,s)

def base_from_image():
    im = Image.open(SRC).convert("RGB")
    # scale to height 320, center-crop width to 480
    sw = int(im.width * H / im.height)
    im = im.resize((sw, H))
    x0 = (sw - W)//2
    im = im.crop((x0, 0, x0+W, H))
    # darken a touch so overlaid text reads
    im = ImageEnhance.Brightness(im).enhance(0.72)
    # top + bottom vignette gradient for title and stat legibility
    ov = Image.new("L",(W,H),0); d=ImageDraw.Draw(ov)
    for y in range(H):
        a = 0
        if y < 70:   a = int(150*(1-y/70))          # darken top band
        if y > 165:  a = max(a, int(150*((y-165)/(H-165))))  # darken lower stats
        d.line([(0,y),(W,y)], fill=a)
    dark = Image.new("RGB",(W,H),(4,2,2))
    im = Image.composite(dark, im, ov)
    return im

def glow_text(base,xy,text,font,fill,glow,anchor="mm",grow=8):
    layer=Image.new("RGBA",base.size,(0,0,0,0))
    ImageDraw.Draw(layer).text(xy,text,font=font,fill=glow+(255,),anchor=anchor)
    blur=layer.filter(ImageFilter.GaussianBlur(grow))
    base.paste(Image.alpha_composite(base.convert("RGBA"),blur).convert("RGB"),(0,0))
    ImageDraw.Draw(base).text(xy,text,font=font,fill=fill,anchor=anchor)

def panel(img, box, alpha=140):
    """translucent obsidian card with molten top edge"""
    x0,y0,x1,y1 = box
    ov = Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(ov)
    d.rounded_rectangle(box, radius=6, fill=(6,4,4,alpha), outline=(150,55,25,220), width=1)
    d.line([(x0+10,y0),(x1-10,y0)], fill=(255,90,0,230), width=1)
    img.paste(Image.alpha_composite(img.convert("RGBA"),ov).convert("RGB"),(0,0))

def mining_bg():
    img = base_from_image()
    # title block with translucent strip
    strip = Image.new("RGBA",(W,H),(0,0,0,0))
    ImageDraw.Draw(strip).rectangle([0,0,W,66], fill=(4,2,3,120))
    img = Image.alpha_composite(img.convert("RGBA"),strip).convert("RGB")
    glow_text(img,(W/2,30),"LORD DAGON",F(SERIF,30),GOLD,glow=EMBER,grow=9)
    d=ImageDraw.Draw(img)
    d.text((W/2,54),"M E H R U N E S   ·   B M 1 3 7 0",font=F(SERIF2,10),fill=ASH,anchor="mm")
    glow_text(img,(W/2,158),"TH/s",F(SERIF2,16),EMBER,glow=(120,20,0),grow=3)
    # three stat cards (same coords as theme_dagon_gen layout)
    for (x0,y0,x1,y1,cap) in [(20,178,158,250,"TEMP"),(171,178,309,250,"POWER"),(322,178,460,250,"BEST")]:
        panel(img,(x0,y0,x1,y1))
        ImageDraw.Draw(img).text(((x0+x1)/2,y0+15),cap,font=F(SERIF2,11),fill=GOLD,anchor="mm")
    # secondary captions
    d=ImageDraw.Draw(img)
    for (x,cap) in [(30,"VCORE"),(175,"CURR"),(320,"EFF")]:
        d.text((x,268),cap,font=F(SERIF2,10),fill=ASH,anchor="lm")
    return img

if __name__=="__main__":
    m = mining_bg()
    m.save(f"{OUT}/miningscreen2.png")
    # generic = same image base + title only
    g = base_from_image()
    glow_text(g,(W/2,30),"LORD DAGON",F(SERIF,30),GOLD,glow=EMBER,grow=9)
    ImageDraw.Draw(g).text((W/2,54),"M E H R U N E S   ·   B M 1 3 7 0",font=F(SERIF2,10),fill=ASH,anchor="mm")
    g.save(f"{OUT}/generic.png")
    # preview with sample values
    from PIL import Image as I
    p = m.copy(); d=ImageDraw.Draw(p)
    for (x,y,t,s) in [(89,212,"62",26),(240,212,"73",26),(391,212,"12.4",24)]:
        d.text((x,y),t,font=F(MONOB,s),fill=GOLD,anchor="mm")
    glow_text(p,(240,108),"4.76",F(MONOB,40),GOLD,glow=MOLTEN,grow=6)
    d=ImageDraw.Draw(p)
    for (x,t) in [(30,"1150mV"),(175,"13.2A"),(320,"15.3J/T")]:
        d.text((x,284),t,font=F(MONOB,14),fill=MOLTEN,anchor="lm")
    p.save("dagon_img_preview.png")
    print("saved miningscreen2/generic + dagon_img_preview.png")
