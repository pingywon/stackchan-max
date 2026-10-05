"""MAX skin preview — Max Headroom styled. Obeys the firmware Feature contract:
position -100..100, weight 0..100 (eyes: open, mouth: open), size -100..100,
rotation tenths of a degree CLOCKWISE (PIL needs -rot)."""
from PIL import Image, ImageDraw, ImageFilter
import math, os, random

HERE=os.path.dirname(os.path.abspath(__file__))

W,H=320,240; S=3
def clamp(v,lo,hi): return lo if v<lo else (hi if v>hi else v)
def mr(v,a,b,c,d): return c if b==a else c+(v-a)*(d-c)/(b-a)

# palette
BG_DEEP=(8,10,30); GRID=(0,150,220); GRID2=(190,40,150)
SKIN=(226,180,128); SKIN_SH=(190,142,96)
HAIR=(214,150,50); HAIR_HI=(240,190,86)
LENS=(14,14,26); GLINT=(120,230,255)
LIP=(150,40,60); TEETH=(248,244,236); MOUTH_DK=(40,12,20)
SUIT=(30,36,70); TIE=(220,60,60); SHIRT=(225,228,240)

def bg(draw, phase):
    draw.rectangle((0,0,W*S,H*S), fill=BG_DEEP)
    # perspective wireframe: horizontal bands converging + verticals sliding
    cy=H*S*0.62
    for i in range(1,11):
        t=(i+phase)%10/10.0
        y=cy+ (t**2.2)*H*S*0.75
        if y<H*S: draw.line((0,y,W*S,y), fill=GRID, width=max(1,int(1.6*S*t)))
    for i in range(-6,7):
        x=W*S/2 + i*W*S*0.16
        draw.line((x, cy, W*S/2 + i*W*S*0.62, H*S), fill=GRID, width=1*S)
    for i in range(5):
        yy=(( (i*0.2+phase*0.08)%1.0)**1.6)*cy
        draw.line((0,yy,W*S,yy), fill=GRID2, width=1*S)

def rot_rect(layer, cx, cy, w, h, ang, fill, radius=0):
    im=Image.new("RGBA",(int(w)+4,int(h)+4),(0,0,0,0))
    d=ImageDraw.Draw(im)
    if radius: d.rounded_rectangle((2,2,w,h),radius=radius,fill=fill)
    else: d.rectangle((2,2,w,h),fill=fill)
    im=im.rotate(-ang,expand=True,resample=Image.BICUBIC)
    layer.alpha_composite(im,(int(cx-im.width/2),int(cy-im.height/2)))

def render(f, phase=0.0, glitch=0.0):
    base=Image.new("RGBA",(W*S,H*S),(0,0,0,255))
    d=ImageDraw.Draw(base)
    bg(d, phase)

    fg=Image.new("RGBA",(W*S,H*S),(0,0,0,0))
    g=ImageDraw.Draw(fg)

    cx, cy = W*S/2, H*S/2

    # ---- head: blocky, slightly tapered jaw
    hw, hh = 96*S, 104*S
    hx, hy = cx, cy+6*S
    g.rounded_rectangle((hx-hw/2, hy-hh/2, hx+hw/2, hy+hh/2), radius=18*S, fill=SKIN)
    # jaw shading
    g.rounded_rectangle((hx-hw/2, hy+hh*0.22, hx+hw/2, hy+hh/2), radius=18*S, fill=SKIN_SH)
    g.rounded_rectangle((hx-hw/2, hy-hh/2, hx+hw/2, hy+hh*0.28), radius=18*S, fill=SKIN)

    # ---- hair: angular slabs, slicked back
    top=hy-hh/2
    g.polygon([(hx-hw/2-3*S, top+16*S),(hx-hw/2+2*S, top-16*S),
               (hx+hw/2-2*S, top-16*S),(hx+hw/2+3*S, top+16*S),
               (hx+hw/2-6*S, top+8*S),(hx-hw/2+6*S, top+8*S)], fill=HAIR)
    for i in range(5):
        x0=hx-hw/2+4*S+i*(hw-8*S)/5
        rot_rect(fg, x0+(hw-8*S)/10, top-4*S, (hw-8*S)/5*0.72, 22*S,
                 -12+i*6, HAIR_HI if i%2 else HAIR, radius=3*S)

    # ---- sunglasses (the eyes)
    for is_left in (True,False):
        e=f["leftEye"] if is_left else f["rightEye"]
        bx=-30*S if is_left else 30*S
        ex=hx+bx+mr(clamp(e["x"],-100,100),-100,100,-9,9)*S
        ey=hy-20*S+mr(clamp(e["y"],-100,100),-100,100,-8,8)*S
        lw=mr(clamp(e["size"],-100,100),-100,100,34,46)*S
        lh=mr(clamp(e["weight"],0,100),0,100,7,26)*S
        g.rounded_rectangle((ex-lw/2,ey-lh/2,ex+lw/2,ey+lh/2),radius=min(7*S,lh/2),fill=LENS)
        if lh>10*S:
            gw=lw*0.42; gh=lh*0.26
            g.rounded_rectangle((ex-lw*0.30-gw/2, ey-lh*0.22-gh/2,
                                 ex-lw*0.30+gw/2, ey-lh*0.22+gh/2),
                                radius=gh/2, fill=GLINT)
        # brow above lens, driven by rotation
        rot=e["rotation"]/10.0
        rot_rect(fg, ex, ey-lh/2-11*S, lw*1.02, 5*S, rot if is_left else rot,
                 HAIR, radius=2*S)
    # bridge
    g.rectangle((hx-8*S, hy-22*S, hx+8*S, hy-18*S), fill=LENS)

    # ---- grin
    m=f["mouth"]; wt=clamp(m["weight"],0,100)
    mw=mr(wt,0,100,74,84)*S; mh=mr(wt,0,100,10,34)*S
    mx=hx+mr(clamp(m["x"],-100,100),-100,100,-8,8)*S
    my=hy+26*S+mr(clamp(m["y"],-100,100),-100,100,-8,8)*S
    g.rounded_rectangle((mx-mw/2,my-mh/2,mx+mw/2,my+mh/2),radius=6*S,fill=LIP)
    iw,ih=mw-6*S, mh-6*S
    if ih>4*S:
        g.rounded_rectangle((mx-iw/2,my-ih/2,mx+iw/2,my+ih/2),radius=4*S,fill=MOUTH_DK)
        # teeth: top row always, bottom row when open
        th=min(ih*0.52, 11*S)
        g.rounded_rectangle((mx-iw/2,my-ih/2,mx+iw/2,my-ih/2+th),radius=2*S,fill=TEETH)
        if ih>16*S:
            g.rounded_rectangle((mx-iw/2,my+ih/2-th*0.8,mx+iw/2,my+ih/2),radius=2*S,fill=TEETH)
        for i in range(1,7):
            gx=mx-iw/2+i*iw/7
            g.line((gx,my-ih/2,gx,my-ih/2+th),fill=MOUTH_DK,width=max(1,int(0.9*S)))

    # ---- collar + tie
    g.polygon([(cx-46*S,H*S),(cx-16*S,hy+hh/2-4*S),(cx+16*S,hy+hh/2-4*S),(cx+46*S,H*S)],fill=SHIRT)
    g.polygon([(cx-96*S,H*S),(cx-30*S,hy+hh/2-2*S),(cx-44*S,H*S)],fill=SUIT)
    g.polygon([(cx+96*S,H*S),(cx+30*S,hy+hh/2-2*S),(cx+44*S,H*S)],fill=SUIT)
    g.polygon([(cx,hy+hh/2+2*S),(cx-9*S,hy+hh/2+13*S),(cx,hy+hh/2+24*S),(cx+9*S,hy+hh/2+13*S)],fill=TIE)
    g.polygon([(cx-8*S,hy+hh/2+16*S),(cx+8*S,hy+hh/2+16*S),(cx+12*S,H*S),(cx-12*S,H*S)],fill=TIE)

    base.alpha_composite(fg)
    img=base.convert("RGB")

    # ---- glitch: horizontal band displacement + chroma split
    if glitch>0:
        px=img.load(); out=img.copy()
        rnd=random.Random(int(phase*1000))
        for _ in range(int(6*glitch)):
            y0=rnd.randrange(0,H*S-8); hgt=rnd.randrange(4,26)
            dx=rnd.randrange(-int(14*S*glitch),int(14*S*glitch)+1)
            band=img.crop((0,y0,W*S,min(H*S,y0+hgt)))
            out.paste(band,(dx,y0))
        r,gg,b=out.split()
        off=int(2*S*glitch)
        r=r.transform(r.size,Image.AFFINE,(1,0,-off,0,1,0))
        b=b.transform(b.size,Image.AFFINE,(1,0, off,0,1,0))
        img=Image.merge("RGB",(r,gg,b))
    return img.resize((W,H),Image.LANCZOS)

EMO={"Neutral":(100,0,14),"Happy":(78,-170,86),"Angry":(58,280,26),
     "Sad":(72,-260,8),"Doubt":(88,-330,34),"Sleepy":(24,-90,6)}
def frame(e):
    wt,rot,mw=EMO[e]
    return {"leftEye":{"x":0,"y":0,"rotation":rot,"weight":wt,"size":0},
            "rightEye":{"x":0,"y":0,"rotation":-rot,"weight":wt,"size":0},
            "mouth":{"x":0,"y":0,"rotation":0,"weight":mw,"size":0}}

if __name__=="__main__":
    sheet=Image.new("RGB",(W*3,H*2),(14,16,22))
    for i,e in enumerate(EMO):
        im=render(frame(e), phase=i*0.7, glitch=0.0)
        ImageDraw.Draw(im).text((5,4),e,fill=(210,230,255))
        sheet.paste(im,((i%3)*W,(i//3)*H))
    sheet.save(os.path.join(HERE,"char_max.png"))
    hero=Image.new("RGB",(W*3,H),(14,16,22))
    hero.paste(render(frame("Happy"),phase=0.2),(0,0))
    hero.paste(render(frame("Neutral"),phase=1.4,glitch=0.8),(W,0))
    hero.paste(render(frame("Doubt"),phase=2.9),(W*2,0))
    ImageDraw.Draw(hero).text((W+5,4),"glitch",fill=(255,120,180))
    hero.save(os.path.join(HERE,"char_max_hero.png"))
    print("wrote max sheets")
