#!/usr/bin/env python3
"""gen_side_elevation_svg.py -- 1:1 side-elevation (Z-stack) of the Tone Trixter in a 1590XX.

Front-to-back vertical cross-section: the VERTICAL budget the flat top-face mock-up cannot show.
Items are PROJECTED onto the depth-height plane (OLED, encoder and jacks are at different widths X,
so this is a height study, not a true section). Print at 100% and check the 100 mm ruler.

⚠ EVERY height is a FLAGGED DEFAULT -- the whole point. Seeded with the brief's validated vertical
budget (board 6 mm below the top face, Seed + components hanging below, ~8 mm over the base). Edit
the dict and re-run. Stdlib only.

  python3 tools/gen_side_elevation_svg.py       # -> mockup/side_elevation_1590xx.svg
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockup", "side_elevation_1590xx.svg")

# ---- box cross-section (depth x height), all DEFAULT -----------------------------------------
DEPTH    = 114.0   # internal depth (front 0 .. back wall)
H_INT    = 33.0    # internal cavity height  (= ext 39 - base 4 - top 2)  ⚠ verify
T_TOP    = 2.0     # top-face thickness
T_BASE   = 4.0     # base-plate thickness

# ---- board + stack (Z measured UP from the base inner surface), all DEFAULT ------------------
PCB_T      = 1.6
STANDOFF   = 6.0                 # board sits this far below the top-face inner surface
Z_BRD_TOP  = H_INT - STANDOFF    # = 27: board top surface height
Z_BRD_BOT  = Z_BRD_TOP - PCB_T
BRD_Y0, BRD_Y1 = 55.0, 112.0     # board depth span (from the top-face mock-up)

SEED_STACK = 17.0                # sockets 8.5 + Seed PCB 1.6 + tallest parts ~7, hanging BELOW
SEED_Y0, SEED_Y1 = 60.0, 95.0    # where the Seed sits (depth)

OLED_GLASS_UNDER = 1.0           # glass sits this far under the top-face inner
Z_GLASS    = H_INT - OLED_GLASS_UNDER     # = 32
OLED_Y0, OLED_Y1 = 68.0, 100.0   # OLED module depth span

ENC_BODY_ABOVE   = 12.0          # encoder body height above the board
ENC_SHAFT_ABOVE_TOP = 8.0        # shaft protrusion above the OUTER top face
ENC_Y      = 84.0

JACK_AX_ABOVE_BRD = 0.0          # ⚠ jack axis relative to the board top -- THE number that must
                                 #    equal the rear-wall hole height. 0 = axis at board level.
Z_JACK     = Z_BRD_TOP + JACK_AX_ABOVE_BRD   # rear-wall hole centre height
JACK_DIA   = 10.0

# ---- SVG helpers -----------------------------------------------------------------------------
def _hdr(w, h):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}mm" height="{h}mm" viewBox="0 0 {w} {h}">\n'
            '<style>text{font-family:sans-serif;fill:#111}.lbl{font-size:3px}.dim{font-size:2.5px;fill:#555}'
            '.ttl{font-size:3.8px;font-weight:bold}.note{font-size:2.5px;fill:#a00}</style>\n'
            f'<rect x="0" y="0" width="{w}" height="{h}" fill="#fff"/>\n')
def _rect(x, y, w, h, stroke='#111', fill='none', dash='', sw=0.4):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>\n'
def _line(x1, y1, x2, y2, stroke='#111', dash='', sw=0.3):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{stroke}" stroke-width="{sw}"{d}/>\n'
def _circ(cx, cy, d, stroke='#111', dash=''):
    da = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{d/2:.2f}" fill="none" stroke="{stroke}" stroke-width="0.3"{da}/>\n'
def _txt(x, y, s, cls='lbl', anchor='start'):
    return f'<text x="{x:.2f}" y="{y:.2f}" class="{cls}" text-anchor="{anchor}">{s}</text>\n'

# ---- page / transforms -----------------------------------------------------------------------
LM, TM = 34.0, 20.0          # left margin (for the Z scale), top margin
Z_TOP_DRAW = ENC_SHAFT_ABOVE_TOP + T_TOP + 4    # headroom above the box for the shaft
PAGE_W = LM + DEPTH + 10
PAGE_H = TM + (Z_TOP_DRAW + H_INT + T_BASE) + 22

def DX(y): return LM + y
def ZY(z): return TM + (Z_TOP_DRAW + H_INT) - z   # z=0 (base inner) low, up increases

s = _hdr(PAGE_W, PAGE_H)
s += _txt(LM-22, 7, "1590XX SIDE ELEVATION (height study, projected) -- PRINT AT 100%", 'ttl')
s += _txt(LM-22, 11, "ALL heights DEFAULT (brief's vertical budget). Front = left, back wall = right.", 'note')
s += _txt(LM-22, 14.5, "⚠ the jack-axis height and the rear-wall hole must match -- see the red note.", 'note')

# --- casting: base plate, top face, back wall (with jack hole), front wall ---
s += _rect(DX(0), ZY(0), DEPTH, T_BASE, fill='#eee', sw=0.5)                 # base plate (below z=0)
s += _rect(DX(0), ZY(H_INT) - T_TOP, DEPTH, T_TOP, fill='#eee', sw=0.5)      # top face (above z=H_INT)
# back wall (right), split around the jack hole
s += _rect(DX(DEPTH), ZY(H_INT) - T_TOP, 2.0, (H_INT + T_TOP + T_BASE), fill='#eee', sw=0.5)
s += _rect(DX(0) - 2.0, ZY(H_INT) - T_TOP, 2.0, (H_INT + T_TOP + T_BASE), fill='#eee', sw=0.5)  # front wall
# cavity outline
s += _rect(DX(0), ZY(H_INT), DEPTH, H_INT, sw=0.3, stroke='#bbb')
s += _txt(DX(2), ZY(H_INT)-T_TOP-1.5, "top face", 'dim')
s += _txt(DX(2), ZY(0)+T_BASE+3.5, "base plate", 'dim')
s += _txt(DX(DEPTH/2), ZY(0)+T_BASE+8, "FRONT (toward player)   ---   BACK WALL (jacks)", 'dim', 'middle')

# --- board ---
s += _rect(DX(BRD_Y0), ZY(Z_BRD_TOP), (BRD_Y1-BRD_Y0), PCB_T, fill='#cde', stroke='#06c', sw=0.4)
s += _txt(DX(BRD_Y0)+1, ZY(Z_BRD_TOP)-1, "PCB", 'dim')
# standoff to the top face (front edge of the board)
s += _rect(DX(BRD_Y0+2), ZY(H_INT)-T_TOP, 3.0, STANDOFF, fill='#ddd', sw=0.3)
s += _txt(DX(BRD_Y0+6), ZY(Z_BRD_TOP+STANDOFF/2), f"standoff {STANDOFF:g}", 'dim')

# --- Seed hanging below the board ---
s += _rect(DX(SEED_Y0), ZY(Z_BRD_BOT - SEED_STACK), (SEED_Y1-SEED_Y0), SEED_STACK, fill='#efe', stroke='#2a2', sw=0.4)
s += _txt(DX(SEED_Y0)+1, ZY(Z_BRD_BOT - SEED_STACK/2), f"Seed3 on sockets ~{SEED_STACK:g}", 'dim')

# --- OLED module reaching up to the glass (under the top face) ---
s += _rect(DX(OLED_Y0), ZY(Z_GLASS), (OLED_Y1-OLED_Y0), (Z_GLASS - Z_BRD_TOP), fill='#fee', stroke='#c33', sw=0.4)
s += _line(DX(OLED_Y0), ZY(Z_GLASS), DX(OLED_Y1), ZY(Z_GLASS), stroke='#c33', sw=0.6)
s += _txt(DX(OLED_Y0)+1, ZY(Z_GLASS)-1, f"OLED glass @ top-{OLED_GLASS_UNDER:g}", 'dim')

# --- encoder: body on board, shaft through the top, protruding ---
s += _rect(DX(ENC_Y-3), ZY(Z_BRD_TOP+ENC_BODY_ABOVE), 6.0, ENC_BODY_ABOVE, fill='#eef', stroke='#55a', sw=0.4)
s += _rect(DX(ENC_Y-1), ZY(H_INT + ENC_SHAFT_ABOVE_TOP), 2.0, (H_INT + ENC_SHAFT_ABOVE_TOP) - (Z_BRD_TOP+ENC_BODY_ABOVE), fill='#eef', stroke='#55a', sw=0.3)
s += _txt(DX(ENC_Y+4), ZY(H_INT + ENC_SHAFT_ABOVE_TOP/2), f"encoder shaft (+{ENC_SHAFT_ABOVE_TOP:g} over top)", 'dim')

# --- jack through the back wall ---
s += _circ(DX(DEPTH)+1, ZY(Z_JACK), JACK_DIA, stroke='#111')
s += _line(DX(BRD_Y1), ZY(Z_JACK), DX(DEPTH)+6, ZY(Z_JACK), stroke='#111', dash='2 1', sw=0.3)
s += _txt(DX(DEPTH)-26, ZY(Z_JACK)-2, f"jack axis z={Z_JACK:g}", 'dim')

# --- Z dimension scale on the left ---
xs = LM - 6
s += _line(xs, ZY(0), xs, ZY(H_INT + ENC_SHAFT_ABOVE_TOP), sw=0.3, stroke='#999')
for z, lab in [(0, "0 base"), (Z_BRD_BOT - SEED_STACK, f"{Z_BRD_BOT-SEED_STACK:.0f} Seed btm"),
               (Z_BRD_TOP, f"{Z_BRD_TOP:.0f} board/jack"), (Z_GLASS, f"{Z_GLASS:.0f} glass"),
               (H_INT, f"{H_INT:.0f} top inner")]:
    s += _line(xs-1.5, ZY(z), xs+1.5, ZY(z), sw=0.3, stroke='#999')
    s += _txt(xs-2.0, ZY(z)+0.9, lab, 'dim', 'end')

# --- the key constraint note ---
ny = ZY(0) + T_BASE + 13
s += _txt(LM-22, ny, "⚠ JACK AXIS (z=27, board level) is HIGH on the 33 mm wall. For a mid-wall jack:", 'note')
s += _txt(LM-22, ny+3.5, "   lower the board (compresses the Seed space below) OR use a jack whose axis sits", 'note')
s += _txt(LM-22, ny+7.0, "   below the board OR keep panel-mount jacks on flying leads (height-independent).", 'note')
s += _txt(LM-22, ny+11.0, "Budget: Seed-on-sockets (~17) + standoff (6) nearly fills the 33 mm; ~8 mm spare over base.", 'dim')

# --- 100 mm ruler ---
ruy = ny + 16
s += _line(LM-22, ruy, LM-22+100, ruy, sw=0.4)
for i in range(0, 101, 10):
    s += _line(LM-22+i, ruy-2, LM-22+i, ruy+2, sw=0.3)
s += _txt(LM-22+50, ruy+5, "this bar MUST measure 100.0 mm", 'note', 'middle')

s += '</svg>\n'
os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w') as f:
    f.write(s)
print(f"wrote {os.path.relpath(OUT)}  ({PAGE_W:.0f}x{PAGE_H:.0f} mm)")
