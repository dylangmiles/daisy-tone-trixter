#!/usr/bin/env python3
"""gen_side_elevation.py -- 1:1 side-elevation (Z-stack) of the Tone Trixter in a 1590XX.

Front-to-back vertical cross-section: the VERTICAL budget the flat top-face mock-up cannot show.
Items are PROJECTED onto the depth-height plane (OLED, encoder and jacks are at different widths X,
so this is a height study, not a true section). Print at 100% and check the 100 mm ruler.

✅ ALL heights MEASURED off the prototype + the box 2026-10-10 (recalibrated caliper). Board hangs
~8 mm below the top face; the Seed hangs below the board; jacks bottom-mount and land low on the wall.
Edit the dict and re-run. Stdlib only.

  python3 enclosure/tools/gen_side_elevation.py  -> enclosure/out/side_elevation.svg
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # ../ -> enclosure/ (enclosure.py)
import enclosure as e

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "out", "side_elevation.svg")

# ---- box cross-section (depth x height) -- SHARED dims from enclosure.py (cannot drift) ------
# Same Z datum as the rear-wall fold-up flap (gen_template.py): z=0 = base (pedal bottom),
# z=H_INT = top inner = the top-rear FOLD edge. This elevation keeps base-at-bottom (reads the
# natural way up); the flap only draws z inverted because it physically folds over the back.
# ⚠ NOTE (2026-10-29, for a future datum flip): consider re-referencing z from the TOP-INNER face
#   instead of the base. The board registers to the TOP (OLED glass flush, encoder bushing nutted
#   through it) and top-referenced heights are MEASURABLE with the lid off; the base is a REMOVABLE
#   lid, so its reference can't be checked once closed. Not changed yet -- just flagged.
DEPTH    = e.BOX_D    # internal depth (front 0 .. back wall) = enclosure BOX_D
H_INT    = e.CAVITY_H # ✅ internal cavity (35.30 box - 2.3 top) = enclosure CAVITY_H
T_TOP    = e.WALL        # top-face thickness = enclosure WALL (top face carrying the holes)
T_BASE   = e.BOTTOM_LID  # base-plate / bottom-lid thickness = enclosure BOTTOM_LID (measured 4.23)
T_FR     = e.WALL_FR     # front & rear wall thickness = enclosure WALL_FR (measured 2.5)

# ---- board + stack (Z measured UP from the base inner surface) -------------------------------
# ✅ ALL MEASURED 2026-10-10 off the prototype (encoder, OLED, K3599, Seed) and the box -- see each line.
PCB_T      = 1.6
STANDOFF   = 7.0                  # board-to-top-face GAP. ⚠ LOWERED 8.24 -> 7.0 (2026-11-04) to fit the IN B combo
                                  #   (raises the board, lifts the jack row). 7.0 = the encoder-body height (RE-MEASURED
                                  #   2026-11-04) = the minimum gap; OLED is cable-mounted to the top face (gap-independent).
                                  #   ⚠ zero nominal encoder clearance (body just reaches the top inner).
Z_BRD_TOP  = H_INT - STANDOFF    # board top surface height
Z_BRD_BOT  = Z_BRD_TOP - PCB_T
BRD_Y0, BRD_Y1 = e.BRD_Y0, e.BRD_Y1   # board depth span (shared with enclosure.py)

SEED_STACK = 16.5                # ✅ MEASURED (proto): mounting surface -> USB top = 16.5 (conservative)
SEED_Y0, SEED_Y1 = 61.0, 80.0    # Seed depth span -- from the U4 footprint bbox (enc_y 61.2..80.0), 2026-10-30.
                                 #   (was 60..95 conservative; the module is crosswise so its LONG axis is X 44..97,
                                 #    Y is the ~19 mm module width; USB-C faces player-LEFT in -X, not +Y.)

OLED_GLASS_UNDER = 0.0           # ✅ glass flush with the top-face inner. OLED is CABLE-MOUNTED to the top face (2026-10-28),
                                 #   so the glass sits at the top inner regardless of the board gap.
Z_GLASS    = H_INT - OLED_GLASS_UNDER
OLED_Y0, OLED_Y1 = 68.0, 100.0   # OLED module depth span

ENC_BODY_ABOVE   = 7.0           # encoder body (flat top) above the board -- RE-MEASURED 2026-11-04 (was 7.23). This
                                 #   SETS the gap floor: STANDOFF = 7.0, so the body just reaches the top-face inner.
ENC_SHAFT_ABOVE_TOP = 16.61       # ✅ shaft top 26.61 above board; outer top face at gap+T_TOP -> ~16.6 proud (knob)
ENC_Y      = 84.0

JACK_AX_ABOVE_BRD = -15.45        # MEASURED 2026-10-29: K3599 barrel axis 13.85 below the board BOTTOM
                                 #    (barrel centre to board plane) + 1.6 PCB = 15.45 below the board top.
                                 #    Bottom-mounted, hangs below (was -13.99 / 12.39-below-bottom).
_Z_JACK_MEASURED = Z_BRD_TOP + JACK_AX_ABOVE_BRD      # measured -> ~9.31
Z_JACK     = e.JACK_Z             # AUTHORITATIVE rear-wall hole centre = fold-up template + PCB (9.31)
assert abs(_Z_JACK_MEASURED - Z_JACK) < 0.2, \
    f"jack-z DRIFT: measured stack {_Z_JACK_MEASURED:.2f} vs enclosure.JACK_Z {Z_JACK:.2f} -- reconcile before trusting the drawing"
JACK_DIA   = 10.0                 # TRS barrel, = enclosure.REAR trs size

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
PAGE_W = LM + DEPTH + 16     # extra right margin so the combo O24 protrusion past the back wall isn't clipped
PAGE_H = TM + (Z_TOP_DRAW + H_INT + T_BASE) + 22

def DX(y): return LM + y
def ZY(z): return TM + (Z_TOP_DRAW + H_INT) - z   # z=0 (base inner) low, up increases

s = _hdr(PAGE_W, PAGE_H)
s += _txt(LM-22, 7, "1590XX SIDE ELEVATION (height study, projected) -- PRINT AT 100%", 'ttl')
s += _txt(LM-22, 11, "ALL heights MEASURED off the prototype + box 2026-10-10 (recalibrated caliper). Front=left, back wall=right.", 'note')
s += _txt(LM-22, 14.5, f"Cavity {H_INT:g} mm. Top face {T_TOP:g}, F/R walls {T_FR:g}, bottom lid {T_BASE:g} mm (all measured). Gap {STANDOFF:g} (OLED glass flush, encoder clears). Jack axis {-JACK_AX_ABOVE_BRD:g} below the board.", 'note')
s += _txt(LM-22, 18, f"Z datum shared with the fold-up flap: z=0 = base (pedal bottom), z={H_INT:g} = top inner (the fold edge), jack z={Z_JACK:g}.", 'note')

# --- casting: base plate, top face, back wall (with jack hole), front wall ---
s += _rect(DX(0), ZY(0), DEPTH, T_BASE, fill='#eee', sw=0.5)                 # base plate (below z=0)
s += _rect(DX(0), ZY(H_INT) - T_TOP, DEPTH, T_TOP, fill='#eee', sw=0.5)      # top face (above z=H_INT)
# back wall (right), split around the jack hole
s += _rect(DX(DEPTH), ZY(H_INT) - T_TOP, T_FR, (H_INT + T_TOP + T_BASE), fill='#eee', sw=0.5)
s += _rect(DX(0) - T_FR, ZY(H_INT) - T_TOP, T_FR, (H_INT + T_TOP + T_BASE), fill='#eee', sw=0.5)  # front wall
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
_enc_clear = STANDOFF - ENC_BODY_ABOVE   # <0 => encoder body exceeds the gap (flagged in the notes below)

# --- jack: BOTTOM-MOUNTED, barrel HANGS BELOW the board, pointing OUT through the back wall ---
s += _line(DX(BRD_Y1-6), ZY(Z_BRD_BOT), DX(BRD_Y1-6), ZY(Z_JACK + JACK_DIA/2), stroke='#111', sw=0.4)  # legs up to PCB
s += _rect(DX(100), ZY(Z_JACK + JACK_DIA/2), (DEPTH - 100 + 10), JACK_DIA, fill='#fff', stroke='#111', sw=0.5)
s += _line(DX(96), ZY(Z_JACK), DX(DEPTH)+11, ZY(Z_JACK), stroke='#111', dash='3 1', sw=0.3)
s += _txt(DX(99), ZY(Z_JACK)-JACK_DIA/2-1, f"OUT/IN A/DC jack z={Z_JACK:g}", 'dim')

# --- IN B COMBO (NCJ9FI-H): O24 cut-out + O23.4 barrel, axis 1.15 below the TRS row; MARGINAL fit ---
_z_combo_chk = round(Z_BRD_BOT - 15.0, 2)   # datasheet axis 15.0 above the PCB mount surface
assert abs(_z_combo_chk - e.COMBO_Z) < 0.1, f"combo-z DRIFT: stack {_z_combo_chk} vs enclosure.COMBO_Z {e.COMBO_Z}"
s += _circ(DX(DEPTH), ZY(e.COMBO_Z), e.COMBO_D, stroke='#c33')                      # O24 cut-out
s += _circ(DX(DEPTH), ZY(e.COMBO_Z), 23.4, stroke='#c33', dash='1.5 1')             # O23.4 barrel
for _sy in (e.COMBO_SCREW_DY, -e.COMBO_SCREW_DY):                                    # 2x M3 screws (projected onto the section)
    s += _circ(DX(DEPTH), ZY(e.COMBO_Z + _sy), e.COMBO_SCREW_D, stroke='#c33')
s += _line(DX(DEPTH)-6, ZY(e.COMBO_Z), DX(DEPTH)+13, ZY(e.COMBO_Z), stroke='#c33', dash='3 1', sw=0.3)
# recessed-lid floor the barrel reaches into (over the centre, inboard of the rim)
s += _line(DX(DEPTH-20), ZY(e.Z_RECESS), DX(DEPTH)+11, ZY(e.Z_RECESS), stroke='#c33', dash='2 1', sw=0.3)
s += _txt(DX(DEPTH)+11, ZY(e.Z_RECESS)+2.5, f"recess floor {e.Z_RECESS:g}", 'dim', 'end')
s += _txt(DX(DEPTH)-e.COMBO_D/2-1, ZY(e.COMBO_Z)-e.COMBO_D/2-1, f"IN B combo O{e.COMBO_D:g} cut-out / O23.4 barrel @ z{e.COMBO_Z:g}", 'note', 'end')
s += _txt(DX(DEPTH)-e.COMBO_D/2-1, ZY(e.COMBO_Z)-e.COMBO_D/2+2, f"MARGINAL: barrel btm {e.COMBO_Z-11.7:.2f} vs recess {e.Z_RECESS:g} (~0.1mm); grind rim + file 30mm ring", 'dim', 'end')

# --- HP jack on the LEFT wall, shown END-ON (its barrel runs in X, out of this section plane) ---
_hp_y = e.LEFT[0][2]
_hp_dia = e.LEFT[0][3]
s += _circ(DX(_hp_y), ZY(e.HP_Z), _hp_dia, stroke='#2a2', dash='2 1')
s += _txt(DX(_hp_y)-5, ZY(e.HP_Z)-5.0, "HP (left wall, end-on)", 'dim', 'end')
s += _txt(DX(_hp_y)-5, ZY(e.HP_Z)-2.5, f"O{_hp_dia:g} @ z{e.HP_Z:g}; y={_hp_y:g}, fwd of OUT TRS body (Seed y80 .. TRS y88 gap)", 'dim', 'end')

# --- Z dimension scale on the left ---
xs = LM - 6
s += _line(xs, ZY(0), xs, ZY(H_INT + ENC_SHAFT_ABOVE_TOP), sw=0.3, stroke='#999')
for z, lab in [(0, "0 base (btm)"), (Z_BRD_BOT - SEED_STACK, f"{Z_BRD_BOT-SEED_STACK:.0f} Seed btm"),
               (Z_JACK, f"{Z_JACK:g} jack axis"),
               (Z_BRD_TOP, f"{Z_BRD_TOP:.0f} board top"),
               (H_INT, f"{H_INT:g} top inner (fold)")]:
    s += _line(xs-1.5, ZY(z), xs+1.5, ZY(z), sw=0.3, stroke='#999')
    s += _txt(xs-2.0, ZY(z)+0.9, lab, 'dim', 'end')

# --- the key constraint note ---
ny = ZY(0) + T_BASE + 13
_stack = SEED_STACK + PCB_T + STANDOFF
s += _txt(LM-22, ny, f"Jacks BOTTOM-MOUNTED: barrel hangs below the board -> axis z={Z_JACK:g} (~{Z_JACK/H_INT*100:.0f}% up the {H_INT:g} mm wall, lower third).", 'note')
s += _txt(LM-22, ny+3.5, f"Budget: Seed {SEED_STACK:g} + PCB {PCB_T:g} + gap {STANDOFF:g} = {_stack:.1f} of {H_INT:g} mm cavity -> ~{H_INT-_stack:.1f} mm spare over the base.", 'note')
if _enc_clear < 0:
    s += _txt(LM-22, ny+7.0, f"⚠ ENCODER vs GAP: body {ENC_BODY_ABOVE:g} > gap {STANDOFF:g} by {-_enc_clear:.2f} mm -> body would poke the top face. Re-measure the encoder; if 7.23 holds, 6.65 is too tight.", 'note')
s += _txt(LM-22, ny+10.5, f"Check the plug + base-plate battery box don't crowd down there. TRS/DC bottom-mount (legs up); USB = PAN-USBCC panel socket O12 @ z{e.USB_Z:g}, barrel 16.9 into the under-board space (Seed USB faces player-L, lead loops to it).", 'dim')

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
