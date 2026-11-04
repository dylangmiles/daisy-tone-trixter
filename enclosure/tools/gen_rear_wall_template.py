#!/usr/bin/env python3
"""gen_rear_wall_template.py -- 1:1 rear-wall drilling / fit template (viewed from BEHIND).

Shows the rear-wall holes (OUT, USB, DC, IN B combo, IN A) at their real X (mirrored, as seen from
behind the pedal) and Z (above the inside base), plus the IN B NCJ9FI-H combo O24 cut-out + O23.4
barrel + 2x M3 screw holes, and the lid floors (rim z=0, recessed centre, external bottom) so the
marginal combo fit can be checked against the real lid. ALL positions come from enclosure.py (6.65 gap).

Print at 100% and ruler-check the 50 mm bar.  Stdlib only.
  python3 enclosure/tools/gen_rear_wall_template.py  -> enclosure/out/rear_wall_xlr_template.svg
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import enclosure as e

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "out", "rear_wall_xlr_template.svg")

BARREL_D = 23.4                        # NCJ9FI-H barrel OD (datasheet); the O24 is the wall cut-out
Z_EXT = -e.BOTTOM_LID                  # external pedal bottom face
CZ = [r[5] for r in e.REAR if r[4] == "combo"][0]     # combo barrel axis Z (= e.COMBO_Z)
GAP = round(e.CAVITY_H - (CZ + 15.0 + 1.6), 2)        # board-to-top gap, derived (combo axis -> board bottom -> gap)

ML, MR, MT, MB = 25, 46, 36, 44
Z_LO, Z_HI = -7.0, 36.0
W = e.BOX_W + ML + MR
H = (Z_HI - Z_LO) + MT + MB
def mx(x):  return e.BOX_W - x         # mirror: viewed from BEHIND
def sx(xm): return ML + xm
def sy(z):  return MT + (Z_HI - z)

s = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}" font-family="sans-serif">']
s.append(f'<rect width="{W}" height="{H}" fill="#fff"/>')
s.append(f'<text x="{ML}" y="11" font-size="4.0" font-weight="bold">Tone Trixie - rear-wall XLR fit template - 1:1 - viewed from BEHIND (outside)</text>')
s.append(f'<text x="{ML}" y="16.5" font-size="2.7" fill="#333">{GAP:g} mm gap (board raised to fit the combo). Register BOX-CENTRE to the box centre; z=0 to where the rear RIM meets the cavity. PRINT 100%, ruler-check the 50 mm bar.</text>')
# lid band + cavity
s.append(f'<rect x="{sx(0)}" y="{sy(0)}" width="{e.BOX_W}" height="{(0 - Z_EXT)}" fill="#f0f0f0" stroke="#ccc" stroke-width="0.2"/>')
s.append(f'<rect x="{sx(0)}" y="{sy(e.CAVITY_H)}" width="{e.BOX_W}" height="{e.CAVITY_H}" fill="none" stroke="#000" stroke-width="0.4"/>')
# floor lines
def floor(z, txt, bold):
    w = 0.7 if bold else 0.35
    dash = '' if bold else ' stroke-dasharray="3 1.5"'
    s.append(f'<line x1="{sx(-4)}" y1="{sy(z)}" x2="{sx(e.BOX_W+4)}" y2="{sy(z)}" stroke="#000" stroke-width="{w}"{dash}/>')
    s.append(f'<text x="{sx(e.BOX_W+4.5)}" y="{sy(z)+1}" font-size="2.5">{txt}</text>')
floor(0,          'z=0 RIM floor (grind here)', True)
floor(e.Z_RECESS, f'z={e.Z_RECESS:.2f} RECESS floor', False)
floor(Z_EXT,      f'z={Z_EXT:g} EXT bottom (feet)', False)
s.append(f'<text x="{sx(-4)}" y="{sy(e.CAVITY_H)-1}" font-size="2.6">TOP-INNER z=33</text>')
# box centre
s.append(f'<line x1="{sx(mx(69))}" y1="{sy(Z_HI)}" x2="{sx(mx(69))}" y2="{sy(Z_LO)}" stroke="#000" stroke-width="0.3" stroke-dasharray="2 1.5"/>')
s.append(f'<text x="{sx(mx(69))}" y="{sy(Z_HI)-1}" font-size="2.5" text-anchor="middle">BOX CENTRE</text>')
# neighbour holes
for ref, label, x, size, kind, z in e.REAR:
    if kind == "combo":
        continue
    cx, cy, r = sx(mx(x)), sy(z), size/2
    s.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="#777" stroke-width="0.3"/>')
    s.append(f'<circle cx="{cx}" cy="{cy}" r="0.4" fill="#777"/>')
    s.append(f'<text x="{cx}" y="{cy-r-0.8}" font-size="2.2" fill="#555" text-anchor="middle">{label} Ø{size:g} z{z:g}</text>')
# IN B combo: Ø24 cut-out + Ø23.4 barrel + 2x M3 screws
ccx, ccy = sx(mx(88.0)), sy(CZ)
s.append(f'<circle cx="{ccx}" cy="{ccy}" r="{e.COMBO_D/2}" fill="none" stroke="#000" stroke-width="0.9"/>')
s.append(f'<circle cx="{ccx}" cy="{ccy}" r="{BARREL_D/2}" fill="none" stroke="#000" stroke-width="0.35" stroke-dasharray="1.5 1"/>')
s.append(f'<circle cx="{ccx}" cy="{ccy}" r="0.6" fill="#000"/>')
# Bottom-mounted combo -> LL+UR diagonal in the enclosure frame (player-natural): left screw LOW,
# right screw HIGH. This view is MIRRORED (from behind), so the screw X goes through mx() like the barrel.
for sdx, sdy in ((-e.COMBO_SCREW_DX, -e.COMBO_SCREW_DY), (e.COMBO_SCREW_DX, e.COMBO_SCREW_DY)):
    s.append(f'<circle cx="{sx(mx(88.0+sdx))}" cy="{sy(CZ+sdy)}" r="{e.COMBO_SCREW_D/2}" fill="none" stroke="#000" stroke-width="0.5"/>')
s.append(f'<text x="{ccx}" y="{ccy-e.COMBO_D/2-2.2}" font-size="2.9" font-weight="bold" text-anchor="middle">IN B combo Ø{e.COMBO_D:g} cut-out / Ø{BARREL_D:g} barrel @ z{CZ:g}</text>')
s.append(f'<text x="{ccx}" y="{ccy-e.COMBO_D/2-5.4}" font-size="2.4" text-anchor="middle">+2x M3 screws {2*e.COMBO_SCREW_DX:g}x{2*e.COMBO_SCREW_DY:g} diag (mark from part)</text>')
_bb = CZ - BARREL_D/2
s.append(f'<text x="{ccx}" y="{sy(Z_LO)+2}" font-size="2.7" font-weight="bold" text-anchor="middle">barrel bottom {_bb:.2f} vs recess floor {e.Z_RECESS:.2f} -> ~{abs(_bb-e.Z_RECESS):.1f} mm: file a touch off the ring / grind a hair</text>')
# scale bar + path
by, bx = H - MB + 26, ML
s.append(f'<line x1="{bx}" y1="{by}" x2="{bx+50}" y2="{by}" stroke="#000" stroke-width="0.6"/>')
for t in range(0, 51, 10):
    s.append(f'<line x1="{bx+t}" y1="{by-1.5}" x2="{bx+t}" y2="{by+1.5}" stroke="#000" stroke-width="0.4"/>')
s.append(f'<text x="{bx}" y="{by+5}" font-size="3" font-weight="bold">50.0 mm - CHECK WITH RULER (confirms 1:1)</text>')
s.append(f'<text x="{bx}" y="{by+10.5}" font-size="2.4" fill="#333">Path: recessed lid centre + grind ~2 mm rear rim for the barrel + file a touch off the 30 mm ring. Mirrored (behind view). From enclosure.py.</text>')
s.append('</svg>')

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w') as f:
    f.write("\n".join(s))
print(f"wrote {os.path.relpath(OUT)}  ({W:.0f}x{H:.0f} mm) | combo z={CZ:g} barrel btm={_bb:.2f} recess={e.Z_RECESS:.2f}")
