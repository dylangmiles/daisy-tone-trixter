#!/usr/bin/env python3
"""gen_side_elevation_svg.py -- 1:1 side-elevation (Z-stack) of the Tone Trixter in a 1590XX.

Front-to-back vertical cross-section: the VERTICAL budget the flat top-face mock-up cannot show.
Items are PROJECTED onto the depth-height plane (OLED, encoder and jacks are at different widths X,
so this is a height study, not a true section). Print at 100% and check the 100 mm ruler.

✅ ALL heights MEASURED off the prototype + the box 2026-10-10 (recalibrated caliper). Board hangs
~8 mm below the top face; the Seed hangs below the board; jacks bottom-mount and land low on the wall.
Edit the dict and re-run. Stdlib only.

  python3 tools/gen_side_elevation_svg.py       # -> mockup/side_elevation_1590xx.svg
"""
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockup", "side_elevation_1590xx.svg")

# ---- box cross-section (depth x height), MEASURED 2026-10-10 --------------------------------
DEPTH    = 114.0   # internal depth (front 0 .. back wall)
H_INT    = 33.0    # ✅ internal cavity (35.30 box bottom->top of wall - 2.3 top face; base plate ~flush)
T_TOP    = 2.33     # top-face thickness
T_BASE   = 4.0     # base-plate thickness

# ---- board + stack (Z measured UP from the base inner surface) -------------------------------
# ✅ ALL MEASURED 2026-10-10 off the prototype (encoder, OLED, K3599, Seed) and the box -- see each line.
PCB_T      = 1.6
STANDOFF   = 8.24                 # ✅ board-to-top-face GAP: OLED glass (8.24, 2x2.54 spacers) flush; encoder body 7.23 clears
Z_BRD_TOP  = H_INT - STANDOFF    # board top surface height
Z_BRD_BOT  = Z_BRD_TOP - PCB_T
BRD_Y0, BRD_Y1 = 55.0, 112.0     # board depth span (from the top-face mock-up)

SEED_STACK = 16.5                # ✅ MEASURED (proto): mounting surface -> USB top = 16.5 (conservative)
SEED_Y0, SEED_Y1 = 60.0, 95.0    # where the Seed sits (depth)

OLED_GLASS_UNDER = 0.0           # ✅ glass at 8.24 above board (2x2.54 spacers) = the gap -> flush with the top-face inner
Z_GLASS    = H_INT - OLED_GLASS_UNDER
OLED_Y0, OLED_Y1 = 68.0, 100.0   # OLED module depth span

ENC_BODY_ABOVE   = 7.23          # ✅ encoder body (flat top) above the board -- measured
ENC_SHAFT_ABOVE_TOP = 16.61       # ✅ shaft top 26.61 above board; outer top face at gap+T_TOP -> ~16.6 proud (knob)
ENC_Y      = 84.0

JACK_AX_ABOVE_BRD = -13.99        # ✅ K3599 barrel axis 12.39 below the board BOTTOM (+1.6 PCB) = 13.99
                                 #    below the board top, pushed through. Bottom-mounted, hangs below.
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
s += _txt(LM-22, 11, "ALL heights MEASURED off the prototype + box 2026-10-10 (recalibrated caliper). Front=left, back wall=right.", 'note')
s += _txt(LM-22, 14.5, "Cavity 33 mm (35.30 box - 2.3 top), walls 2.33 mm. Gap 8.24 (OLED glass flush, encoder clears). Jack axis 12.4 below the board.", 'note')

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
s += _rect(DX(BRD_Y0+2), ZY(H_INT), 3.0, STANDOFF, fill='#ddd', sw=0.3)
s += _txt(DX(BRD_Y0+6), ZY(Z_BRD_TOP+STANDOFF/2), f"standoff {STANDOFF:g}", 'dim')

# --- Seed3 HANGING BELOW the board (from the PCB underside, down toward the base) ---
s += _rect(DX(SEED_Y0), ZY(Z_BRD_BOT), (SEED_Y1-SEED_Y0), SEED_STACK, fill='#dfd', stroke='#2a2', sw=0.5)
s += _txt(DX(SEED_Y0)+1.5, ZY(Z_BRD_BOT - SEED_STACK/2), f"Seed3 + sockets ~{SEED_STACK:g}", 'dim')
s += _txt(DX(SEED_Y0)+1.5, ZY(Z_BRD_BOT - SEED_STACK/2)+3, "(hangs below the PCB)", 'dim')

# --- OLED module reaching up to the glass (under the top face) ---
s += _rect(DX(OLED_Y0), ZY(Z_GLASS), (OLED_Y1-OLED_Y0), (Z_GLASS - Z_BRD_TOP), fill='#fee', stroke='#c33', sw=0.4)
s += _line(DX(OLED_Y0), ZY(Z_GLASS), DX(OLED_Y1), ZY(Z_GLASS), stroke='#c33', sw=0.6)
s += _txt(DX(OLED_Y0)+1, ZY(Z_GLASS)-1, "OLED glass (flush w/ top inner)", 'dim')

# --- encoder: body on board, shaft through the top, protruding ---
s += _rect(DX(ENC_Y-3), ZY(Z_BRD_TOP+ENC_BODY_ABOVE), 6.0, ENC_BODY_ABOVE, fill='#eef', stroke='#55a', sw=0.4)
s += _rect(DX(ENC_Y-1), ZY(H_INT + ENC_SHAFT_ABOVE_TOP), 2.0, (H_INT + ENC_SHAFT_ABOVE_TOP) - (Z_BRD_TOP+ENC_BODY_ABOVE), fill='#eef', stroke='#55a', sw=0.3)
s += _txt(DX(ENC_Y+4), ZY(H_INT + ENC_SHAFT_ABOVE_TOP/2), f"encoder shaft (+{ENC_SHAFT_ABOVE_TOP:g} over top)", 'dim')

# --- jack: BOTTOM-MOUNTED, barrel HANGS BELOW the board, pointing OUT through the back wall ---
s += _line(DX(BRD_Y1-6), ZY(Z_BRD_BOT), DX(BRD_Y1-6), ZY(Z_JACK + JACK_DIA/2), stroke='#111', sw=0.4)  # legs up to PCB
s += _rect(DX(100), ZY(Z_JACK + JACK_DIA/2), (DEPTH - 100 + 10), JACK_DIA, fill='#fff', stroke='#111', sw=0.5)
s += _line(DX(96), ZY(Z_JACK), DX(DEPTH)+11, ZY(Z_JACK), stroke='#111', dash='3 1', sw=0.3)
s += _txt(DX(99), ZY(Z_JACK)-JACK_DIA/2-1, f"jack z={Z_JACK:g} (bottom-mt)", 'dim')

# --- Z dimension scale on the left ---
xs = LM - 6
s += _line(xs, ZY(0), xs, ZY(H_INT + ENC_SHAFT_ABOVE_TOP), sw=0.3, stroke='#999')
for z, lab in [(0, "0 base"), (Z_BRD_BOT - SEED_STACK, f"{Z_BRD_BOT-SEED_STACK:.0f} Seed btm"),
               (Z_JACK, f"{Z_JACK:.0f} jack axis"),
               (Z_BRD_TOP, f"{Z_BRD_TOP:.0f} board/jack"),
               (H_INT, f"{H_INT:.0f} top inner")]:
    s += _line(xs-1.5, ZY(z), xs+1.5, ZY(z), sw=0.3, stroke='#999')
    s += _txt(xs-2.0, ZY(z)+0.9, lab, 'dim', 'end')

# --- the key constraint note ---
ny = ZY(0) + T_BASE + 13
s += _txt(LM-22, ny, f"Jacks BOTTOM-MOUNTED (barrel below the board) -> axis z=9.8 = ~33% up the 33 mm wall (lower third) -- barrel drop 12.4 + the 8.2 gap the", 'note')
s += _txt(LM-22, ny+3.5, "  gap the controls force. Check the plug + base-plate battery box do not crowd down there on the casting.", 'note')
s += _txt(LM-22, ny+7.0, "  Seed hangs to ~6.7 mm over the base (16.5 stack). TRS/DC bottom-mount, legs up; USB = Seed's own port.", 'note')
s += _txt(LM-22, ny+11.0, "Budget: Seed (16.5) + PCB (1.6) + gap (8.24) = 26.3 of the 33 mm cavity; ~6.7 mm spare over the base.", 'dim')

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
