#!/usr/bin/env python3
"""Build warm, high-resolution transparent PNG art matching the approved mockups.

Artwork is freshly recreated, not cropped from the mockups. It is painted at
8x target resolution with soft gradients, glow, rounded organic silhouettes and
subtle texture, then Lanczos-downsampled to 4x PNG source assets. The firmware
ARGB packer performs the final alpha-aware downsample to display resolution.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
import math
import random

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "theme-sprites"
OUT.mkdir(parents=True, exist_ok=True)

R = 8          # painting scale
SOURCE = 4     # committed PNG scale

CLASSIC_BLUE=(61,168,244,255)
CLASSIC_LIGHT=(155,219,255,255)
CLASSIC_DARK=(13,30,45,255)
SOCCER_GREEN=(51,235,111,255)
SOCCER_MINT=(133,247,190,255)
SOCCER_DARK=(4,62,34,255)
KITTEN_PINK=(239,108,162,255)
KITTEN_LIGHT=(252,173,204,255)
KITTEN_CREAM=(242,224,230,255)
KITTEN_WARM=(226,202,211,255)
KITTEN_DARK=(91,48,64,255)
MAGIC_BLUE=(38,157,235,255)
MAGIC_SKY=(121,205,255,255)
MAGIC_DARK=(5,37,82,255)
MAGIC_GOLD=(228,181,85,255)
MAGIC_CREAM=(255,237,194,255)
WHITE=(248,252,250,255)
TRAN=(0,0,0,0)

def sc(v): return int(round(v*R))
def box(x0,y0,x1,y1): return tuple(sc(v) for v in (x0,y0,x1,y1))
def canvas(w,h): return Image.new("RGBA",(sc(w),sc(h)),TRAN)

def rgba(hexv,a=255):
    return ((hexv>>16)&255,(hexv>>8)&255,hexv&255,a)

def alpha_scale(mask, value):
    return mask.point(lambda p: (p*value)//255)

def soft_glow(base, mask, color, radius, alpha=150, offset=(0,0)):
    blur=mask.filter(ImageFilter.GaussianBlur(sc(radius)))
    blur=alpha_scale(blur,alpha)
    layer=Image.new("RGBA",base.size,color[:3]+(0,))
    layer.putalpha(blur)
    if offset != (0,0):
        shifted=Image.new("RGBA",base.size,TRAN)
        shifted.alpha_composite(layer,(sc(offset[0]),sc(offset[1])))
        base.alpha_composite(shifted)
    else:
        base.alpha_composite(layer)

def masked_gradient(base, mask, top, bottom):
    w,h=base.size
    grad=Image.new("RGBA",(1,h))
    px=grad.load()
    for y in range(h):
        t=y/max(1,h-1)
        px[0,y]=tuple(int(top[i]*(1-t)+bottom[i]*t) for i in range(4))
    grad=grad.resize((w,h))
    grad.putalpha(mask)
    base.alpha_composite(grad)

def ellipse_mask(size,b):
    m=Image.new("L",size,0); d=ImageDraw.Draw(m)
    d.ellipse(tuple(sc(v) for v in b),fill=255)
    return m

def rounded_rect_mask(size,b,r):
    m=Image.new("L",size,0); d=ImageDraw.Draw(m)
    d.rounded_rectangle(tuple(sc(v) for v in b),radius=sc(r),fill=255)
    return m

def polygon_mask(size,pts):
    m=Image.new("L",size,0); d=ImageDraw.Draw(m)
    d.polygon([(sc(x),sc(y)) for x,y in pts],fill=255)
    return m

def draw_line(d,pts,fill,width):
    d.line([(sc(x),sc(y)) for x,y in pts],fill=fill,width=max(1,sc(width)),joint="curve")

def draw_ellipse(d,b,fill,outline=None,width=1):
    d.ellipse(box(*b),fill=fill,outline=outline,width=max(1,sc(width)))

def draw_poly(d,pts,fill):
    d.polygon([(sc(x),sc(y)) for x,y in pts],fill=fill)

def draw_rr(d,b,r,fill,outline=None,width=1):
    d.rounded_rectangle(box(*b),radius=sc(r),fill=fill,outline=outline,width=max(1,sc(width)))

def composite_mask(base,mask,color):
    layer=Image.new("RGBA",base.size,color); layer.putalpha(mask)
    base.alpha_composite(layer)

def finish(im,name,w,h):
    target=(w*SOURCE,h*SOURCE)
    im=im.resize(target,Image.Resampling.LANCZOS)
    im.save(OUT/name,optimize=True,compress_level=9)

def star(d,cx,cy,r,color,points=4):
    pts=[]
    for i in range(points*2):
        rr=r if i%2==0 else r*.24
        a=-math.pi/2+i*math.pi/points
        pts.append((cx+math.cos(a)*rr,cy+math.sin(a)*rr))
    draw_poly(d,pts,color)

def house(theme):
    w=h=56; im=canvas(w,h)
    if theme=="classic": main,light,dark=CLASSIC_BLUE,CLASSIC_LIGHT,CLASSIC_DARK
    elif theme=="soccer": main,light,dark=SOCCER_GREEN,SOCCER_MINT,SOCCER_DARK
    elif theme=="kittens": main,light,dark=KITTEN_PINK,KITTEN_LIGHT,KITTEN_DARK
    else: main,light,dark=MAGIC_BLUE,MAGIC_SKY,MAGIC_DARK

    if theme=="magic":
        outer=polygon_mask(im.size,[(7,29),(28,7),(49,29),(45,49),(11,49)])
        soft_glow(im,outer,MAGIC_GOLD,2.8,125)
        composite_mask(im,outer,MAGIC_GOLD)
        inner=polygon_mask(im.size,[(11,29),(28,12),(45,29),(41,45),(15,45)])
        masked_gradient(im,inner,(27,130,225,255),(8,62,126,255))
        d=ImageDraw.Draw(im)
        draw_line(d,[(17,30),(28,19),(39,30)],MAGIC_CREAM,1.2)
        draw_rr(d,(24,33,32,46),2,MAGIC_DARK)
        star(d,47,11,5,MAGIC_GOLD,4)
    else:
        roof=polygon_mask(im.size,[(6,28),(28,7),(50,28),(45,33),(28,17),(11,33)])
        body=rounded_rect_mask(im.size,(12,27,44,48),4)
        mask=Image.new("L",im.size,0)
        mask=Image.frombytes("L",im.size,bytes(max(a,b) for a,b in zip(roof.tobytes(),body.tobytes())))
        soft_glow(im,mask,main,2.5,115)
        masked_gradient(im,mask,light,main)
        d=ImageDraw.Draw(im)
        draw_rr(d,(23,34,33,49),2,dark)
        draw_line(d,[(15,28),(28,15),(41,28)],rgba(0xFFFFFF,110),1.2)
        if theme=="kittens":
            draw_ellipse(d,(8,11,13,16),KITTEN_LIGHT)
            draw_ellipse(d,(43,8,48,13),KITTEN_LIGHT)
        elif theme=="soccer":
            draw_ellipse(d,(43,9,48,14),rgba(0xCFFFE1,170))
    finish(im,f"header_{theme}.png",w,h)

for theme in ("classic","soccer","kittens","magic"):
    house(theme)

def petal_layer(size,cx,cy,length,width,color,highlight,angle):
    layer=Image.new("RGBA",size,TRAN)
    mask=Image.new("L",size,0)
    d=ImageDraw.Draw(mask)
    # rounded teardrop, narrow at hub and broad at tip
    pts=[(sc(cx-width*.28),sc(cy-7)),
         (sc(cx-width*.55),sc(cy-length*.54)),
         (sc(cx-width*.38),sc(cy-length*.92)),
         (sc(cx),sc(cy-length)),
         (sc(cx+width*.38),sc(cy-length*.92)),
         (sc(cx+width*.55),sc(cy-length*.54)),
         (sc(cx+width*.28),sc(cy-7))]
    d.polygon(pts,fill=255)
    d.ellipse(box(cx-width*.38,cy-length,cx+width*.38,cy-length+width*.75),fill=255)
    soft_glow(layer,mask,color,1.2,70)
    masked_gradient(layer,mask,highlight,color)
    return layer.rotate(angle,resample=Image.Resampling.BICUBIC,center=(sc(cx),sc(cy)))

def fan(theme):
    w,h=142,116; im=canvas(w,h); cx,cy=58,58
    if theme=="classic":
        ring,blade,hi,hub=CLASSIC_BLUE,CLASSIC_LIGHT,rgba(0xD8F2FF),CLASSIC_BLUE
        blades=4
    elif theme=="soccer":
        ring,blade,hi,hub=SOCCER_GREEN,SOCCER_MINT,rgba(0xE6FFF0),rgba(0x24CB63)
        blades=4
    elif theme=="kittens":
        ring,blade,hi,hub=KITTEN_PINK,KITTEN_LIGHT,rgba(0xFFE5F0),KITTEN_PINK
        blades=4
    else:
        ring,blade,hi,hub=MAGIC_GOLD,MAGIC_SKY,rgba(0xE7F6FF),MAGIC_GOLD
        blades=3

    d=ImageDraw.Draw(im)
    if theme=="magic":
        shield=polygon_mask(im.size,[(58,3),(109,27),(98,85),(58,113),(18,85),(7,27)])
        soft_glow(im,shield,MAGIC_GOLD,3.2,115)
        composite_mask(im,shield,MAGIC_GOLD)
        inner=polygon_mask(im.size,[(58,10),(101,31),(90,80),(58,104),(26,80),(15,31)])
        masked_gradient(im,inner,(10,83,158,255),(4,39,91,255))
        radius=28
        for side in (-1,1):
            for i in range(6):
                y=32+i*10
                x=58+side*(48+i*1.4)
                leaf=ellipse_mask(im.size,(x-5,y-2.5,x+5,y+2.5))
                leaf=leaf.rotate(side*(-25+i*2),resample=Image.Resampling.BICUBIC,
                                 center=(sc(x),sc(y)))
                composite_mask(im,leaf,MAGIC_GOLD)
    else:
        radius=49
        ringmask=ellipse_mask(im.size,(cx-radius,cy-radius,cx+radius,cy+radius))
        inner=ellipse_mask(im.size,(cx-radius+4,cy-radius+4,cx+radius-4,cy+radius-4))
        ringmask=Image.frombytes("L",im.size,bytes(max(0,a-b) for a,b in zip(ringmask.tobytes(),inner.tobytes())))
        soft_glow(im,ringmask,ring,2.6,90)
        composite_mask(im,ringmask,ring)

    step=360/blades
    for i in range(blades):
        angle=i*step
        p=petal_layer(im.size,cx,cy,37 if theme!="magic" else 24,
                      25 if theme!="magic" else 19,blade,hi,angle)
        im.alpha_composite(p)

    draw_ellipse(d,(cx-9,cy-9,cx+9,cy+9),hub)
    draw_ellipse(d,(cx-5,cy-5,cx+5,cy+5),hi)
    draw_ellipse(d,(cx-3,cy-3,cx+3,cy+3),hub)
    finish(im,f"fan_{theme}.png",w,h)

for theme in ("classic","soccer","kittens","magic"):
    fan(theme)

def kitten_peek():
    w,h=74,56; im=canvas(w,h)
    shadow=ellipse_mask(im.size,(7,21,67,65)); soft_glow(im,shadow,rgba(0x8E4966),2.5,65,(0,1))
    ear1=polygon_mask(im.size,[(7,33),(14,3),(31,28)])
    ear2=polygon_mask(im.size,[(43,28),(60,3),(68,33)])
    composite_mask(im,ear1,KITTEN_CREAM); composite_mask(im,ear2,KITTEN_CREAM)
    d=ImageDraw.Draw(im)
    draw_poly(d,[(12,28),(16,10),(27,28)],KITTEN_LIGHT)
    draw_poly(d,[(47,28),(58,10),(63,28)],KITTEN_LIGHT)
    face=ellipse_mask(im.size,(8,18,67,72))
    masked_gradient(im,face,(252,239,243,255),KITTEN_CREAM)
    d=ImageDraw.Draw(im)
    draw_ellipse(d,(20,36,31,47),rgba(0xFFF8FA))
    draw_ellipse(d,(43,36,54,47),rgba(0xFFF8FA))
    draw_line(d,[(22,39),(27,41),(32,39)],KITTEN_DARK,1.0)
    draw_line(d,[(42,39),(47,41),(52,39)],KITTEN_DARK,1.0)
    draw_ellipse(d,(18,43,25,48),rgba(0xF7A7C7,95))
    draw_ellipse(d,(49,43,56,48),rgba(0xF7A7C7,95))
    draw_poly(d,[(37,42),(33.5,45),(40.5,45)],KITTEN_PINK)
    draw_line(d,[(37,45),(37,49),(34,51)],KITTEN_DARK,0.8)
    draw_line(d,[(37,49),(40,51)],KITTEN_DARK,0.8)
    for yy in (42,47):
        draw_line(d,[(29,yy),(4,yy-2)],rgba(0x744052,220),.7)
        draw_line(d,[(45,yy),(70,yy-2)],rgba(0x744052,220),.7)
    finish(im,"kitten_peek.png",w,h)

def kitten_sleep():
    w,h=118,74; im=canvas(w,h)
    bodymask=ellipse_mask(im.size,(22,27,105,69)); soft_glow(im,bodymask,rgba(0x9A5770),3,55,(0,1.5))
    masked_gradient(im,bodymask,(248,237,241,255),KITTEN_WARM)
    belly=ellipse_mask(im.size,(53,35,94,62)); masked_gradient(im,belly,(255,250,251,255),(241,226,231,255))
    head=ellipse_mask(im.size,(13,19,56,58)); masked_gradient(im,head,(248,235,240,255),KITTEN_WARM)
    d=ImageDraw.Draw(im)
    draw_poly(d,[(17,28),(20,7),(34,25)],KITTEN_WARM)
    draw_poly(d,[(38,25),(49,7),(53,30)],KITTEN_WARM)
    draw_poly(d,[(22,25),(24,14),(31,25)],KITTEN_LIGHT)
    draw_poly(d,[(42,24),(48,14),(50,27)],KITTEN_LIGHT)
    draw_line(d,[(24,38),(31,40)],KITTEN_DARK,.85)
    draw_line(d,[(38,40),(45,38)],KITTEN_DARK,.85)
    draw_poly(d,[(35,43),(31.5,46),(38.5,46)],KITTEN_PINK)
    # soft curled tail
    draw_line(d,[(91,55),(106,47),(112,34),(108,22)],KITTEN_WARM,7)
    draw_line(d,[(92,54),(106,46)],rgba(0xFFFFFF,80),1.3)
    # heart + z's
    draw_ellipse(d,(2,46,8,53),KITTEN_PINK); draw_ellipse(d,(7,46,13,53),KITTEN_PINK)
    draw_poly(d,[(2,49.5),(13,49.5),(7.5,57)],KITTEN_PINK)
    for x,y,size in [(77,17,4),(89,10,5),(102,3,6)]:
        draw_line(d,[(x-size,y),(x+size,y),(x-size,y+size),(x+size,y+size)],rgba(0x73384F,185),.65)
    finish(im,"kitten_sleep.png",w,h)

def kitten_paws():
    w,h=60,68; im=canvas(w,h); d=ImageDraw.Draw(im)
    def paw(cx,cy,rot=0):
        layer=Image.new("RGBA",im.size,TRAN); ld=ImageDraw.Draw(layer)
        col=rgba(0xB44476,205)
        ld.ellipse(box(cx-7,cy-5,cx+7,cy+7),fill=col)
        for ox,oy,r in [(-9,-8,3.3),(-3,-12,3.2),(4,-12,3.2),(10,-8,3.3)]:
            ld.ellipse(box(cx+ox-r,cy+oy-r,cx+ox+r,cy+oy+r),fill=col)
        return layer.rotate(rot,resample=Image.Resampling.BICUBIC,center=(sc(cx),sc(cy)))
    im.alpha_composite(paw(18,23,-18)); im.alpha_composite(paw(41,49,16))
    finish(im,"kitten_paws.png",w,h)

kitten_peek(); kitten_sleep(); kitten_paws()

def magic_castle():
    w,h=230,78; im=canvas(w,h)
    moon=ellipse_mask(im.size,(88,-18,144,38)); soft_glow(im,moon,rgba(0xD8ECFF),7,170)
    masked_gradient(im,moon,(245,250,255,235),(177,214,248,215))
    d=ImageDraw.Draw(im)
    random.seed(7)
    towers=[(2,27,40),(31,24,57),(58,29,46),(91,31,66),(126,25,48),(154,28,59),(186,25,43),(213,16,35)]
    for x,tw,th in towers:
        y=72-th
        draw_rr(d,(x,y,x+tw,72),1.8,MAGIC_DARK)
        draw_poly(d,[(x-2,y),(x+tw/2,y-15),(x+tw+2,y)],MAGIC_DARK)
        if tw>22:
            draw_line(d,[(x+tw/2,y-15),(x+tw/2,y-21)],MAGIC_GOLD,.7)
        for wx in range(x+7,x+tw-4,11):
            glow=ellipse_mask(im.size,(wx-1,y+13,wx+4,y+25)); soft_glow(im,glow,rgba(0xFFD55D),2,80)
            draw_rr(d,(wx,y+14,wx+3,y+23),.6,rgba(0xFFD258))
    draw_rr(d,(0,70,230,77),1,rgba(0x031D44))
    for _ in range(18):
        x=random.randint(4,225); y=random.randint(3,48); r=random.choice((.5,.7,1.0))
        star(d,x,y,r,rgba(0xF9D476,170),4)
    finish(im,"magic_castle.png",w,h)

def magic_lantern():
    w,h=56,72; im=canvas(w,h)
    glow=ellipse_mask(im.size,(13,18,43,61)); soft_glow(im,glow,rgba(0xFFD05A),6,175)
    d=ImageDraw.Draw(im)
    draw_line(d,[(18,15),(20,6),(28,2),(36,6),(38,15)],MAGIC_GOLD,2.2)
    draw_rr(d,(12,18,44,61),3,rgba(0xA96D14),MAGIC_GOLD,1.5)
    inner=rounded_rect_mask(im.size,(18,24,38,53),3)
    masked_gradient(im,inner,(255,241,170,245),(235,151,26,230))
    draw_line(d,[(19,26),(35,51)],rgba(0xFFF4C4,210),1.2)
    draw_line(d,[(37,26),(21,51)],rgba(0xFFF4C4,210),1.2)
    draw_rr(d,(17,58,39,65),2,MAGIC_GOLD)
    finish(im,"magic_lantern.png",w,h)

def magic_owl():
    w,h=62,78; im=canvas(w,h)
    body=ellipse_mask(im.size,(9,5,53,57)); soft_glow(im,body,rgba(0x7A4A25),2.5,55,(0,1))
    masked_gradient(im,body,(255,249,231,255),(227,211,179,255))
    d=ImageDraw.Draw(im)
    wing=rgba(0xC9A16D)
    draw_poly(d,[(12,39),(0,63),(22,56)],wing); draw_poly(d,[(50,39),(62,63),(40,56)],wing)
    draw_ellipse(d,(12,12,31,34),rgba(0xD5B37C)); draw_ellipse(d,(31,12,50,34),rgba(0xD5B37C))
    for cx in (22,40):
        eye=ellipse_mask(im.size,(cx-7,17,cx+7,31)); soft_glow(im,eye,rgba(0xA96832),1.6,70)
        draw_ellipse(d,(cx-7,17,cx+7,31),rgba(0x7B4B2C))
        draw_ellipse(d,(cx-3.5,20,cx+3.5,28),rgba(0x3C2418))
        draw_ellipse(d,(cx-1.5,21,cx+.5,23),WHITE)
    draw_poly(d,[(31,29),(27,36),(35,36)],rgba(0xC88935))
    draw_rr(d,(4,61,58,67),1,rgba(0x5D3B2A))
    draw_rr(d,(8,68,54,74),1,rgba(0xB77E3B))
    finish(im,"magic_owl.png",w,h)

def magic_quill():
    w,h=36,70; im=canvas(w,h)
    d=ImageDraw.Draw(im)
    feather=polygon_mask(im.size,[(28,3),(35,12),(31,27),(24,44),(14,58),(7,62),(10,47),(9,34),(16,18)])
    soft_glow(im,feather,rgba(0x8B6242),2,50)
    masked_gradient(im,feather,(236,219,190,255),(189,154,113,255))
    draw_line(d,[(8,64),(29,7)],rgba(0x8C6243),1.8)
    for y,x1,x2 in [(21,16,8),(29,18,7),(37,21,10),(46,18,8),(18,20,31),(27,22,32),(36,20,29)]:
        draw_line(d,[(x1,y),(x2,y+4)],rgba(0x8C6243,220),.8)
    draw_rr(d,(3,59,14,69),1.5,rgba(0x76431F))
    finish(im,"magic_quill.png",w,h)

def magic_corner():
    w=h=36; im=canvas(w,h); d=ImageDraw.Draw(im)
    # curved filigree rather than blocky right angles
    draw_line(d,[(2,33),(2,15),(5,9),(11,7),(31,7)],MAGIC_GOLD,1.5)
    draw_line(d,[(7,31),(7,18),(10,13),(16,12),(29,12)],MAGIC_CREAM,.7)
    draw_ellipse(d,(0,5,6,11),MAGIC_GOLD); draw_ellipse(d,(28,4,34,10),MAGIC_GOLD)
    draw_line(d,[(8,27),(13,21),(18,20),(22,22),(18,26),(13,26),(10,29)],MAGIC_GOLD,1.2)
    star(d,29,7,4,rgba(0xFFE39A),4)
    finish(im,"magic_corner_tl.png",w,h)

magic_castle(); magic_lantern(); magic_owl(); magic_quill(); magic_corner()

def soccer_goal():
    w,h=76,44; im=canvas(w,h)
    d=ImageDraw.Draw(im)
    frame=rgba(0xE8F8EF,235); net=rgba(0x87CFA1,155)
    glow=polygon_mask(im.size,[(8,9),(55,9),(70,3),(70,31),(55,36),(8,36)])
    soft_glow(im,glow,rgba(0x66D58A),2.2,55)
    draw_line(d,[(8,9),(55,9),(55,36),(8,36),(8,9)],frame,1.7)
    draw_line(d,[(55,9),(70,3),(70,31),(55,36)],frame,1.3)
    draw_line(d,[(8,9),(23,3),(70,3)],frame,1.3)
    for x in range(15,56,10): draw_line(d,[(x,9),(x,36)],net,.55)
    for y in range(15,37,7): draw_line(d,[(8,y),(55,y)],net,.55)
    for x in (59,64,69): draw_line(d,[(x,8),(x,31)],net,.45)
    for y in (10,17,24,31): draw_line(d,[(55,y),(70,y-4)],net,.45)
    finish(im,"soccer_goal.png",w,h)

def soccer_field():
    w,h=800,408; im=canvas(w,h)
    d=ImageDraw.Draw(im)

    # Smooth vertical grass gradient and alternating mowing bands. This uses
    # Pillow-native raster operations rather than Python per-pixel loops.
    for y in range(sc(h)):
        t=y/max(1,sc(h)-1)
        col=(7+int(3*t),55+int(15*t),30+int(7*t),255)
        d.line((0,y,sc(w),y),fill=col)
    for i,x in enumerate(range(0,w,80)):
        if i%2:
            d.rectangle((sc(x),0,sc(min(w,x+80)),sc(h)),fill=(8,70,37,52))

    # Fine organic grass grain generated at modest resolution and smoothly
    # scaled up; avoids both blockiness and a very expensive Python loop.
    noise=Image.effect_noise((w*2,h*2),11).convert("L")
    noise=noise.resize(im.size,Image.Resampling.BICUBIC)
    noise=noise.filter(ImageFilter.GaussianBlur(sc(.18)))
    grain=Image.new("RGBA",im.size,(31,116,63,0))
    grain.putalpha(noise.point(lambda p: max(0,min(24,int((p-96)*.17)))))
    im.alpha_composite(grain)

    # Diagonal stadium-light streaks like the approved green mockup.
    light=Image.new("RGBA",im.size,TRAN); ld=ImageDraw.Draw(light)
    for off,a in [(-80,24),(80,18),(260,13)]:
        ld.polygon([(sc(off),0),(sc(off+125),0),
                    (sc(off+500),sc(h)),(sc(off+370),sc(h))],
                   fill=(50,225,112,a))
    light=light.filter(ImageFilter.GaussianBlur(sc(13)))
    im.alpha_composite(light)

    d=ImageDraw.Draw(im)
    field=rgba(0x93CDA8,105); inset=18
    draw_line(d,[(inset,inset),(w-inset,inset),(w-inset,h-inset),
                 (inset,h-inset),(inset,inset)],field,1.2)
    draw_line(d,[(400,inset),(400,h-inset)],field,1.1)
    d.ellipse(box(340,144,460,264),outline=field,width=sc(1.1))
    draw_ellipse(d,(397,201,403,207),field)
    for left in (True,False):
        gx=inset if left else w-inset
        px_=132 if left else 668
        sx=70 if left else 730
        draw_line(d,[(gx,102),(px_,102),(px_,306),(gx,306)],field,1.0)
        draw_line(d,[(gx,148),(sx,148),(sx,260),(gx,260)],field,1.0)
        spot=92 if left else 708
        draw_ellipse(d,(spot-2.5,201.5,spot+2.5,206.5),field)

    # Soft edge vignette so the field sits behind the live cards rather than
    # looking like a flat green diagram.
    edge=Image.new("L",im.size,0)
    ed=ImageDraw.Draw(edge)
    ed.rectangle((0,0,*im.size),fill=120)
    inner=rounded_rect_mask(im.size,(10,10,w-10,h-10),22)
    inner=inner.filter(ImageFilter.GaussianBlur(sc(30)))
    edge=Image.frombytes("L",im.size,
        bytes(max(0,a-b//2) for a,b in zip(edge.tobytes(),inner.tobytes())))
    shade=Image.new("RGBA",im.size,(0,16,9,0))
    shade.putalpha(edge)
    im.alpha_composite(shade)
    finish(im,"soccer_field.png",w,h)


soccer_goal(); soccer_field()

print(f"Wrote 18 warm high-resolution transparent PNG assets to {OUT}")
