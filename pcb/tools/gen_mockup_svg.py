#!/usr/bin/env python3
"""gen_mockup_svg.py -- 1:1 OUTSIDE drilling template for the Tone Trixter in a Hammond 1590XX.

Positions come from enclosure.py (SHARED with gen_place.py), so the template and the board cannot
drift. TAPE TO THE OUTSIDE of the box, ink up, drill from the outside (clean entry on the show face).

LAYOUT = a box NET. The TOP FACE is drawn player-natural (FRONT/footswitches at the bottom, encoder
on your right). The REAR WALL is a FOLD-UP FLAP hinged at the back-rear edge, drawn ABOVE the top
view: fold it down over the back of the box and the jack columns line up straight down into the top
view. Because the flap folds (print ends up facing outward), X is NOT mirrored -- it reads the same
left/right as the top view, so the jacks project vertically across both.

REGISTER BY THE CENTRE: mark the box's two diagonals -- where they cross is the centre -- and line it
up with the template's centre crosshair (one continuous blue line runs through the flap and the top
view). That is immune to the wall-thickness / 145-vs-138 guesswork.

Convention (locked 2026-10-12): INPUT on the RIGHT, OUTPUT on the LEFT; encoder right of the screen.

Print at 100% / "actual size", confirm the 100 mm ruler reads 100.0, cut out, tape, mark, drill.
  python3 tools/gen_mockup_svg.py              # -> mockup/mockup_1590xx.svg
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import enclosure as e

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockup", "mockup_1590xx.svg")

BOX_W, BOX_D = e.BOX_W, e.BOX_D
EXT_W, EXT_D = e.EXT_W, e.EXT_D
CAVITY_H     = e.CAVITY_H
NOTCH        = e.NOTCH
BRD_X0, BRD_X1 = e.BRD_INSET_X, BOX_W - e.BRD_INSET_X
BRD_Y0, BRD_Y1 = e.BRD_Y0, e.BRD_Y1

# ---- SVG helpers -----------------------------------------------------------------------------
def _hdr(w, h):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" '
            f'viewBox="0 0 {w} {h}">\n'
            '<style>text{font-family:sans-serif;fill:#111}'
            '.lbl{font-size:3.2px}.dim{font-size:2.6px;fill:#555}.ttl{font-size:4px;font-weight:bold}'
            '.note{font-size:2.6px;fill:#a00}.ctr{font-size:2.6px;fill:#09c}.fold{font-size:2.8px;fill:#06c;font-weight:bold}</style>\n'
            f'<rect x="0" y="0" width="{w}" height="{h}" fill="#fff"/>\n')
def _dash(d): return f' stroke-dasharray="{d}"' if d else ''
def _circ(cx, cy, d, stroke='#111', dash=''):
    return (f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{d/2:.2f}" fill="none" '
            f'stroke="{stroke}" stroke-width="0.3"{_dash(dash)}/>\n')
def _cross(cx, cy, s=2.0, stroke='#111'):
    return (f'<line x1="{cx-s:.2f}" y1="{cy:.2f}" x2="{cx+s:.2f}" y2="{cy:.2f}" stroke="{stroke}" stroke-width="0.2"/>\n'
            f'<line x1="{cx:.2f}" y1="{cy-s:.2f}" x2="{cx:.2f}" y2="{cy+s:.2f}" stroke="{stroke}" stroke-width="0.2"/>\n')
def _rect(x, y, w, h, stroke='#111', dash='', sw=0.4):
    return (f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="none" '
            f'stroke="{stroke}" stroke-width="{sw}"{_dash(dash)}/>\n')
def _line(x1, y1, x2, y2, stroke='#111', dash='', sw=0.3):
    return f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{stroke}" stroke-width="{sw}"{_dash(dash)}/>\n'
def _poly(pts, stroke='#06c', dash='3 2', sw=0.4):
    p = ' '.join(f'{x:.2f},{y:.2f}' for x, y in pts)
    return f'<polygon points="{p}" fill="none" stroke="{stroke}" stroke-width="{sw}"{_dash(dash)}/>\n'
def _txt(x, y, s, cls='lbl', anchor='middle'):
    return f'<text x="{x:.2f}" y="{y:.2f}" class="{cls}" text-anchor="{anchor}">{s}</text>\n'

# ---- vertical layout (mm, top of page downward) ----------------------------------------------
MARGIN   = 12.0
EXT_OVER = (EXT_D - BOX_D)              # external outline overhang above the inner plane
# REAR WALL fold-up flap sits ABOVE the top view; its base (z=0, pedal bottom) is the top edge,
# its hinge (z=CAVITY_H, top-rear fold edge) is the bottom edge -- i.e. the strip is flipped so the
# "top edge is the bottom", ready to flop down over the back.
FY_BASE  = 34.0                        # flap TOP edge  = pedal base (z=0)
FY_HINGE = FY_BASE + CAVITY_H          # flap BOTTOM edge = top-rear fold edge (z=CAVITY_H)
ox       = MARGIN
oy       = FY_HINGE + EXT_OVER         # top-face inner-plane top; external outline top == the fold
def TX(x): return ox + x                                 # SAME X as the flap -> columns align
def TY(y): return oy + (BOX_D - y)                       # flip so FRONT (Y=0) is at the bottom

PAGE_W = MARGIN*2 + max(EXT_W, BOX_W) + 6
PAGE_H = TY(0) + 24

s = _hdr(PAGE_W, PAGE_H)
s += _txt(MARGIN, 7,  "1590XX template -- OUTSIDE, ink up, drill from outside -- PRINT AT 100%", 'ttl', 'start')
s += _txt(MARGIN, 11, "Box NET: TOP FACE below (FRONT toward player); REAR WALL = fold-up flap above, hinged at the back edge.", 'note', 'start')
s += _txt(MARGIN, 14.5,"Fold the flap down over the back; jack columns project straight down. IN on the RIGHT, OUT on the LEFT; encoder right of screen.", 'note', 'start')

# ============================================================ TOP FACE (player-natural) =========
ext_off = (EXT_W - BOX_W) / 2.0
s += _rect(ox - ext_off, oy - EXT_OVER, EXT_W, EXT_D, stroke='#999', dash='2 2', sw=0.3)
s += _rect(ox, oy, BOX_W, BOX_D, stroke='#bbb', sw=0.3)
s += _txt(TX(BOX_W/2), TY(0)+5, "FRONT (toward player)", 'dim')

# ONE continuous centre line: through the flap AND the top view (registration datum)
cx, cy = BOX_W/2, BOX_D/2
s += _line(TX(cx), FY_BASE, TX(cx), TY(0)+3, stroke='#09c', dash='5 2', sw=0.35)
s += _line(ox - ext_off - 3, TY(cy), ox + BOX_W + ext_off + 3, TY(cy), stroke='#09c', dash='5 2', sw=0.35)
s += _txt(ox + BOX_W + ext_off + 2, TY(cy)-1, "BOX CENTRE (align to the diagonals)", 'ctr', 'end')

# corner bosses (internal -- shown so you keep the board notches clear)
for bx in (e.BOSS_IN, BOX_W - e.BOSS_IN):
    for by in (e.BOSS_IN, BOX_D - e.BOSS_IN):
        s += _circ(TX(bx), TY(by), e.BOSS_D, stroke='#999')

# full-width board, rear corners SQUARE-notched (right-angle, matches gen_board.py)
brd = [
    (BRD_X0, BRD_Y0), (BRD_X1, BRD_Y0),
    (BRD_X1, BRD_Y1 - NOTCH), (BRD_X1 - NOTCH, BRD_Y1 - NOTCH), (BRD_X1 - NOTCH, BRD_Y1),
    (BRD_X0 + NOTCH, BRD_Y1), (BRD_X0 + NOTCH, BRD_Y1 - NOTCH), (BRD_X0, BRD_Y1 - NOTCH),
]
s += _poly([(TX(x), TY(y)) for x, y in brd])
s += _txt(TX(BOX_W/2), TY(BRD_Y0 + 5), "full-width board (dashed) -- rear corners square-notched for the bosses", 'dim')

# footswitches (panel-mounted, up front)
for fx, fy in e.FOOTSW:
    s += _circ(TX(fx), TY(fy), e.FS_DIA) + _cross(TX(fx), TY(fy))
s += _txt(TX(BOX_W/2), TY(e.FOOTSW[0][1])+7, f"footswitches O{e.FS_DIA:g} @ {e.FS_CTR:g} ctrs", 'dim')

# LEDs
for lx, ly in e.LEDS:
    s += _circ(TX(lx), TY(ly), e.LED_DIA) + _cross(TX(lx), TY(ly))
s += _txt(TX(BOX_W/2), TY(e.LEDS[0][1])-4, "LED O3 x2 (shape-distinct)", 'dim')

# OLED window (centred)
oc_x, oc_y = e.OLED_CTR
s += _rect(TX(oc_x - e.OLED_W/2), TY(oc_y + e.OLED_H/2), e.OLED_W, e.OLED_H, sw=0.4)
s += _txt(TX(oc_x), TY(oc_y + e.OLED_H/2 + 3), f"OLED {e.OLED_W:g}x{e.OLED_H:g}", 'dim')
# encoder (RIGHT of the OLED)
s += _circ(TX(e.ENCODER[0]), TY(e.ENCODER[1]), e.ENC_DIA) + _cross(TX(e.ENCODER[0]), TY(e.ENCODER[1]))
s += _txt(TX(e.ENCODER[0]), TY(e.ENCODER[1])-5, f"enc O{e.ENC_DIA:g}", 'dim')

# rear jacks: ticks where they exit the BACK WALL edge (top of the top view) -- align with the flap
for ref, label, xc, size, kind, z in e.REAR:
    s += _line(TX(xc), oy - 1.5, TX(xc), oy + 1.5, stroke='#111', sw=0.4)

# ============================================================ REAR WALL flap (fold-up, above) ===
# Hinge (bottom edge, z=CAVITY_H) sits on the fold; base (top edge, z=0) is the pedal bottom.
def RY(z): return FY_BASE + z          # z measured up from base, base at the TOP of the flap
s += _rect(ox, FY_BASE, BOX_W, CAVITY_H, sw=0.6)
# fold emphasis on the bottom edge
s += _txt(TX(BOX_W/2), FY_HINGE + 3, "FOLD DOWN HERE -- top-rear edge (z=%g) -- flap flops over the back wall" % CAVITY_H, 'fold')
# datum + end-wall labels (above the flap, UN-mirrored: player's left on the left, right on the right)
s += _txt(ox + 1, FY_BASE - 7, "← box LEFT wall (player's L)", 'dim', 'start')
s += _txt(ox + BOX_W - 1, FY_BASE - 7, "box RIGHT wall (player's R) →", 'dim', 'end')
s += _txt(ox + BOX_W/2, FY_BASE - 7, "jack axes: TRS %g mm / USB %g mm up from base" % (e.JACK_Z, e.USB_Z), 'dim')
s += _txt(ox + 1, FY_BASE - 2.5, "base z=0 = pedal bottom (this top edge)", 'dim', 'start')
# holes at TRUE X (TX, same as top view) and true height z; projection line DOWN into the top view
for ref, label, xc, size, kind, z in e.REAR:
    hy = RY(z)
    s += _line(TX(xc), hy, TX(xc), oy, stroke='#bbb', dash='2 2', sw=0.2)   # vertical projection
    if kind == "usb":
        w, h = size
        s += _rect(TX(xc) - w/2, hy - h/2, w, h, sw=0.4)
        s += _txt(TX(xc), hy - h/2 - 1.5, f"{label} {w:g}x{h:g}", 'dim')
    else:
        s += _circ(TX(xc), hy, size) + _cross(TX(xc), hy)
        s += _txt(TX(xc), hy - size/2 - 1.5, f"{label} O{size:g}", 'dim')

# ---- 100 mm scale ruler (print check) ----
ruy = TY(0) + 13
s += _line(ox, ruy, ox+100, ruy, sw=0.4)
for i in range(0, 101, 10):
    s += _line(ox+i, ruy-2, ox+i, ruy+2, sw=0.3)
s += _txt(ox+50, ruy+6, "this bar MUST measure 100.0 mm -- if not, fix the print scale", 'note')

s += '</svg>\n'
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w') as f:
    f.write(s)
print(f"wrote {os.path.relpath(OUT)}  ({PAGE_W:.0f}x{PAGE_H:.0f} mm page)")
