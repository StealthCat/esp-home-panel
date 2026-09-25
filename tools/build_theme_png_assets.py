#!/usr/bin/env python3
"""Build high-resolution transparent PNG source art for ESP Home Panel themes.

These are source assets for the firmware ARGB packer. They are intentionally
redrawn from the approved mockup compositions rather than cropped from the
mockups. All assets are produced at 2x or 4x firmware resolution so the packer
can alpha-aware downsample them for clean edges on the panel.
"""
from pathlib import Path
from PIL import Image, ImageDraw
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "theme-sprites"
OUT.mkdir(parents=True, exist_ok=True)

CLASSIC_BLUE=(57,166,244,255)
CLASSIC_LIGHT=(132,209,255,255)
CLASSIC_DARK=(15,31,45,255)
SOCCER_GREEN=(48,238,109,255)
SOCCER_MINT=(123,246,188,255)
SOCCER_DARK=(5,66,36,255)
KITTEN_PINK=(244,111,169,255)
KITTEN_LIGHT=(251,169,203,255)
KITTEN_CREAM=(236,219,224,255)
KITTEN_DARK=(95,51,67,255)
MAGIC_BLUE=(31,151,231,255)
MAGIC_DARK=(5,42,92,255)
MAGIC_GOLD=(226,181,86,255)
MAGIC_CREAM=(255,235,191,255)
WHITE=(245,250,248,255)
TRAN=(0,0,0,0)

def img(w,h): return Image.new('RGBA',(w,h),TRAN)
def poly(d,p,fill): d.polygon(p,fill=fill)
def ellipse(d,box,fill,outline=None,width=1): d.ellipse(box,fill=fill,outline=outline,width=width)
def line(d,p,fill,width): d.line(p,fill=fill,width=width,joint='curve')
def rect(d,box,fill,outline=None,width=1): d.rectangle(box,fill=fill,outline=outline,width=width)

def clear_polygon(im, pts):
    m=Image.new('L',im.size,0); md=ImageDraw.Draw(m); md.polygon(pts,fill=255); im.paste((0,0,0,0),mask=m)

def save(im,name):
    im.save(OUT/name,optimize=True,compress_level=9)

def house(color, magic=False):
    s=4; im=img(56*s,56*s); d=ImageDraw.Draw(im)
    if magic:
        poly(d,[(24*s,16*s),(28*s,8*s),(32*s,16*s),(47*s,29*s),(43*s,48*s),(13*s,48*s),(9*s,29*s)],MAGIC_GOLD)
        poly(d,[(28*s,11*s),(44*s,30*s),(40*s,44*s),(16*s,44*s),(12*s,30*s)],MAGIC_DARK)
        poly(d,[(28*s,16*s),(40*s,29*s),(38*s,42*s),(18*s,42*s),(16*s,29*s)],MAGIC_BLUE)
        rect(d,(25*s,31*s,31*s,44*s),MAGIC_DARK)
        ellipse(d,(26*s,12*s,30*s,16*s),MAGIC_CREAM)
    else:
        poly(d,[(7*s,27*s),(28*s,8*s),(49*s,27*s),(44*s,31*s),(44*s,47*s),(12*s,47*s),(12*s,31*s)],color)
        poly(d,[(11*s,27*s),(28*s,12*s),(45*s,27*s)],color)
        clear_polygon(im,[(21*s,24*s),(28*s,17*s),(35*s,24*s)])
        rect(d,(24*s,34*s,32*s,47*s),CLASSIC_DARK)
    return im

save(house(CLASSIC_BLUE),'header_classic.png')
save(house(SOCCER_GREEN),'header_soccer.png')
save(house(KITTEN_PINK),'header_kittens.png')
save(house(MAGIC_BLUE,True),'header_magic.png')

def fan(kind):
    s=4; im=img(142*s,116*s); d=ImageDraw.Draw(im)
    cx,cy=58*s,58*s
    if kind=='classic': ring,blade,hub=CLASSIC_BLUE,CLASSIC_LIGHT,CLASSIC_BLUE
    elif kind=='soccer': ring,blade,hub=SOCCER_GREEN,SOCCER_MINT,(36,207,99,255)
    elif kind=='kittens': ring,blade,hub=KITTEN_PINK,KITTEN_LIGHT,KITTEN_PINK
    else: ring,blade,hub=MAGIC_GOLD,(143,210,252,255),MAGIC_GOLD

    if kind=='magic':
        poly(d,[(58*s,4*s),(108*s,27*s),(98*s,84*s),(58*s,112*s),(18*s,84*s),(8*s,27*s)],MAGIC_GOLD)
        poly(d,[(58*s,10*s),(101*s,30*s),(91*s,80*s),(58*s,104*s),(25*s,80*s),(15*s,30*s)],MAGIC_DARK)
        poly(d,[(58*s,18*s),(92*s,34*s),(84*s,75*s),(58*s,94*s),(32*s,75*s),(24*s,34*s)],(7,70,137,255))
        radius=29*s
    else:
        radius=48*s
        ellipse(d,(cx-radius,cy-radius,cx+radius,cy+radius),TRAN,ring,4*s)

    for a in [-90,-18,54,126,198]:
        ar=math.radians(a)
        r1=9*s; r2=39*s if kind!='magic' else 24*s
        spread=.22
        pts=[]
        for rr,off in [(r1,-.55),(r2,-spread),(r2,.22),(r1,.55)]:
            aa=ar+off
            pts.append((cx+math.cos(aa)*rr,cy+math.sin(aa)*rr))
        poly(d,pts,blade)
    ellipse(d,(cx-8*s,cy-8*s,cx+8*s,cy+8*s),hub)
    ellipse(d,(cx-4*s,cy-4*s,cx+4*s,cy+4*s),ring)

    if kind=='magic':
        for side in (-1,1):
            base=58*s + side*50*s
            for i in range(6):
                y=(35+i*9)*s
                x=base + side*(i*1.7*s)
                ellipse(d,(x-5*s,y-3*s,x+5*s,y+3*s),MAGIC_GOLD)
    return im

for k in ('classic','soccer','kittens','magic'):
    save(fan(k),f'fan_{k}.png')

s=4; im=img(74*s,56*s); d=ImageDraw.Draw(im)
poly(d,[(7*s,33*s),(14*s,4*s),(30*s,27*s)],KITTEN_CREAM)
poly(d,[(44*s,27*s),(60*s,4*s),(67*s,33*s)],KITTEN_CREAM)
poly(d,[(12*s,27*s),(16*s,11*s),(26*s,27*s)],KITTEN_LIGHT)
poly(d,[(48*s,27*s),(58*s,11*s),(62*s,27*s)],KITTEN_LIGHT)
ellipse(d,(8*s,20*s,66*s,72*s),KITTEN_CREAM)
ellipse(d,(23*s,33*s,38*s,49*s),WHITE); ellipse(d,(36*s,33*s,51*s,49*s),WHITE)
for x in (25,49):
    line(d,[(x-6)*s,34*s,x*s,36*s,(x+6)*s,34*s],KITTEN_DARK,1*s)
poly(d,[(37*s,39*s),(33*s,43*s),(41*s,43*s)],KITTEN_PINK)
line(d,[(37*s,43*s),(37*s,48*s)],KITTEN_DARK,1*s)
for yy in (40,46):
    line(d,[(28*s,yy*s),(4*s,(yy-2)*s)],KITTEN_DARK,1*s)
    line(d,[(46*s,yy*s),(70*s,(yy-2)*s)],KITTEN_DARK,1*s)
save(im,'kitten_peek.png')

s=4; im=img(118*s,74*s); d=ImageDraw.Draw(im)
body=(223,205,211,255)
ellipse(d,(22*s,27*s,105*s,69*s),body)
ellipse(d,(54*s,34*s,94*s,62*s),(250,244,246,255))
ellipse(d,(14*s,20*s,55*s,57*s),body)
poly(d,[(17*s,27*s),(20*s,8*s),(33*s,24*s)],body)
poly(d,[(38*s,24*s),(48*s,8*s),(52*s,29*s)],body)
poly(d,[(21*s,24*s),(23*s,14*s),(30*s,24*s)],KITTEN_LIGHT)
poly(d,[(41*s,23*s),(47*s,14*s),(49*s,27*s)],KITTEN_LIGHT)
line(d,[(24*s,38*s),(31*s,40*s)],KITTEN_DARK,1*s); line(d,[(38*s,40*s),(45*s,38*s)],KITTEN_DARK,1*s)
poly(d,[(35*s,43*s),(31*s,46*s),(39*s,46*s)],KITTEN_PINK)
line(d,[(94*s,54*s),(110*s,45*s),(113*s,30*s),(108*s,22*s)],body,7*s)
poly(d,[(6*s,52*s),(3*s,47*s),(5*s,43*s),(9*s,45*s),(12*s,43*s),(14*s,47*s),(11*s,52*s)],KITTEN_PINK)
for x,y,sz in [(76,18,5),(88,11,6),(101,3,7)]:
    line(d,[(x-sz)*s,y*s,(x+sz)*s,y*s,(x-sz)*s,(y+sz)*s,(x+sz)*s,(y+sz)*s],KITTEN_DARK,1*s)
save(im,'kitten_sleep.png')

s=4; im=img(60*s,68*s); d=ImageDraw.Draw(im)
def paw(cx,cy):
    ellipse(d,((cx-7)*s,(cy-5)*s,(cx+7)*s,(cy+7)*s),(176,65,116,230))
    for ox,oy in [(-9,-8),(-3,-12),(4,-12),(10,-8)]:
        ellipse(d,((cx+ox-3)*s,(cy+oy-3)*s,(cx+ox+3)*s,(cy+oy+3)*s),(176,65,116,230))
paw(18,23); paw(41,49); save(im,'kitten_paws.png')

s=4; im=img(230*s,78*s); d=ImageDraw.Draw(im)
ellipse(d,(88*s,-18*s,144*s,38*s),(217,237,255,230))
for x,w,h in [(2,27,40),(31,24,57),(58,29,46),(91,31,66),(126,25,48),(154,28,59),(186,25,43),(213,16,35)]:
    y=72-h
    rect(d,(x*s,y*s,(x+w)*s,72*s),MAGIC_DARK)
    poly(d,[(x*s,y*s),((x+w/2)*s,(y-15)*s),((x+w)*s,y*s)],MAGIC_DARK)
    for wx in range(x+7,x+w-4,11):
        rect(d,(wx*s,(y+14)*s,(wx+3)*s,(y+23)*s),(255,210,80,255))
rect(d,(0,70*s,230*s,76*s),(3,25,58,255)); save(im,'magic_castle.png')

s=4; im=img(56*s,72*s); d=ImageDraw.Draw(im)
line(d,[(18*s,14*s),(20*s,5*s),(28*s,1*s),(36*s,5*s),(38*s,14*s)],MAGIC_GOLD,3*s)
poly(d,[(12*s,20*s),(44*s,20*s),(38*s,59*s),(18*s,59*s)],(177,109,18,255))
poly(d,[(18*s,25*s),(38*s,25*s),(34*s,52*s),(22*s,52*s)],(241,186,66,255))
line(d,[(19*s,27*s),(35*s,51*s)],MAGIC_CREAM,2*s); line(d,[(37*s,27*s),(21*s,51*s)],MAGIC_CREAM,2*s)
rect(d,(17*s,58*s,39*s,65*s),MAGIC_GOLD); save(im,'magic_lantern.png')

s=4; im=img(62*s,78*s); d=ImageDraw.Draw(im)
cream=(246,237,219,255); tan=(206,165,111,255); brown=(84,51,33,255)
ellipse(d,(9*s,5*s,53*s,56*s),cream)
poly(d,[(9*s,38*s),(0,62*s),(21*s,55*s)],tan); poly(d,[(53*s,38*s),(62*s,62*s),(41*s,55*s)],tan)
ellipse(d,(12*s,12*s,31*s,34*s),tan); ellipse(d,(31*s,12*s,50*s,34*s),tan)
ellipse(d,(18*s,18*s,26*s,28*s),brown); ellipse(d,(36*s,18*s,44*s,28*s),brown)
ellipse(d,(20*s,20*s,23*s,23*s),WHITE); ellipse(d,(38*s,20*s,41*s,23*s),WHITE)
poly(d,[(31*s,29*s),(27*s,35*s),(35*s,35*s)],(192,130,55,255))
rect(d,(4*s,61*s,58*s,67*s),(87,57,42,255)); rect(d,(8*s,68*s,54*s,73*s),(194,143,68,255)); save(im,'magic_owl.png')

s=4; im=img(36*s,70*s); d=ImageDraw.Draw(im)
feather=(205,181,147,255); shaft=(138,103,72,255)
poly(d,[(28*s,3*s),(35*s,12*s),(30*s,30*s),(23*s,45*s),(13*s,59*s),(7*s,61*s),(10*s,48*s),(9*s,34*s),(16*s,18*s)],feather)
line(d,[(8*s,63*s),(29*s,7*s)],shaft,2*s)
for y,x1,x2 in [(21,16,8),(29,18,7),(37,21,10),(46,18,8),(18,20,31),(27,22,32),(36,20,29)]:
    line(d,[(x1*s,y*s),(x2*s,(y+4)*s)],shaft,1*s)
rect(d,(3*s,59*s,14*s,69*s),(110,66,30,255)); save(im,'magic_quill.png')

s=4; im=img(36*s,36*s); d=ImageDraw.Draw(im)
line(d,[(2*s,33*s),(2*s,7*s),(31*s,7*s)],MAGIC_GOLD,3*s)
line(d,[(7*s,31*s),(7*s,13*s),(28*s,13*s)],MAGIC_CREAM,1*s)
ellipse(d,(0,4*s,6*s,10*s),MAGIC_GOLD); ellipse(d,(28*s,4*s,34*s,10*s),MAGIC_GOLD)
line(d,[(8*s,25*s),(14*s,17*s),(22*s,18*s),(19*s,25*s),(13*s,26*s)],MAGIC_GOLD,2*s); save(im,'magic_corner_tl.png')

s=4; im=img(76*s,44*s); d=ImageDraw.Draw(im)
frame=(230,247,239,235); net=(96,199,133,185)
line(d,[(8*s,9*s),(55*s,9*s),(55*s,36*s),(8*s,36*s),(8*s,9*s)],frame,3*s)
line(d,[(55*s,9*s),(70*s,3*s),(70*s,31*s),(55*s,36*s)],frame,2*s)
line(d,[(8*s,9*s),(23*s,3*s),(70*s,3*s)],frame,2*s)
for x in range(15,56,10): line(d,[(x*s,9*s),(x*s,36*s)],net,1*s)
for y in range(15,37,7): line(d,[(8*s,y*s),(55*s,y*s)],net,1*s)
for x in (58,63,68): line(d,[(x*s,8*s),(x*s,31*s)],net,1*s)
for y in (10,17,24,31): line(d,[(55*s,y*s),(70*s,(y-4)*s)],net,1*s)
save(im,'soccer_goal.png')

s=2; im=img(800*s,408*s); d=ImageDraw.Draw(im)
bands=[(5,61,34,255),(6,74,40,255)]
for i,x in enumerate(range(0,800,80)):
    rect(d,(x*s,0,min(800,x+80)*s,408*s),bands[i%2])
for y in range(0,408,14):
    for x in range((y//14)%2*18,800,36):
        line(d,[(x*s,y*s),((x+18)*s,(y-6)*s)],(18,94,51,40),1*s)
fieldline=(122,195,147,115)
inset=18
line(d,[(inset*s,inset*s),((800-inset)*s,inset*s),((800-inset)*s,(408-inset)*s),(inset*s,(408-inset)*s),(inset*s,inset*s)],fieldline,2*s)
line(d,[(400*s,inset*s),(400*s,(408-inset)*s)],fieldline,2*s)
ellipse(d,(340*s,144*s,460*s,264*s),TRAN,fieldline,2*s)
ellipse(d,(397*s,201*s,403*s,207*s),fieldline)
for left in (True,False):
    gx=inset if left else 800-inset
    px=132 if left else 668
    sx=70 if left else 730
    line(d,[(gx*s,102*s),(px*s,102*s),(px*s,306*s),(gx*s,306*s)],fieldline,2*s)
    line(d,[(gx*s,148*s),(sx*s,148*s),(sx*s,260*s),(gx*s,260*s)],fieldline,2*s)
    spot=92 if left else 708
    ellipse(d,((spot-3)*s,201*s,(spot+3)*s,207*s),fieldline)
save(im,'soccer_field.png')

print(f"Wrote 18 high-resolution transparent PNG assets to {OUT}")
