#!/usr/bin/env python3
"""gen_mockup_svg.py -- 1:1 paper mock-up template for the Tone Trixter in a Hammond 1590XX.

Brief §1/§7: "mock the face up 1:1 on paper, then draw the board." This writes an SVG at TRUE
1:1 (mm units). Print at 100% / "actual size", confirm the 100 mm ruler measures 100.0 mm, cut it
out, tape it to the casting, and mark any corrections.

Measured box (2026-10-09, box in hand):
  external 145 x 121 x 39 mm, wall 2 mm, internal TOP-FACE PLANE 138 x 114, four O7 corner bosses.

Orientation (revised 2026-10-09): LONG SIDE HORIZONTAL -> 138 wide (left-right) x 114 deep
(front-back). Wider back wall for the jacks, wider footswitch spacing.

Board strategy (revised 2026-10-09): FULL-WIDTH board hugging the BACK WALL, rear corners notched
for the O7 bosses, registered by board-mounted TS jacks along the rear edge (sidesteps tapping M3
into the 2 mm top face). ⚠ jacks stay the ISOLATED-bushing plastic type (shielding logic).

⚠ Every PART HOLE SIZE is a DEFAULT to verify against the real part. Stdlib only.
  python3 tools/gen_mockup_svg.py              # -> mockup/mockup_1590xx.svg
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockup", "mockup_1590xx.svg")

# ---- measured box (top-face plane; LONG side horizontal) ------------------------------------
BOX_W   = 138.0   # internal width  (left-right)  <- long side
BOX_D   = 114.0   # internal depth  (front-back)
EXT_W   = 145.0
EXT_D   = 121.0
BOSS_D  = 7.0     # corner lid boss diameter
BOSS_IN = 6.0     # boss centre inset from each wall (approx -- verify)

# ---- top-face holes (X = width 0..138 from left wall, Y = depth 0..114 from FRONT wall) ------
# ⚠ DEFAULT diameters -- verify against the real parts.
FS_DIA   = 12.0   # footswitch, SPST momentary
FS_Y     = 22.0   # from the front wall
FS_CTR   = 90.0   # centre-to-centre (wider, now that the box is turned)
ENC_DIA  = 7.0    # PEC11R bushing  (⚠ nylon shoulder washer: bushing is a GPIO pull-up)
OLED_W, OLED_H = 35.0, 20.0   # SH1106 1.3" VISIBLE display window (glass/module is larger)
LED_DIA  = 3.0

LED_Y    = 44.0
LED_X    = (55.0, 83.0)
OLED_CY  = 66.0   # OLED window centre, depth
ENC_CY   = 66.0
ENC_X    = 108.0  # encoder to the right of the OLED

# ---- full-width board hugging the back wall, rear corners notched for the bosses -------------
BRD_X0, BRD_X1 = 3.0, 135.0     # ~132 wide: 3 mm clearance each side (wall taper + fab tol + edge rule)
BRD_Y0, BRD_Y1 = 36.0, 112.0    # front edge (behind switches) .. rear edge (2 mm off back wall)
NOTCH          = 12.0           # rear-corner cut to clear the O7 bosses
M3_DIA         = 3.2

# ---- jacks: board-mounted at the rear edge, through the back wall (X = width) ----------------
WALL_H   = 33.0
XLR_SCREW = 9.5   # vertical screw-hole offset from barrel centre (verify vs chosen XLR/combo part)
REAR = [  # (label, shape, size, x_centre)   across the 138 wide back wall, XLR grouped with IN
    ("XLR", "xlr",  24.0, 26.0),    # v2 mic input -- RESERVED area, NOT drilled now (barrel ~Ø24 + 2 screws)
    ("IN",  "circ", 10.0, 52.0),
    ("DC",  "circ", 12.0, 74.0),
    ("USB", "rect", (13.0, 8.0), 96.0),
    ("OUT", "circ", 10.0, 118.0),
]
JACK_Y = BRD_Y1 - 4.0   # where the jack bodies sit on the board (rear edge)

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
def _poly(pts, stroke='#06c', dash='3 2', sw=0.4):
    p = ' '.join(f'{x:.2f},{y:.2f}' for x, y in pts)
    return f'<polygon points="{p}" fill="none" stroke="{stroke}" stroke-width="{sw}"{_dash(dash)}/>\n'
def _txt(x, y, s, cls='lbl', anchor='middle'):
    return f'<text x="{x:.2f}" y="{y:.2f}" class="{cls}" text-anchor="{anchor}">{s}</text>\n'

# ---- page ------------------------------------------------------------------------------------
MARGIN = 12.0
PAGE_W = MARGIN*2 + max(EXT_W, BOX_W) + 6
PAGE_H = MARGIN*3 + EXT_D + WALL_H + 20

s = _hdr(PAGE_W, PAGE_H)
s += _txt(MARGIN, 7, "1590XX mock-up (landscape) -- PRINT AT 100%, verify the 100 mm ruler", 'ttl', 'start')
s += _txt(MARGIN, 11.0, "Box: ext 145x121x39, wall 2, top-face plane 138x114, O7 corner bosses.", 'note', 'start')
s += _txt(MARGIN, 14.5, "Full-width notched board; jacks board-mounted. XLR = v2 mic RESERVE (red, not drilled). Sizes DEFAULT.", 'note', 'start')

ox, oy = MARGIN, MARGIN + 9
def TX(x): return ox + x
def TY(y): return oy + (BOX_D - y)    # flip so front (Y=0) is at the bottom

# external outline (dashed) + internal top-face plane (solid)
ext_off = (EXT_W - BOX_W) / 2.0
s += _rect(ox - ext_off, oy - (EXT_D - BOX_D), EXT_W, EXT_D, stroke='#999', dash='2 2', sw=0.3)
s += _rect(ox, oy, BOX_W, BOX_D, sw=0.6)
s += _txt(TX(BOX_W/2), oy - 2, "TOP FACE  (back wall this edge)  138 wide x 114 deep", 'dim')
s += _txt(TX(BOX_W/2), TY(0)+5, "FRONT (toward player)", 'dim')

# corner bosses
for bx in (BOSS_IN, BOX_W - BOSS_IN):
    for by in (BOSS_IN, BOX_D - BOSS_IN):
        s += _circ(TX(bx), TY(by), BOSS_D, stroke='#999')

# full-width board, rear corners notched (polygon in board coords, then transform)
brd = [
    (BRD_X0, BRD_Y0), (BRD_X1, BRD_Y0),
    (BRD_X1, BRD_Y1 - NOTCH), (BRD_X1 - NOTCH, BRD_Y1),
    (BRD_X0 + NOTCH, BRD_Y1), (BRD_X0, BRD_Y1 - NOTCH),
]
s += _poly([(TX(x), TY(y)) for x, y in brd])
s += _txt(TX(BOX_W/2), TY(BRD_Y0 + 5), "full-width board (dashed) -- rear corners notched for bosses", 'dim')

# footswitches (panel-mounted, in front of the board)
for fx in ((BOX_W-FS_CTR)/2.0, (BOX_W+FS_CTR)/2.0):
    s += _circ(TX(fx), TY(FS_Y), FS_DIA) + _cross(TX(fx), TY(FS_Y))
s += _txt(TX(BOX_W/2), TY(FS_Y)+7, f"footswitches O{FS_DIA:g} @ {FS_CTR:g} ctrs", 'dim')

# LEDs
for lx in LED_X:
    s += _circ(TX(lx), TY(LED_Y), LED_DIA) + _cross(TX(lx), TY(LED_Y))
s += _txt(TX(BOX_W/2), TY(LED_Y)-4, "LED O3 x2 (shape-distinct)", 'dim')

# OLED window
s += _rect(TX(BOX_W/2-OLED_W/2), TY(OLED_CY+OLED_H/2), OLED_W, OLED_H, sw=0.4)
s += _txt(TX(BOX_W/2), TY(OLED_CY+OLED_H/2+3), f"OLED {OLED_W:g}x{OLED_H:g}", 'dim')
# encoder
s += _circ(TX(ENC_X), TY(ENC_CY), ENC_DIA) + _cross(TX(ENC_X), TY(ENC_CY))
s += _txt(TX(ENC_X), TY(ENC_CY)-5, f"enc O{ENC_DIA:g}", 'dim')

# jacks board-mounted at the rear edge (shown on the top view where they sit on the board)
for label, shape, size, xc in REAR:
    if shape == "circ":
        s += _circ(TX(xc), TY(JACK_Y), size)
    elif shape == "xlr":
        s += _circ(TX(xc), TY(JACK_Y), size, stroke='#a00', dash='2 2')
        s += _txt(TX(xc), TY(JACK_Y)+2, "v2", 'dim')
    else:
        w, h = size
        s += _rect(TX(xc-w/2), TY(JACK_Y+h/2), w, h, sw=0.3)
    s += _txt(TX(xc), TY(JACK_Y)-5, label, 'dim')

# ---- REAR WALL drilling strip (138 wide) ----
ry = oy + EXT_D + 14
s += _txt(ox, ry - 3, "REAR WALL drilling strip (138 wide x 33 tall internal) -- same X as the jacks above", 'dim', 'start')
s += _rect(ox, ry, BOX_W, WALL_H, sw=0.6)
for label, shape, size, xc in REAR:
    cy = ry + WALL_H/2
    if shape == "circ":
        s += _circ(ox+xc, cy, size) + _cross(ox+xc, cy)
        s += _txt(ox+xc, ry+WALL_H+4, f"{label} O{size:g}", 'dim')
    elif shape == "xlr":
        s += _circ(ox+xc, cy, size, stroke='#a00', dash='2 2')
        for dy in (-XLR_SCREW, XLR_SCREW):
            s += _cross(ox+xc, cy+dy)
        s += _txt(ox+xc, ry+WALL_H+4, f"{label} O{size:g} (v2 reserve)", 'dim')
    else:
        w, h = size
        s += _rect(ox+xc-w/2, cy-h/2, w, h, sw=0.4)
        s += _txt(ox+xc, ry+WALL_H+4, f"{label} {w:g}x{h:g}", 'dim')

# ---- 100 mm scale ruler (print check) ----
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
