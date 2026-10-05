"""Candidate character renderers. Mirrors what the C++ skin will do so previews are honest.
All obey the firmware Feature contract: position -100..100, weight 0..100, size -100..100,
rotation in tenths of a degree."""
from PIL import Image, ImageDraw, ImageFilter
import math, os

HERE=os.path.dirname(os.path.abspath(__file__))

W,H=320,240
S=3  # supersample

def clamp(v,lo,hi): return lo if v<lo else (hi if v>hi else v)
def mr(v,a,b,c,d): return c if b==a else c+(v-a)*(d-c)/(b-a)

def glow(layer, radius, strength=1.0):
    g = layer.filter(ImageFilter.GaussianBlur(radius))
    if strength != 1.0:
        a = g.split()[3].point(lambda p: int(p*strength))
        g.putalpha(a)
    return g

def new_layer(): return Image.new("RGBA",(W*S,H*S),(0,0,0,0))

# ---------------------------------------------------------------- VOLT ------
def volt(f, bg=(6,8,14)):
    """Neon HUD sprite: rounded-rect visor eyes, angular brows, waveform mouth."""
    CORE=(190,255,255,255); NEON=(0,225,255,255); BROW=(255,60,170,255); MOUTH=(0,255,190,255)
    img=Image.new("RGBA",(W*S,H*S),bg+(255,))
    gl=new_layer(); dg=ImageDraw.Draw(gl)
    sh=new_layer(); ds=ImageDraw.Draw(sh)

    for is_left in (True,False):
        e=f["leftEye"] if is_left else f["rightEye"]
        bx = -66 if is_left else 66
        cx=(W/2+bx+mr(clamp(e["x"],-100,100),-100,100,-16,16))*S
        cy=(H/2-18+mr(clamp(e["y"],-100,100),-100,100,-16,16))*S
        base=mr(clamp(e["size"],-100,100),-100,100,26,52)*S
        ew=base; eh=base*mr(clamp(e["weight"],0,100),0,100,0.10,1.0)
        r=min(ew,eh)*0.42
        box=(cx-ew/2,cy-eh/2,cx+ew/2,cy+eh/2)
        dg.rounded_rectangle(box,radius=r,fill=NEON)
        inset=ew*0.20
        if eh>inset*2.2:
            dg.rounded_rectangle((box[0]+inset,box[1]+inset*0.75,box[2]-inset,box[3]-inset*0.75),
                                 radius=r*0.5,fill=CORE)
        # brow: angular slash, angle from rotation
        rot=e["rotation"]/10.0
        bw=base*1.15; bh=base*0.17
        byy=cy-eh/2-base*0.42
        br=Image.new("RGBA",(int(bw)+4,int(bh)+4),(0,0,0,0))
        ImageDraw.Draw(br).rounded_rectangle((2,2,bw,bh),radius=bh/2,fill=BROW)
        br=br.rotate(-rot,expand=True,resample=Image.BICUBIC)  # LVGL CW -> PIL CCW
        sh.alpha_composite(br,(int(cx-br.width/2),int(byy-br.height/2)))

    m=f["mouth"]
    mx=(W/2+mr(clamp(m["x"],-100,100),-100,100,-16,16))*S
    my=(H/2+40+mr(clamp(m["y"],-100,100),-100,100,-16,16))*S
    wt=clamp(m["weight"],0,100)
    mw=mr(wt,0,100,86,62)*S; mh=mr(wt,0,100,7,44)*S; rr=mr(wt,0,100,3,18)*S
    dg.rounded_rectangle((mx-mw/2,my-mh/2,mx+mw/2,my+mh/2),radius=rr,fill=MOUTH)

    img.alpha_composite(glow(gl,10*S,0.85)); img.alpha_composite(glow(sh,7*S,0.85))
    img.alpha_composite(gl); img.alpha_composite(sh)
    return img.resize((W,H),Image.LANCZOS).convert("RGB")

# ----------------------------------------------------------------- PIP ------
def pip(f, bg=(10,12,20)):
    """Big round anime eyes with highlight, soft brows, cat mouth."""
    SCLERA=(255,255,255,255); IRIS=(90,180,255,255); PUP=(12,14,24,255)
    BROW=(230,235,245,255); MOUTH=(255,255,255,255)
    img=Image.new("RGBA",(W*S,H*S),bg+(255,))
    l=new_layer(); d=ImageDraw.Draw(l)
    for is_left in (True,False):
        e=f["leftEye"] if is_left else f["rightEye"]
        bx=-64 if is_left else 64
        cx=(W/2+bx+mr(clamp(e["x"],-100,100),-100,100,-14,14))*S
        cy=(H/2-16+mr(clamp(e["y"],-100,100),-100,100,-14,14))*S
        dia=mr(clamp(e["size"],-100,100),-100,100,34,60)*S
        op=mr(clamp(e["weight"],0,100),0,100,0.08,1.0)
        eh=dia*op
        d.ellipse((cx-dia/2,cy-eh/2,cx+dia/2,cy+eh/2),fill=SCLERA)
        if op>0.35:
            ir=dia*0.40
            d.ellipse((cx-ir/2,cy-ir/2*op,cx+ir/2,cy+ir/2*op),fill=IRIS)
            pr=ir*0.52
            d.ellipse((cx-pr/2,cy-pr/2*op,cx+pr/2,cy+pr/2*op),fill=PUP)
            hr=dia*0.15
            d.ellipse((cx-ir*0.30-hr/2,cy-ir*0.34*op-hr/2,cx-ir*0.30+hr/2,cy-ir*0.34*op+hr/2),
                      fill=(255,255,255,235))
        rot=e["rotation"]/10.0
        bw=dia*0.92; bh=dia*0.13
        br=Image.new("RGBA",(int(bw)+4,int(bh)+4),(0,0,0,0))
        ImageDraw.Draw(br).rounded_rectangle((2,2,bw,bh),radius=bh/2,fill=BROW)
        br=br.rotate(-rot,expand=True,resample=Image.BICUBIC)  # LVGL CW -> PIL CCW
        l.alpha_composite(br,(int(cx-br.width/2),int(cy-dia*0.72-br.height/2)))
    m=f["mouth"]; wt=clamp(m["weight"],0,100)
    mx=(W/2+mr(clamp(m["x"],-100,100),-100,100,-14,14))*S
    my=(H/2+42+mr(clamp(m["y"],-100,100),-100,100,-14,14))*S
    mw=mr(wt,0,100,44,54)*S; mh=mr(wt,0,100,6,40)*S
    d.rounded_rectangle((mx-mw/2,my-mh/2,mx+mw/2,my+mh/2),radius=mh/2,fill=MOUTH)
    img.alpha_composite(l)
    return img.resize((W,H),Image.LANCZOS).convert("RGB")

# --------------------------------------------------------------- GLYPH ------
def glyph(f, bg=(4,6,10)):
    """Minimal geometric: hexagon eyes, single-line brows, segmented mouth."""
    NEON=(255,190,40,255); DIM=(120,80,10,255); MOUTH=(255,190,40,255)
    img=Image.new("RGBA",(W*S,H*S),bg+(255,))
    l=new_layer(); d=ImageDraw.Draw(l)
    def hexpts(cx,cy,rw,rh):
        return [(cx-rw,cy),(cx-rw*0.5,cy-rh),(cx+rw*0.5,cy-rh),
                (cx+rw,cy),(cx+rw*0.5,cy+rh),(cx-rw*0.5,cy+rh)]
    for is_left in (True,False):
        e=f["leftEye"] if is_left else f["rightEye"]
        bx=-68 if is_left else 68
        cx=(W/2+bx+mr(clamp(e["x"],-100,100),-100,100,-16,16))*S
        cy=(H/2-16+mr(clamp(e["y"],-100,100),-100,100,-16,16))*S
        rw=mr(clamp(e["size"],-100,100),-100,100,16,30)*S
        rh=rw*mr(clamp(e["weight"],0,100),0,100,0.10,0.92)
        d.polygon(hexpts(cx,cy,rw,rh),fill=NEON)
        if rh>rw*0.3:
            d.polygon(hexpts(cx,cy,rw*0.55,rh*0.55),fill=bg+(255,))
        rot=e["rotation"]/10.0
        bw=rw*2.1; bh=rw*0.20
        br=Image.new("RGBA",(int(bw)+4,int(bh)+4),(0,0,0,0))
        ImageDraw.Draw(br).rectangle((2,2,bw,bh),fill=DIM)
        br=br.rotate(-rot,expand=True,resample=Image.BICUBIC)  # LVGL CW -> PIL CCW
        l.alpha_composite(br,(int(cx-br.width/2),int(cy-rw*1.5-br.height/2)))
    m=f["mouth"]; wt=clamp(m["weight"],0,100)
    mx=(W/2)*S; my=(H/2+40)*S
    segs=7; total=mr(wt,0,100,74,92)*S; gap=total/segs*0.30; sw=(total-gap*(segs-1))/segs
    hgt=mr(wt,0,100,5,34)*S
    for i in range(segs):
        x0=mx-total/2+i*(sw+gap)
        k=1.0-abs(i-(segs-1)/2)/((segs-1)/2)*0.55
        d.rectangle((x0,my-hgt*k/2,x0+sw,my+hgt*k/2),fill=MOUTH)
    img.alpha_composite(l)
    return img.resize((W,H),Image.LANCZOS).convert("RGB")

EMO={"Neutral":(100,0),"Happy":(72,1550),"Angry":(70,450),
     "Sad":(70,-400),"Doubt":(75,0),"Sleepy":(35,-50)}
MOUTH_W={"Neutral":12,"Happy":62,"Angry":22,"Sad":8,"Doubt":30,"Sleepy":5}

def frame(emo):
    wt,rot=EMO[emo]
    return {"leftEye":{"x":0,"y":0,"rotation":rot,"weight":wt,"size":0},
            "rightEye":{"x":0,"y":0,"rotation":-rot,"weight":wt,"size":0},
            "mouth":{"x":0,"y":0,"rotation":0,"weight":MOUTH_W[emo],"size":0}}

if __name__=="__main__":
    import sys
    styles={"volt":volt,"pip":pip,"glyph":glyph}
    for nm,fn in styles.items():
        sheet=Image.new("RGB",(W*3,H*2),(18,20,26))
        for i,emo in enumerate(EMO):
            im=fn(frame(emo))
            ImageDraw.Draw(im).text((6,4),emo,fill=(150,160,180))
            sheet.paste(im,((i%3)*W,(i//3)*H))
        sheet.save(os.path.join(HERE,f"char_{nm}.png"))
        print("wrote", nm)
    # side-by-side neutral+happy comparison
    cmp=Image.new("RGB",(W*3,H*2),(18,20,26))
    for i,(nm,fn) in enumerate(styles.items()):
        cmp.paste(fn(frame("Neutral")),(i*W,0))
        cmp.paste(fn(frame("Happy")),(i*W,H))
        ImageDraw.Draw(cmp).text((i*W+6,4),nm.upper(),fill=(150,160,180))
    cmp.save(os.path.join(HERE,"char_compare.png"))
    print("wrote compare")
