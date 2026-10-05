"""Bake MAX's static art (background grid, hair, face, collar, tie) to a 320x240 plate.
Eyes and mouth are LIVE LVGL objects drawn on top, so they stay animatable."""
import os, sys
HERE=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,HERE)
from PIL import Image, ImageDraw
import render_max as R

W,H,S = R.W, R.H, R.S

def backplate():
    base=Image.new("RGBA",(W*S,H*S),(0,0,0,255))
    d=ImageDraw.Draw(base)
    R.bg(d, 0.0)
    fg=Image.new("RGBA",(W*S,H*S),(0,0,0,0))
    g=ImageDraw.Draw(fg)
    cx, cy = W*S/2, H*S/2

    # head with a tapered jaw (trapezoid) rather than a plain block
    hw, hh = 96*S, 104*S
    hx, hy = cx, cy+6*S
    jaw = hw*0.82
    g.polygon([(hx-hw/2, hy-hh/2+14*S), (hx-hw/2+10*S, hy-hh/2),
               (hx+hw/2-10*S, hy-hh/2), (hx+hw/2, hy-hh/2+14*S),
               (hx+jaw/2, hy+hh/2-14*S), (hx+jaw/2-12*S, hy+hh/2),
               (hx-jaw/2+12*S, hy+hh/2), (hx-jaw/2, hy+hh/2-14*S)], fill=R.SKIN)
    # cheek/jaw shading
    g.polygon([(hx-jaw/2, hy+hh*0.16), (hx+jaw/2, hy+hh*0.16),
               (hx+jaw/2-12*S, hy+hh/2), (hx-jaw/2+12*S, hy+hh/2)], fill=R.SKIN_SH)
    # brow ridge highlight
    g.polygon([(hx-hw/2+6*S, hy-hh/2+16*S),(hx+hw/2-6*S, hy-hh/2+16*S),
               (hx+hw/2-10*S, hy-hh/2+30*S),(hx-hw/2+10*S, hy-hh/2+30*S)],
              fill=(238,196,146))

    # slicked-back angular hair wedge
    top=hy-hh/2
    g.polygon([(hx-hw/2-4*S, top+20*S),(hx-hw/2+4*S, top-20*S),
               (hx+hw/2-4*S, top-20*S),(hx+hw/2+4*S, top+20*S),
               (hx+hw/2-8*S, top+10*S),(hx-hw/2+8*S, top+10*S)], fill=R.HAIR)
    for i in range(6):
        x0=hx-hw/2+5*S+i*(hw-10*S)/6
        R.rot_rect(fg, x0+(hw-10*S)/12, top-6*S, (hw-10*S)/6*0.66, 26*S,
                   -16+i*6.5, R.HAIR_HI if i%2 else R.HAIR, radius=3*S)

    # glasses bridge (static; lenses are live)
    g.rectangle((hx-9*S, hy-23*S, hx+9*S, hy-18*S), fill=R.LENS)

    # collar, jacket, tie
    g.polygon([(cx-46*S,H*S),(cx-16*S,hy+hh/2-4*S),(cx+16*S,hy+hh/2-4*S),(cx+46*S,H*S)],fill=R.SHIRT)
    g.polygon([(cx-100*S,H*S),(cx-30*S,hy+hh/2-2*S),(cx-44*S,H*S)],fill=R.SUIT)
    g.polygon([(cx+100*S,H*S),(cx+30*S,hy+hh/2-2*S),(cx+44*S,H*S)],fill=R.SUIT)
    g.polygon([(cx,hy+hh/2+2*S),(cx-9*S,hy+hh/2+13*S),(cx,hy+hh/2+24*S),(cx+9*S,hy+hh/2+13*S)],fill=R.TIE)
    g.polygon([(cx-8*S,hy+hh/2+16*S),(cx+8*S,hy+hh/2+16*S),(cx+12*S,H*S),(cx-12*S,H*S)],fill=R.TIE)

    base.alpha_composite(fg)
    return base.convert("RGB").resize((W,H),Image.LANCZOS)

if __name__=="__main__":
    im=backplate()
    im.save(os.path.join(HERE,"max_backplate.png"))
    print("wrote max_backplate.png", im.size)
