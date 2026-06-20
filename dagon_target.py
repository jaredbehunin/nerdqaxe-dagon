#!/usr/bin/env python3
"""Polished Mehrunes Dagon mining card -- target design at 480x320."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter
import math, random

W, H = 480, 320
random.seed(7)

# molten / infernal palette
OBSIDIAN = (8, 6, 8)
BLOOD    = (120, 14, 6)
EMBER    = (255, 78, 0)
MOLTEN   = (255, 120, 12)
GOLD     = (255, 196, 92)
ASH      = (150, 120, 112)
DIM      = (96, 40, 22)

SERIF  = "/usr/share/fonts/truetype/noto/NotoSerifDisplay-BlackItalic.ttf"
SERIFR = "/usr/share/fonts/truetype/noto/NotoSerifDisplay-Black.ttf"
SERIF2 = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
MONOB  = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
def F(p,s): return ImageFont.truetype(p,s)

def base():
    img = Image.new("RGB",(W,H),OBSIDIAN); px=img.load()
    cx, cy = W/2, H+30
    maxd = math.hypot(W/2, H+30)
    for y in range(H):
        for x in range(W):
            d = math.hypot(x-cx,y-cy)/maxd
            t = max(0.0, 1.0-d)**2.0
            # obsidian -> blood -> molten toward the bottom
            r = OBSIDIAN[0] + (BLOOD[0]-OBSIDIAN[0])*t + (MOLTEN[0]-BLOOD[0])*(t**2.6)
            g = OBSIDIAN[1] + (BLOOD[1]-OBSIDIAN[1])*t + (MOLTEN[1]-BLOOD[1])*(t**3.2)
            b = OBSIDIAN[2] + (BLOOD[2]-OBSIDIAN[2])*t + (MOLTEN[2]-BLOOD[2])*(t**3.6)
            px[x,y]=(min(int(r),255),min(int(g),255),min(int(b),255))
    return img

def lava_cracks(img):
    """faint glowing fractures across the lower half"""
    layer = Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(layer)
    for _ in range(7):
        x = random.randint(20,W-20); y = random.randint(150,H-10)
        pts=[(x,y)]
        for _ in range(random.randint(4,7)):
            x += random.randint(-40,40); y += random.randint(-22,10)
            pts.append((x,y))
        d.line(pts, fill=(255,90,0,70), width=1)
    layer = layer.filter(ImageFilter.GaussianBlur(1))
    return Image.alpha_composite(img.convert("RGBA"), layer).convert("RGB")

def glow_text(base,xy,text,font,fill,glow,anchor="mm",grow=8,ga=255):
    layer=Image.new("RGBA",base.size,(0,0,0,0))
    ImageDraw.Draw(layer).text(xy,text,font=font,fill=glow+(ga,),anchor=anchor)
    blur=layer.filter(ImageFilter.GaussianBlur(grow))
    base.paste(Image.alpha_composite(base.convert("RGBA"),blur).convert("RGB"),(0,0))
    ImageDraw.Draw(base).text(xy,text,font=font,fill=fill,anchor=anchor)

def rune(d,cx,cy,s,c,k):
    if k==0:
        d.line([(cx,cy-s),(cx,cy+s)],fill=c,width=2); d.line([(cx-s*.6,cy-s*.3),(cx,cy+s*.1),(cx+s*.6,cy-s*.3)],fill=c,width=2)
    elif k==1:
        d.polygon([(cx,cy-s),(cx+s*.7,cy),(cx,cy+s),(cx-s*.7,cy)],outline=c,width=2)
    elif k==2:
        d.line([(cx-s*.7,cy-s),(cx+s*.7,cy-s)],fill=c,width=2); d.line([(cx,cy-s),(cx,cy+s)],fill=c,width=2); d.line([(cx-s*.4,cy+s),(cx+s*.5,cy+s*.2)],fill=c,width=2)
    else:
        d.line([(cx-s*.6,cy-s),(cx-s*.6,cy+s)],fill=c,width=2); d.line([(cx-s*.6,cy-s),(cx+s*.5,cy-s)],fill=c,width=2); d.line([(cx-s*.6,cy+s*.1),(cx+s*.4,cy+s*.1)],fill=c,width=2)

def trident(d,cx,cy,s,c):
    """Mehrunes' Razor / trident sigil"""
    d.line([(cx,cy-s),(cx,cy+s)],fill=c,width=3)
    d.line([(cx-s*.7,cy-s*.4),(cx-s*.7,cy-s)],fill=c,width=3)
    d.line([(cx+s*.7,cy-s*.4),(cx+s*.7,cy-s)],fill=c,width=3)
    d.line([(cx-s*.7,cy-s*.4),(cx,cy),(cx+s*.7,cy-s*.4)],fill=c,width=3)
    d.line([(cx-s*.35,cy+s),(cx+s*.35,cy+s)],fill=c,width=3)

def stat_card(d,x0,y0,x1,y1):
    # molten-edged obsidian card
    d.rounded_rectangle([x0,y0,x1,y1],radius=6,outline=DIM,width=1)
    d.line([(x0+10,y0),(x1-10,y0)],fill=(255,90,0),width=1)

img = base()
img = lava_cracks(img)
d = ImageDraw.Draw(img)

# frame: thin molten border + corner filigree
d.rectangle([3,3,W-4,H-4],outline=(60,20,10),width=1)
for (cx,cy,dx,dy) in [(6,6,1,1),(W-6,6,-1,1),(6,H-6,1,-1),(W-6,H-6,-1,-1)]:
    d.line([(cx,cy),(cx+18*dx,cy)],fill=EMBER,width=2)
    d.line([(cx,cy),(cx,cy+18*dy)],fill=EMBER,width=2)

# header band
trident(d, 40, 30, 14, GOLD)
trident(d, W-40, 30, 14, GOLD)
glow_text(img,(W/2,28),"LORD DAGON",F(SERIF,30),GOLD,glow=EMBER,grow=9)
d=ImageDraw.Draw(img)
d.text((W/2,52),"M E H R U N E S   ·   B M 1 3 7 0",font=F(SERIF2,10),fill=ASH,anchor="mm")
d.line([(70,64),(W-70,64)],fill=DIM,width=1)

# hero hashrate
glow_text(img,(W/2,104),"4.76",F(MONOB,72),GOLD,glow=MOLTEN,grow=12)
d=ImageDraw.Draw(img)
glow_text(img,(W/2,150),"TH/s",F(SERIF2,18),EMBER,glow=BLOOD,grow=4)
d=ImageDraw.Draw(img)

# three hero stat cards
cards=[(18,172,156,250,"TEMP","62°","ASIC"),
       (171,172,309,250,"POWER","73W","73% load"),
       (324,172,462,250,"BEST DIFF","12.4M","session")]
for (x0,y0,x1,y1,cap,val,sub) in cards:
    stat_card(d,x0,y0,x1,y1)
    d.text(((x0+x1)/2,y0+14),cap,font=F(SERIF2,11),fill=ASH,anchor="mm")
    glow_text(img,((x0+x1)/2,y0+42),val,F(MONOB,26),GOLD,glow=MOLTEN,grow=5)
    d=ImageDraw.Draw(img)
    d.text(((x0+x1)/2,y0+64),sub,font=F(SERIF2,9),fill=DIM,anchor="mm")

# secondary stat row
secs=[(60,"VCORE","1199mV"),(180,"CURRENT","13.2A"),(300,"EFF","15.3 J/T"),(415,"FAN","2800")]
d.line([(18,262),(W-18,262)],fill=DIM,width=1)
for (x,cap,val) in secs:
    d.text((x,274),cap,font=F(SERIF2,9),fill=ASH,anchor="mm")
    d.text((x,290),val,font=F(MONOB,14),fill=MOLTEN,anchor="mm")

# footer
d.line([(18,302),(W-18,302)],fill=DIM,width=1)
d.text((24,311),"UPTIME 2d 14h 09m",font=F(SERIF2,9),fill=ASH,anchor="lm")
d.text((W-24,311),"192.168.1.50",font=F(MONOB,10),fill=GOLD,anchor="rm")

img.save("dagon_target.png")
print("saved dagon_target.png")
