#!/usr/bin/env python3
"""gen_mockup_svg.py -- 1:1 paper mock-up template for the Tone Trixter in a Hammond 1590XX.

Brief §1/§7: "mock the face up 1:1 on paper, then draw the board." This writes an SVG at TRUE
1:1 (mm units) of the TOP FACE (footswitches, OLED window, encoder, LEDs, candidate board outline)
and the REAR WALL (IN/DC/USB/OUT + a reserved 5th). Print at 100% / "actual size", confirm the
100 mm ruler measures 100.0 mm, cut it out, tape it to the casting, and mark any corrections.

Measured box (2026-10-09, box in hand):
  external 145 x 121 x 39 mm, wall 2 mm, internal TOP-FACE PLANE 138 (deep) x 114 (wide),
  four Ø7 corner lid bosses.

⚠ Every PART HOLE SIZE below is a DEFAULT to verify against the real part -- the whole point of a
paper mock-up. Edit the dicts and re-run. Stdlib only.

  python3 tools/gen_mockup_svg.py              # -> mockup/mockup_1590xx.svg
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockup", "mockup_1590xx.svg")

# ---- measured box (top-face plane; front = toward the player) -------------------------------
BOX_W   = 114.0   # internal width  (left-right), from the 121 external
BOX_D   = 138.0   # internal depth  (front-back), from the 145 external
EXT_W   = 121.0
EXT_D   = 145.0
BOSS_D  = 7.0     # corner lid boss diameter
BOSS_IN = 6.0     # boss centre inset from each wall (approx -- verify)

# ---- top-face holes (X = width 0..114 from left wall, Y = depth 0..138 from FRONT wall) ------
# ⚠ DEFAULT diameters -- verify against the real parts.
FS_DIA   = 12.0   # footswitch, SPST momentary
FS_Y     = 22.0   # from the front wall
FS_CTR   = 76.0   # centre-to-centre
ENC_DIA  = 7.0    # PEC11R bushing  (⚠ nylon shoulder washer: bushing is a GPIO pull-up)
OLED_W, OLED_H = 35.0, 32.0   # SH1106 1.3" viewable window
LED_DIA  = 3.0

LED_Y    = 50.0   # just behind the footswitches, in view above the foot
LED_X    = (40.0, 74.0)
OLED_CY  = 82.0   # OLED window centre, depth (on the board, behind the LEDs)
ENC_CY   = 82.0
ENC_X    = 90.0   # encoder to the right of the OLED

# candidate board outline (dashed) -- front edge BEHIND the footswitches, rear edge clear of the
# rear-wall jack bodies. The real outline is locked in KiCad from the placed parts.
BOARD_W, BOARD_D = 75.0, 82.0
BOARD_Y0         = 33.0       # front edge, just behind the footswitch nuts
BOARD_INSET_M3   = 3.0        # M3 corner holes, 3 mm inset
M3_DIA           = 3.2

# ---- rear wall (X = width 0..114, holes on a strip ~33 mm tall internal) ---------------------
WALL_H   = 33.0
REAR = [  # (label, shape, size, x_centre)   x at ~22 mm centres, 5 slots
    ("IN",  "circ", 10.0, 13.0),
    ("DC",  "circ", 12.0, 35.0),
    ("USB", "rect", (13.0, 8.0), 57.0),
    ("OUT", "circ", 10.0, 79.0),
    ("5th", "circ", 10.0, 101.0),   # reserved -- marked, not drilled
]

# ---- SVG helpers -----------------------------------------------------------------------------
def _hdr(w, h):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" '
            f'viewBox="0 0 {w} {h}">\n'
            '<style>text{font-family:sans-serif;fill:#111}'
            '.lbl{font-size:3.2px}.dim{font-size:2.6px;fill:#555}.ttl{font-size:4px;font-weight:bold}'
            '.note{font-size:2.6px;fill:#a00}</style>\n'
            f'<rect x="0" y="0" width="{w}" height="{h}" fill="#fff"/>\n')
def _dash(d): return f' stroke-dasharray="{d}"' if d else ''
def _circ(cx, cy, d, stroke='#111', dash=''):
    return (f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{d/2:.2f}" fill="none" '
            f'stroke="{stroke}" stroke-width="0.3"{_dash(dash)}/>\n')
def _cross(cx, cy, s=2.0):
    return (f'<line x1="{cx-s:.2f}" y1="{cy:.2f}" x2="{cx+s:.2f}" y2="{cy:.2f}" stroke="#111" stroke-width="0.2"/>\n'
            f'<line x1="{cx:.2f}" y1="{cy-s:.2f}" x2="{cx:.2f}" y2="{cy+s:.2f}" stroke="#111" stroke-width="0.2"/>\n')
def _rect(x, y, w, h, stroke='#111', dash='', sw=0.4):
    return (f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="none" '
            f'stroke="{stroke}" stroke-width="{sw}"{_dash(dash)}/>\n')
def _txt(x, y, s, cls='lbl', anchor='middle'):
    return f'<text x="{x:.2f}" y="{y:.2f}" class="{cls}" text-anchor="{anchor}">{s}</text>\n'

# ---- page: top-face view, then rear-wall strip, then ruler -----------------------------------
MARGIN = 12.0
PAGE_W = MARGIN*2 + max(EXT_W, BOX_W) + 40
PAGE_H = MARGIN*3 + EXT_D + WALL_H + 20

s = _hdr(PAGE_W, PAGE_H)
s += _txt(MARGIN, 7, "Tone Trixter 1590XX mock-up -- PRINT AT 100% (actual size). Verify the 100 mm ruler.", 'ttl', 'start')
s += _txt(MARGIN, 11, "Box: ext 145x121x39, wall 2, top-face plane 138x114, corner bosses O7. "
                      "All PART hole sizes are DEFAULTS -- check each vs the real part.", 'note', 'start')

ox, oy = MARGIN, MARGIN + 6
def TX(x): return ox + x
def TY(y): return oy + (BOX_D - y)    # flip so front (Y=0) is at the bottom

# external outline (dashed) + internal top-face plane (solid)
ext_off = (EXT_W - BOX_W) / 2.0
s += _rect(ox - ext_off, oy - (EXT_D - BOX_D), EXT_W, EXT_D, stroke='#999', dash='2 2', sw=0.3)
s += _rect(ox, oy, BOX_W, BOX_D, sw=0.6)
s += _txt(TX(BOX_W/2), oy - 2, "TOP FACE  (rear wall this edge)  138 deep x 114 wide", 'dim')
s += _txt(TX(BOX_W/2), TY(0)+5, "FRONT (toward player)", 'dim')

# corner bosses
for bx in (BOSS_IN, BOX_W - BOSS_IN):
    for by in (BOSS_IN, BOX_D - BOSS_IN):
        s += _circ(TX(bx), TY(by), BOSS_D, stroke='#999')

# candidate board outline (dashed): centred in width, fixed front edge behind the footswitches
bx0 = (BOX_W - BOARD_W)/2.0
by0 = BOARD_Y0
s += _rect(TX(bx0), TY(by0+BOARD_D), BOARD_W, BOARD_D, stroke='#06c', dash='3 2', sw=0.4)
s += _txt(TX(BOX_W/2), TY(by0+BOARD_D-5), f"candidate PCB {BOARD_W:g}x{BOARD_D:g} (dashed)", 'dim')
for mx in (bx0+BOARD_INSET_M3, bx0+BOARD_W-BOARD_INSET_M3):
    for my in (by0+BOARD_INSET_M3, by0+BOARD_D-BOARD_INSET_M3):
        s += _circ(TX(mx), TY(my), M3_DIA, stroke='#06c')

# footswitches
for fx in ((BOX_W-FS_CTR)/2.0, (BOX_W+FS_CTR)/2.0):
    s += _circ(TX(fx), TY(FS_Y), FS_DIA) + _cross(TX(fx), TY(FS_Y))
s += _txt(TX(BOX_W/2), TY(FS_Y)+8, f"footswitches O{FS_DIA:g} @ {FS_CTR:g} ctrs", 'dim')

# OLED window
s += _rect(TX(BOX_W/2-OLED_W/2), TY(OLED_CY+OLED_H/2), OLED_W, OLED_H, sw=0.4)
s += _txt(TX(BOX_W/2), TY(OLED_CY+OLED_H/2+3), f"OLED {OLED_W:g}x{OLED_H:g}", 'dim')
# encoder
s += _circ(TX(ENC_X), TY(ENC_CY), ENC_DIA) + _cross(TX(ENC_X), TY(ENC_CY))
s += _txt(TX(ENC_X), TY(ENC_CY)-5, f"enc O{ENC_DIA:g}", 'dim')
# LEDs
for lx in LED_X:
    s += _circ(TX(lx), TY(LED_Y), LED_DIA) + _cross(TX(lx), TY(LED_Y))
s += _txt(TX(BOX_W/2), TY(LED_Y)-4, "LED O3 x2 (shape-distinct)", 'dim')

# REAR WALL strip (below the top face)
ry = oy + EXT_D + 14
s += _txt(ox, ry - 3, "REAR WALL  (114 wide x 33 tall internal)  -- hole sizes default", 'dim', 'start')
s += _rect(ox, ry, BOX_W, WALL_H, sw=0.6)
for label, shape, size, xc in REAR:
    cy = ry + WALL_H/2
    if shape == "circ":
        s += _circ(ox+xc, cy, size) + _cross(ox+xc, cy)
        s += _txt(ox+xc, ry+WALL_H+4, f"{label} O{size:g}", 'dim')
    else:
        w, h = size
        s += _rect(ox+xc-w/2, cy-h/2, w, h, sw=0.4)
        s += _txt(ox+xc, ry+WALL_H+4, f"{label} {w:g}x{h:g}", 'dim')

# 100 mm scale ruler (print check)
ruy = ry + WALL_H + 12
s += f'<line x1="{ox}" y1="{ruy}" x2="{ox+100}" y2="{ruy}" stroke="#111" stroke-width="0.4"/>\n'
for i in range(0, 101, 10):
    s += f'<line x1="{ox+i}" y1="{ruy-2}" x2="{ox+i}" y2="{ruy+2}" stroke="#111" stroke-width="0.3"/>\n'
s += _txt(ox+50, ruy+6, "this bar MUST measure 100.0 mm -- if not, fix the print scale", 'note')

s += '</svg>\n'

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w') as f:
    f.write(s)
print(f"wrote {os.path.relpath(OUT)}  ({PAGE_W:.0f}x{PAGE_H:.0f} mm page)")
