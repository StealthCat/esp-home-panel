#!/usr/bin/env python3
"""Generate companion theme sprites matching the approved reference mockups."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont
import math

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"assets"/"theme-sprites"
OUT.mkdir(parents=True,exist_ok=True)
S=4

def canvas(w,h): return Image.new("RGBA",(w*S,h*S),(0,0,0,0))
def P(v): return int(round(v*S))
def box(a,b,c,d): return (P(a),P(b),P(c),P(d))
def line(d,pts,fill,width): d.line([(P(x),P(y)) for x,y in pts],fill=fill,width=max(1,P(width)),joint="curve")
def ell(d,b,fill,outline=None,width=1): d.ellipse(box(*b),fill=fill,outline=outline,width=max(1,P(width)))
def poly(d,pts,fill): d.polygon([(P(x),P(y)) for x,y in pts],fill=fill)
def rr(d,b,r,fill,outline=None,width=1): d.rounded_rectangle(box(*b),radius=P(r),fill=fill,outline=outline,width=max(1,P(width)))
def save(im,name): im.save(OUT/name,optimize=True,compress_level=9)

def glow(im,shape,color,r=3):
    layer=Image.new("RGBA",im.size,(0,0,0,0))
    mask=Image.new("L",im.size,0); md=ImageDraw.Draw(mask); shape(md)
    mask=mask.filter(ImageFilter.GaussianBlur(P(r)))
    tint=Image.new("RGBA",im.size,color); tint.putalpha(mask.point(lambda x:x*color[3]//255))
    im.alpha_composite(tint)

def star(d,x,y,r,color):
    pts=[]
    for i in range(8):
        a=-math.pi/2+i*math.pi/4
        q=r if i%2==0 else r*.22
        pts.append((x+math.cos(a)*q,y+math.sin(a)*q))
    poly(d,pts,color)

def soccer_ball():
    im=canvas(64,64); d=ImageDraw.Draw(im)
    glow(im,lambda q:q.ellipse(box(4,4,60,60),fill=255),(45,255,104,120),3)
    ell(d,(4,4,60,60),(45,220,92,255),(139,255,154,255),2)
    ell(d,(8,8,56,56),(32,162,72,255))
    cx=cy=32
    p=[]
    for i in range(5):
        a=-math.pi/2+i*2*math.pi/5
        p.append((cx+math.cos(a)*8,cy+math.sin(a)*8))
    poly(d,p,(3,40,29,255))
    for i in range(5):
        a=-math.pi/2+i*2*math.pi/5
        x=cx+math.cos(a)*18; y=cy+math.sin(a)*18
        h=[]
        for j in range(6):
            aa=j*math.pi/3
            h.append((x+math.cos(aa)*7,y+math.sin(aa)*7))
        poly(d,h,(5,61,39,255))
        line(d,[(cx+math.cos(a)*8,cy+math.sin(a)*8),(x,y)],(3,44,31,255),1)
    ell(d,(12,9,28,20),(255,255,255,65))
    save(im,"soccer_ball.png")

def soccer_streak():
    im=canvas(230,58); g=Image.new("RGBA",im.size,(0,0,0,0)); d=ImageDraw.Draw(g)
    paths=[([(5,49),(60,36),(112,24),(165,10),(226,2)],(60,255,105,210),2.2),
           ([(17,55),(74,42),(132,29),(188,16),(226,12)],(203,255,73,160),1.2),
           ([(47,52),(103,42),(158,31),(211,23)],(51,223,93,130),.8)]
    for pts,col,w in paths: line(d,pts,col,w)
    for x,y,r in [(60,36,2),(165,10,2.4),(211,23,1.5)]:
        ell(d,(x-r,y-r,x+r,y+r),(226,255,91,220))
    g=g.filter(ImageFilter.GaussianBlur(P(1.4))); im.alpha_composite(g)
    save(im,"soccer_streak.png")

def kitten_sit():
    im=canvas(72,92); d=ImageDraw.Draw(im)
    glow(im,lambda q:q.ellipse(box(15,31,58,84),fill=255),(236,93,149,75),2)
    ell(d,(17,34,57,83),(239,216,213,255))
    ell(d,(11,10,61,60),(251,235,228,255))
    poly(d,[(14,27),(20,2),(32,20)],(245,221,216,255))
    poly(d,[(41,20),(54,2),(59,28)],(245,221,216,255))
    poly(d,[(19,21),(21,9),(29,20)],(252,167,186,255))
    poly(d,[(45,20),(53,9),(56,22)],(252,167,186,255))
    for x in (27,45):
        ell(d,(x-3.8,29,x+3.8,38),(76,35,51,255))
        ell(d,(x-1.7,30,x+.2,32),(255,255,255,255))
    poly(d,[(36,39),(33,42),(39,42)],(226,85,116,255))
    for yy in (40,45):
        line(d,[(30,yy),(8,yy-2)],(130,57,79,210),.7)
        line(d,[(43,yy),(65,yy-2)],(130,57,79,210),.7)
    ell(d,(20,70,31,88),(248,227,220,255)); ell(d,(40,70,51,88),(248,227,220,255))
    line(d,[(51,77),(64,79),(68,69),(64,60)],(227,198,198,255),6)
    save(im,"kitten_sit.png")

def caption(name,lines,w,h):
    im=canvas(w,h); d=ImageDraw.Draw(im)
    try: font=ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",P(15))
    except OSError: font=ImageFont.load_default()
    y=P(2)
    for i,t in enumerate(lines):
        x=P(3+(i%2)*4)
        d.text((x+P(1),y+P(1)),t,font=font,fill=(120,39,73,235))
        d.text((x,y),t,font=font,fill=(255,188,216,255))
        y+=P(21)
    save(im,name)

def magic_window():
    im=canvas(72,116); d=ImageDraw.Draw(im)
    pts=[(7,108),(7,43),(12,25),(22,12),(36,4),(50,12),(60,25),(65,43),(65,108)]
    glow(im,lambda q:q.polygon([(P(x),P(y)) for x,y in pts],fill=255),(28,130,247,95),3)
    poly(d,pts,(5,42,101,255))
    inner=[(13,103),(13,45),(18,29),(27,19),(36,13),(45,19),(54,29),(59,45),(59,103)]
    poly(d,inner,(12,78,163,255))
    line(d,[(36,13),(36,103)],(3,23,58,255),2)
    line(d,[(14,49),(58,49)],(3,23,58,255),1.4)
    line(d,[(14,74),(58,74)],(3,23,58,255),1.4)
    line(d,[(18,31),(36,49),(54,31)],(3,23,58,255),1.2)
    for x,y,r in [(27,31,1.4),(47,61,1.1),(31,83,1.2),(43,91,.9)]: star(d,x,y,r,(246,201,88,230))
    rr(d,(5,104,67,112),2,(5,37,83,255))
    save(im,"magic_window.png")

def magic_wave():
    im=canvas(240,58); g=Image.new("RGBA",im.size,(0,0,0,0)); d=ImageDraw.Draw(g)
    for off,col,w in [(0,(39,177,255,210),2),(7,(105,220,255,170),1.2),(-6,(244,199,88,125),.8)]:
        pts=[]
        for x in range(0,241,8): pts.append((x,29+off+9*math.sin((x/240)*math.pi*2.1)))
        line(d,pts,col,w)
    g=g.filter(ImageFilter.GaussianBlur(P(1.8))); im.alpha_composite(g)
    d=ImageDraw.Draw(im)
    for x in range(18,235,28):
        y=21+9*math.sin((x/240)*math.pi*2.1); star(d,x,y,1.5,(248,206,92,210))
    save(im,"magic_wave.png")

def magic_spark():
    im=canvas(54,72); d=ImageDraw.Draw(im)
    line(d,[(10,63),(39,20)],(142,81,29,255),4.2)
    line(d,[(11,62),(38,21)],(232,183,72,255),1.4)
    glow(im,lambda q:q.ellipse(box(28,5,53,31),fill=255),(255,208,74,185),5)
    star(d,42,17,9,(255,241,164,255))
    for x,y,r in [(31,8,2),(48,31,2),(28,30,1.4),(17,44,1.1)]: star(d,x,y,r,(249,202,82,230))
    save(im,"magic_spark.png")

soccer_ball(); soccer_streak(); kitten_sit()
caption("kitten_caption_home.png",["Good","Things","At Home"],104,82)
caption("kitten_caption_echo.png",["Small","Voices","Big Joy"],118,82)
magic_window(); magic_wave(); magic_spark()
print("Generated reference companion sprites")
