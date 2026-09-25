#!/usr/bin/env python3
"""Create high-resolution transparent PNG theme artwork from the approved mockup designs.

This is a source-art maintenance tool, not part of the PlatformIO build. The
firmware build consumes the committed PNG files through generate_theme_art.py.
"""

from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
import math

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets" / "theme-sprites"
OUT.mkdir(parents=True, exist_ok=True)

def rgba(hexv, a=255):
    return ((hexv>>16)&255,(hexv>>8)&255,hexv&255,a)


def home_icon2(color, magic=False):
    S=4; w=h=56*S
    im=Image.new("RGBA",(w,h),(0,0,0,0)); d=ImageDraw.Draw(im)
    if not magic:
        roof=[(7*S,26*S),(28*S,7*S),(49*S,26*S),(44*S,31*S),(28*S,17*S),(12*S,31*S)]
        d.polygon(roof,fill=color)
        d.rounded_rectangle([13*S,24*S,43*S,49*S], radius=3*S, fill=color)
        d.rounded_rectangle([24*S,34*S,32*S,49*S], radius=S, fill=(12,25,35,255))
    else:
        gold=rgba(0xE5B95F); deep=rgba(0x113767); blue=rgba(0x23A8FF); lite=rgba(0x76D5FF)
        d.polygon([(5*S,25*S),(28*S,3*S),(51*S,25*S),(47*S,51*S),(9*S,51*S)], fill=gold)
        d.polygon([(9*S,26*S),(28*S,8*S),(47*S,26*S),(43*S,47*S),(13*S,47*S)], fill=deep)
        d.polygon([(14*S,27*S),(28*S,14*S),(42*S,27*S),(39*S,44*S),(17*S,44*S)], fill=blue)
        d.rounded_rectangle([25*S,33*S,31*S,44*S],radius=S,fill=deep)
        d.ellipse([26*S,10*S,30*S,14*S],fill=lite)
    return im


def fan_simple2(theme):
    S=4; W,H=142*S,116*S
    im=Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    palette={
        'classic':(rgba(0x55B9FF),rgba(0x8FD5FF),rgba(0x1F7FC4)),
        'soccer':(rgba(0x50F29A),rgba(0x95FFD0),rgba(0x19C962)),
        'kittens':(rgba(0xFF91BF),rgba(0xFFC0D7),rgba(0xE85F9D)),
    }
    ring,blade,hub=palette[theme]
    cx,cy=58*S,58*S
    d.ellipse([8*S,8*S,108*S,108*S], outline=ring, width=4*S)
    base=[(cx+3*S,cy-7*S),(cx+13*S,cy-35*S),(cx+1*S,cy-44*S),(cx-12*S,cy-33*S),(cx-10*S,cy-9*S)]
    for a in [0,90,180,270]:
        pts=[]; ca,sa=math.cos(math.radians(a)),math.sin(math.radians(a))
        for x,y in base:
            dx,dy=x-cx,y-cy
            pts.append((cx+dx*ca-dy*sa,cy+dx*sa+dy*ca))
        d.polygon(pts,fill=blade)
    d.ellipse([49*S,49*S,67*S,67*S],fill=hub)
    d.ellipse([53*S,53*S,63*S,63*S],fill=ring)
    return im


def fan_magic():
    S=4; W,H=142*S,116*S
    im=Image.new("RGBA",(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    gold=rgba(0xE3B85E); gold2=rgba(0xF3D587); navy=rgba(0x0B2A55); blue=rgba(0x2C9BEB); lite=rgba(0x9DDCFF)
    cx,cy=71*S,60*S
    for side in (-1,1):
        for i in range(7):
            y=(28+i*10)*S
            x=(37 if side<0 else 105)*S
            ang=(-35+i*7) if side<0 else (215-i*7)
            leaf=Image.new("RGBA",(20*S,10*S),(0,0,0,0)); ld=ImageDraw.Draw(leaf)
            ld.ellipse([1*S,1*S,19*S,9*S],fill=gold)
            leaf=leaf.rotate(ang,resample=Image.Resampling.BICUBIC,expand=True)
            im.alpha_composite(leaf,(int(x-leaf.width/2),int(y-leaf.height/2)))
    d=ImageDraw.Draw(im)
    shield=[(71*S,15*S),(111*S,30*S),(103*S,84*S),(71*S,107*S),(39*S,84*S),(31*S,30*S)]
    inner=[(71*S,21*S),(104*S,34*S),(97*S,79*S),(71*S,98*S),(45*S,79*S),(38*S,34*S)]
    d.polygon(shield,fill=gold)
    d.polygon(inner,fill=navy)
    d.ellipse([47*S,35*S,95*S,83*S],outline=blue,width=3*S)
    fc=(71*S,59*S)
    base=[(fc[0]+1*S,fc[1]-5*S),(fc[0]+8*S,fc[1]-18*S),(fc[0],fc[1]-24*S),(fc[0]-9*S,fc[1]-18*S),(fc[0]-7*S,fc[1]-5*S)]
    for a in [0,90,180,270]:
        ca,sa=math.cos(math.radians(a)),math.sin(math.radians(a))
        pts=[]
        for x,y in base:
            dx,dy=x-fc[0],y-fc[1]
            pts.append((fc[0]+dx*ca-dy*sa,fc[1]+dx*sa+dy*ca))
        d.polygon(pts,fill=lite)
    d.ellipse([65*S,53*S,77*S,65*S],fill=gold2)
    d.ellipse([68*S,56*S,74*S,62*S],fill=blue)
    return im


def kitten_peek():
    S=4; W,H=74*S,56*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    fur=rgba(0xE7D7D8); fur2=rgba(0xCDBDC2); pink=rgba(0xF39AB4); dark=rgba(0x5B3D47); white=rgba(0xFFF8FA)
    d.polygon([(10*S,27*S),(16*S,5*S),(29*S,23*S)],fill=fur2)
    d.polygon([(45*S,23*S),(58*S,5*S),(64*S,27*S)],fill=fur2)
    d.polygon([(14*S,23*S),(17*S,11*S),(25*S,22*S)],fill=pink)
    d.polygon([(49*S,22*S),(57*S,11*S),(60*S,23*S)],fill=pink)
    d.ellipse([9*S,17*S,65*S,69*S],fill=fur)
    d.ellipse([26*S,34*S,48*S,52*S],fill=white)
    d.arc([20*S,28*S,31*S,38*S],0,180,fill=dark,width=2*S)
    d.arc([43*S,28*S,54*S,38*S],0,180,fill=dark,width=2*S)
    d.polygon([(37*S,39*S),(33*S,42*S),(41*S,42*S)],fill=pink)
    d.line([(37*S,42*S),(37*S,46*S)],fill=dark,width=S)
    d.arc([30*S,42*S,37*S,49*S],0,120,fill=dark,width=S)
    d.arc([37*S,42*S,44*S,49*S],60,180,fill=dark,width=S)
    for yy,off in [(40,-1),(45,1)]:
        d.line([(29*S,yy*S),(11*S,(yy+off)*S)],fill=dark,width=S)
        d.line([(45*S,yy*S),(63*S,(yy+off)*S)],fill=dark,width=S)
    return im


def kitten_sleep():
    S=4; W,H=118*S,74*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    fur=rgba(0xD8C7CA); fur2=rgba(0xBBAAAF); pink=rgba(0xF29AB3); dark=rgba(0x5B3D47); white=rgba(0xF9F2F4)
    d.ellipse([31*S,30*S,103*S,68*S],fill=fur)
    d.ellipse([57*S,37*S,98*S,64*S],fill=white)
    d.ellipse([18*S,24*S,58*S,62*S],fill=fur)
    d.polygon([(22*S,31*S),(25*S,12*S),(36*S,28*S)],fill=fur2)
    d.polygon([(40*S,28*S),(52*S,12*S),(54*S,33*S)],fill=fur2)
    d.polygon([(25*S,27*S),(27*S,18*S),(33*S,28*S)],fill=pink)
    d.polygon([(44*S,27*S),(50*S,18*S),(51*S,29*S)],fill=pink)
    d.arc([27*S,36*S,35*S,43*S],0,180,fill=dark,width=2*S)
    d.arc([40*S,36*S,48*S,43*S],0,180,fill=dark,width=2*S)
    d.polygon([(38*S,44*S),(35*S,46*S),(41*S,46*S)],fill=pink)
    d.arc([78*S,19*S,116*S,62*S],300,80,fill=fur2,width=7*S)
    def heart(cx,cy,sz):
        d.ellipse([(cx-sz)*S,(cy-sz)*S,cx*S,cy*S],fill=pink)
        d.ellipse([cx*S,(cy-sz)*S,(cx+sz)*S,cy*S],fill=pink)
        d.polygon([((cx-sz)*S,(cy-2)*S),((cx+sz)*S,(cy-2)*S),(cx*S,(cy+sz+2)*S)],fill=pink)
    heart(10,50,3); heart(108,48,3)
    for (x,y,s) in [(69,24,4),(80,17,5),(92,9,6)]:
        d.line([(x*S,y*S),((x+s)*S,y*S),(x*S,(y+s)*S),((x+s)*S,(y+s)*S)],fill=dark,width=S)
    return im


def kitten_paws():
    S=4; W,H=60*S,68*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    pink=rgba(0xD95691,200)
    def paw(cx,cy,scale=1):
        d.ellipse([(cx-7)*S,(cy-5)*S,(cx+7)*S,(cy+7)*S],fill=pink)
        for ox,oy in [(-9,-8),(-3,-12),(4,-12),(10,-7)]:
            d.ellipse([(cx+ox-3)*S,(cy+oy-3)*S,(cx+ox+3)*S,(cy+oy+3)*S],fill=pink)
    paw(18,27); paw(40,51)
    return im


def soccer_goal():
    S=4; W,H=76*S,44*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    white=rgba(0xE8FFF0,230); green=rgba(0x66E38D,140)
    front=[(8*S,8*S),(58*S,8*S),(58*S,37*S),(8*S,37*S)]
    back=[(16*S,3*S),(70*S,3*S),(70*S,32*S),(58*S,37*S)]
    for a,b in [(front[0],front[1]),(front[1],front[2]),(front[2],front[3]),(front[3],front[0]),
                (front[0],back[0]),(front[1],back[1]),(front[2],back[2]),(back[0],back[1]),(back[1],back[2])]:
        d.line([a,b],fill=white,width=2*S)
    for t in [0.2,0.4,0.6,0.8]:
        x1=8+(58-8)*t; x2=16+(70-16)*t
        d.line([(x1*S,8*S),(x2*S,3*S),(x2*S,32*S),(x1*S,37*S)],fill=green,width=S)
    for y in [13,19,25,31]:
        d.line([(8*S,y*S),(58*S,y*S),(70*S,(y-5)*S)],fill=green,width=S)
    return im


def magic_castle():
    S=4; W,H=230*S,78*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    dark=rgba(0x071B3B); mid=rgba(0x0D2E59); gold=rgba(0xFFD166); moon=rgba(0xD9ECFF,230)
    d.ellipse([57*S,-4*S,126*S,65*S],fill=moon)
    cloud=rgba(0x2C5C90,160)
    for x,y,r in [(160,54,16),(176,49,20),(196,55,15),(215,48,18)]:
        d.ellipse([(x-r)*S,(y-r/2)*S,(x+r)*S,(y+r/2)*S],fill=cloud)
    towers=[(4,31,16,54),(18,18,14,67),(34,28,18,58),(55,12,15,74),(74,26,18,60),(94,8,18,70),(116,23,15,61),(137,16,18,68),(160,30,16,54),(181,20,18,64),(205,32,15,50)]
    for x,top,wid,base in towers:
        d.rectangle([x*S,top*S,(x+wid)*S,72*S],fill=dark)
        d.polygon([(x*S,top*S),((x+wid/2)*S,(top-15)*S),((x+wid)*S,top*S)],fill=dark)
        for wy in range(top+14,68,17):
            wx=x+wid/2-1
            d.rectangle([wx*S,wy*S,(wx+3)*S,(wy+7)*S],fill=gold)
    d.rectangle([0,67*S,W,78*S],fill=dark)
    return im


def magic_lantern2():
    S=4; W,H=56*S,72*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    gold=rgba(0xE7B75A); deep=rgba(0x6E3A12); glass=rgba(0xFFD56A,180); glow=rgba(0xFFF0BA,120)
    d.ellipse([17*S,24*S,39*S,52*S],fill=glow)
    d.arc([18*S,0,38*S,20*S],180,360,fill=gold,width=3*S)
    d.line([(28*S,5*S),(28*S,13*S)],fill=gold,width=2*S)
    d.polygon([(14*S,18*S),(42*S,18*S),(38*S,57*S),(18*S,57*S)],fill=deep)
    d.polygon([(19*S,22*S),(37*S,22*S),(34*S,52*S),(22*S,52*S)],fill=glass)
    d.line([(18*S,18*S),(38*S,57*S)],fill=gold,width=2*S)
    d.line([(42*S,18*S),(18*S,57*S)],fill=gold,width=2*S)
    d.rectangle([16*S,15*S,40*S,21*S],fill=gold)
    d.rectangle([17*S,57*S,39*S,63*S],fill=gold)
    d.polygon([(22*S,15*S),(28*S,10*S),(34*S,15*S)],fill=gold)
    return im


def magic_owl():
    S=4; W,H=62*S,78*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    white=rgba(0xF4E8DE); tan=rgba(0xC9A57C); brown=rgba(0x6E4A2D); dark=rgba(0x37251B); gold=rgba(0xD3A85A)
    d.ellipse([14*S,19*S,48*S,69*S],fill=white)
    d.polygon([(15*S,38*S),(4*S,63*S),(22*S,58*S)],fill=tan)
    d.polygon([(47*S,38*S),(58*S,63*S),(40*S,58*S)],fill=tan)
    d.ellipse([9*S,4*S,53*S,43*S],fill=white)
    d.ellipse([14*S,12*S,31*S,31*S],fill=tan); d.ellipse([31*S,12*S,48*S,31*S],fill=tan)
    d.ellipse([20*S,17*S,27*S,25*S],fill=dark); d.ellipse([35*S,17*S,42*S,25*S],fill=dark)
    d.ellipse([22*S,18*S,24*S,20*S],fill=(255,255,255,255)); d.ellipse([37*S,18*S,39*S,20*S],fill=(255,255,255,255))
    d.polygon([(31*S,25*S),(27*S,31*S),(35*S,31*S)],fill=gold)
    d.rectangle([5*S,65*S,57*S,69*S],fill=brown)
    d.rectangle([10*S,69*S,52*S,74*S],fill=gold)
    return im


def magic_quill():
    S=4; W,H=36*S,70*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    cream=rgba(0xCDBB9D); tan=rgba(0x9E8062); gold=rgba(0xD4A75A); dark=rgba(0x3C2A20)
    feather=[(26,5),(33,13),(30,26),(25,38),(18,51),(11,58),(13,47),(11,35),(15,22)]
    d.polygon([(x*S,y*S) for x,y in feather],fill=cream)
    d.line([(11*S,58*S),(27*S,8*S)],fill=tan,width=2*S)
    for a,b,c,dv in [(15,49,8,45),(18,40,10,35),(21,31,13,27),(24,21,31,17),(19,38,28,32)]:
        d.line([(a*S,b*S),(c*S,dv*S)],fill=tan,width=S)
    d.rectangle([4*S,57*S,16*S,68*S],fill=dark)
    d.rectangle([6*S,54*S,14*S,58*S],fill=gold)
    return im


def magic_corner():
    S=4; W=H=36*S
    im=Image.new('RGBA',(W,H),(0,0,0,0)); d=ImageDraw.Draw(im)
    gold=rgba(0xD5A75D); lite=rgba(0xF3D48C)
    d.line([(2*S,33*S),(2*S,7*S),(28*S,7*S)],fill=gold,width=2*S)
    d.arc([3*S,7*S,18*S,22*S],180,300,fill=lite,width=2*S)
    d.arc([12*S,2*S,30*S,20*S],90,220,fill=gold,width=2*S)
    d.arc([2*S,18*S,18*S,34*S],250,30,fill=gold,width=2*S)
    d.ellipse([27*S,4*S,32*S,9*S],fill=lite)
    d.ellipse([0*S,30*S,5*S,35*S],fill=lite)
    return im


def soccer_field_clean():
    S=2; W,H=800*S,408*S
    im=Image.new('RGBA',(W,H),rgba(0x082B18,255)); d=ImageDraw.Draw(im)
    cols=[rgba(0x0A351E,255),rgba(0x0D4024,255)]
    for i,x in enumerate(range(0,W,80*S)):
        d.rectangle([x,0,min(W,x+80*S),H],fill=cols[i%2])
    for offs,alpha,width in [(0,42,5),(55,28,3),(110,20,2)]:
        d.line([(-80*S,(70+offs)*S),(360*S,-20*S)],fill=(65,255,140,alpha),width=width*S)
    line=rgba(0xB9E6C4,95); bright=rgba(0xD6F6DE,115)
    inset=18*S
    d.rectangle([inset,inset,W-inset,H-inset],outline=line,width=2*S)
    cx=W//2; cy=H//2
    d.line([(cx,inset),(cx,H-inset)],fill=line,width=2*S)
    d.ellipse([cx-60*S,cy-60*S,cx+60*S,cy+60*S],outline=line,width=2*S)
    d.ellipse([cx-3*S,cy-3*S,cx+3*S,cy+3*S],fill=bright)
    for left in [True,False]:
        gx=inset if left else W-inset
        px=132*S if left else W-132*S
        sx=70*S if left else W-70*S
        y1=102*S;y2=H-102*S; sy1=148*S; sy2=H-148*S
        d.line([(gx,y1),(px,y1),(px,y2),(gx,y2)],fill=line,width=2*S)
        d.line([(gx,sy1),(sx,sy1),(sx,sy2),(gx,sy2)],fill=line,width=2*S)
        spot=(92*S if left else W-92*S)
        d.ellipse([spot-2*S,cy-2*S,spot+2*S,cy+2*S],fill=bright)
    return im


def write(img, filename):
    img.save(OUT / filename, format="PNG", optimize=True)


def main():
    write(home_icon2(rgba(0x3DA7FF)), "header_classic.png")
    write(home_icon2(rgba(0x39E66D)), "header_soccer.png")
    write(home_icon2(rgba(0xFF83B8)), "header_kittens.png")
    write(home_icon2(rgba(0x3DA7FF), magic=True), "header_magic.png")
    write(fan_simple2("classic"), "fan_classic.png")
    write(fan_simple2("soccer"), "fan_soccer.png")
    write(fan_simple2("kittens"), "fan_kittens.png")
    write(fan_magic(), "fan_magic.png")
    write(kitten_peek(), "kitten_peek.png")
    write(kitten_sleep(), "kitten_sleep.png")
    write(kitten_paws(), "kitten_paws.png")
    write(soccer_goal(), "soccer_goal.png")
    write(soccer_field_clean(), "soccer_field.png")
    write(magic_castle(), "magic_castle.png")
    write(magic_lantern2(), "magic_lantern.png")
    write(magic_owl(), "magic_owl.png")
    write(magic_quill(), "magic_quill.png")
    write(magic_corner(), "magic_corner_tl.png")


if __name__ == "__main__":
    main()
