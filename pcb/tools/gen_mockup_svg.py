#!/usr/bin/env python3
"""gen_mockup_svg.py -- 1:1 OUTSIDE drilling template for the Tone Trixter in a Hammond 1590XX.

Positions come from enclosure.py (SHARED with gen_place.py), so the template and the board cannot
drift. TAPE TO THE OUTSIDE of the box, ink up, drill from the outside (clean entry on the show face).

REGISTER BY THE CENTRE: mark the box's two diagonals -- where they cross is the centre -- and line it
up with the template's centre crosshair. That is immune to the wall-thickness / 145-vs-138 guesswork,
and no mirroring is needed because ink-up + drill-from-outside shows the layout exactly as it ends up.

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
            '.note{font-size:2.6px;fill:#a00}.ctr{font-size:2.6px;fill:#09c}</style>\n'
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

# ---- page ------------------------------------------------------------------------------------
MARGIN = 12.0
PAGE_W = MARGIN*2 + max(EXT_W, BOX_W) + 6
PAGE_H = MARGIN*3 + EXT_D + CAVITY_H + 26

s = _hdr(PAGE_W, PAGE_H)
s += _txt(MARGIN, 7,  "1590XX template -- OUTSIDE, ink up, drill from outside -- PRINT AT 100%", 'ttl', 'start')
s += _txt(MARGIN, 11, "Register by CENTRE: mark the box's diagonals, cross = centre, align the blue crosshair. Box ext ~145x121x39.", 'note', 'start')
s += _txt(MARGIN, 14.5,"IN on the RIGHT, OUT on the LEFT (convention); encoder right of the centred screen. Positions shared with the PCB.", 'note', 'start')

ox, oy = MARGIN, MARGIN + 9
def TX(x): return ox + x
def TY(y): return oy + (BOX_D - y)        # flip so FRONT (Y=0) is at the bottom

# external outline (dashed) + internal top-face plane (thin) -- reference only; centre is the datum
ext_off = (EXT_W - BOX_W) / 2.0
s += _rect(ox - ext_off, oy - (EXT_D - BOX_D), EXT_W, EXT_D, stroke='#999', dash='2 2', sw=0.3)
s += _rect(ox, oy, BOX_W, BOX_D, stroke='#bbb', sw=0.3)
s += _txt(TX(BOX_W/2), oy - 2, "TOP FACE  (BACK WALL this edge)  -- ext dashed, inner plane thin", 'dim')
s += _txt(TX(BOX_W/2), TY(0)+5, "FRONT (toward player)", 'dim')

# CENTRE crosshair -- the primary registration datum
cx, cy = BOX_W/2, BOX_D/2
s += _line(TX(cx), oy - (EXT_D - BOX_D) - 3, TX(cx), TY(0)+3, stroke='#09c', dash='5 2', sw=0.35)
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

# rear jacks: small ticks at the rear edge to show X alignment -- full detail is on the strip below
for ref, label, xc, size, kind, z in e.REAR:
    s += _line(TX(xc), TY(BRD_Y1)-1.5, TX(xc), TY(BRD_Y1)+1.5, stroke='#111', sw=0.4)
s += _txt(TX(BOX_W/2), TY(BRD_Y1)-2.5, "rear jacks exit the BACK WALL here (X ticks) -- drill them from the REAR WALL strip below", 'dim')

# ---- REAR WALL strip -- OUTSIDE view (you standing BEHIND the pedal), so X is MIRRORED ----
ry = oy + EXT_D + 16
def SX(x): return ox + (BOX_W - x)        # mirror: box LEFT wall -> your RIGHT when behind
s += _txt(ox, ry - 4, "REAR WALL -- tape OUTSIDE, you are BEHIND the pedal (so this is MIRRORED from the top view).", 'note', 'start')
s += _txt(ox, ry - 1.5, "Centre-register (mark the back-wall centre + base). Best accuracy for board jacks: drill through the finished board.", 'dim', 'start')
s += _rect(ox, ry, BOX_W, CAVITY_H, sw=0.6)
# datums
s += _txt(ox+2, ry+CAVITY_H-1.5, "base (z=0)", 'dim', 'start')
s += _txt(ox+2, ry+3, f"top inner (z={CAVITY_H:g})", 'dim', 'start')
s += _line(ox + BOX_W/2, ry, ox + BOX_W/2, ry+CAVITY_H, stroke='#09c', dash='5 2', sw=0.35)
s += _txt(ox + BOX_W/2, ry-4, "centre", 'ctr')
# end labels (which physical wall is which, since mirrored)
s += _txt(ox+1, ry+CAVITY_H+10.5, "← box RIGHT wall (player's R)", 'dim', 'start')
s += _txt(ox+BOX_W-1, ry+CAVITY_H+10.5, "box LEFT wall (player's L) →", 'dim', 'end')
# holes at mirrored X, true height z
for ref, label, xc, size, kind, z in e.REAR:
    hy = ry + CAVITY_H - z
    if kind == "usb":
        w, h = size
        s += _rect(SX(xc) - w/2, hy - h/2, w, h, sw=0.4)
        s += _txt(SX(xc), ry+CAVITY_H+4, f"{label} {w:g}x{h:g}", 'dim')
    else:
        s += _circ(SX(xc), hy, size) + _cross(SX(xc), hy)
        s += _txt(SX(xc), ry+CAVITY_H+4, f"{label} O{size:g}", 'dim')
    s += _txt(SX(xc), ry+CAVITY_H+6.5, f"axis {z:g} up", 'dim')

# ---- 100 mm scale ruler (print check) ----
ruy = ry + CAVITY_H + 16
s += _line(ox, ruy, ox+100, ruy, sw=0.4)
for i in range(0, 101, 10):
    s += _line(ox+i, ruy-2, ox+i, ruy+2, sw=0.3)
s += _txt(ox+50, ruy+6, "this bar MUST measure 100.0 mm -- if not, fix the print scale", 'note')

s += '</svg>\n'
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w') as f:
    f.write(s)
print(f"wrote {os.path.relpath(OUT)}  ({PAGE_W:.0f}x{PAGE_H:.0f} mm page)")
