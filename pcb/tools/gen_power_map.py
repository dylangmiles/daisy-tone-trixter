#!/usr/bin/env python3
"""Power-domain map: where each power rail's pads live, to plan the In1.Cu split.
Bottom view (X-mirrored). Colour-blind safe: each rail has a distinct SHAPE and a
text label; colour is only a redundant cue. Overlays the GNDD (digital ground)
region so power rails can be aligned with the ground split below.
Run with KiCad's bundled python (has pcbnew). Output: out/power_domain_map.svg
"""
import pcbnew, os, collections, math
B = pcbnew.LoadBoard(os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tone_trixter.kicad_pcb"))
mm = pcbnew.ToMM

# --- board bbox (for framing + mirror) ---
bb = B.GetBoardEdgesBoundingBox()
minX, maxX = mm(bb.GetLeft()), mm(bb.GetRight())
minY, maxY = mm(bb.GetTop()), mm(bb.GetBottom())
X1 = minX + maxX  # mirror axis: xm = X1 - x

# --- rails of interest + style (shape, colour) ---
RAILS = {
 "VIN":    ("square",   "#d62728", "main input supply — entry, fans to all loads"),
 "+9VA":   ("tri_up",   "#1f77b4", "analog 9V supply (op-amps)"),
 "+3V3D":  ("tri_dn",   "#ff7f0e", "digital 3V3 (Seed/SD/OLED) — put over GNDD"),
 "+3V3A":  ("diamond",  "#17becf", "analog 3V3 (keep over AGND, by Seed)"),
 "VREF":   ("hexagon",  "#2ca02c", "analog bias ref — pour over AGND, via-stitch (driven DC, not high-Z)"),
 "P48V":   ("pentagon", "#9467bd", "phantom 48V — strip boost->XLR, HV clearance"),
 "BST_IN": ("circle",   "#8c564b", "boost input (VIN at U7) — tight cluster"),
 "BATT-":  ("cross",    "#7f7f7f", "battery return"),
}
# rail SOURCE (origin) — pour each region outward from here. Resolved LIVE from the
# board (ref, pad) so markers follow parts if they move (e.g. R1 relocated to GNDA).
SRC_REFS = {
 "VIN":    ("F1", None),   # entry fuse (diode-OR)
 "+9VA":   ("R1", None),   # RC filter off VIN
 "+3V3D":  ("U4", "38"),   # Seed pin 38
 "+3V3A":  ("U4", "21"),   # Seed pin 21
 "VREF":   ("U3", None),   # buffer output
 "P48V":   ("D6", None),   # boost output diode
 "BST_IN": ("U7", "5"),    # U7 pin 5
 "BATT-":  ("J4", None),   # battery connector
}
def _padpos(ref, num):
    f = B.FindFootprintByReference(ref)
    if not f: return None
    if num is not None:
        for p in f.Pads():
            if p.GetNumber() == num: return (mm(p.GetCenter().x), mm(p.GetCenter().y))
    return (mm(f.GetPosition().x), mm(f.GetPosition().y))
SOURCES = {n: _padpos(r, p) for n,(r,p) in SRC_REFS.items() if _padpos(r, p)}
pads = collections.defaultdict(list)
for fp in B.GetFootprints():
    for p in fp.Pads():
        n = p.GetNetname()
        if n in RAILS:
            pads[n].append((mm(p.GetCenter().x), mm(p.GetCenter().y)))

# --- GNDD zone outline (digital ground region) on In2.Cu ---
gndd = None
for z in B.Zones():
    if z.GetNetname() == "GNDD" and z.IsOnLayer(pcbnew.In2_Cu):
        ps = z.Outline(); best = -1
        for oi in range(ps.OutlineCount()):
            o = ps.Outline(oi); n = o.PointCount()
            pts = [(mm(o.CPoint(i).x), mm(o.CPoint(i).y)) for i in range(n)]
            # area
            A = abs(sum(pts[i][0]*pts[(i+1)%n][1]-pts[(i+1)%n][0]*pts[i][1] for i in range(n)))/2
            if A > best: best, gndd = A, pts

# --- transform mm -> px (mirror X) ---
S = 9.0; MX, MT, MB = 45, 80, 240
W = int((maxX-minX)*S + 2*MX)
H = int((maxY-minY)*S + MT + MB)
def TX(x): return MX + (X1 - x - minX)*S
def TY(y): return MT + (y - minY)*S

def marker(shape, cx, cy, col):
    s = 5.5
    if shape == "square":   return f'<rect x="{cx-s:.1f}" y="{cy-s:.1f}" width="{2*s:.1f}" height="{2*s:.1f}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "circle":   return f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{s:.1f}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "tri_up":   return f'<polygon points="{cx:.1f},{cy-s:.1f} {cx-s:.1f},{cy+s:.1f} {cx+s:.1f},{cy+s:.1f}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "tri_dn":   return f'<polygon points="{cx:.1f},{cy+s:.1f} {cx-s:.1f},{cy-s:.1f} {cx+s:.1f},{cy-s:.1f}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "diamond":  return f'<polygon points="{cx:.1f},{cy-s:.1f} {cx+s:.1f},{cy:.1f} {cx:.1f},{cy+s:.1f} {cx-s:.1f},{cy:.1f}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "pentagon":
        pts = " ".join(f"{cx+s*math.sin(2*math.pi*k/5):.1f},{cy-s*math.cos(2*math.pi*k/5):.1f}" for k in range(5))
        return f'<polygon points="{pts}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "hexagon":
        pts = " ".join(f"{cx+s*math.sin(2*math.pi*k/6):.1f},{cy-s*math.cos(2*math.pi*k/6):.1f}" for k in range(6))
        return f'<polygon points="{pts}" fill="{col}" stroke="#111" stroke-width="0.6"/>'
    if shape == "cross":    return (f'<line x1="{cx-s:.1f}" y1="{cy-s:.1f}" x2="{cx+s:.1f}" y2="{cy+s:.1f}" stroke="{col}" stroke-width="2.2"/>'
                                    f'<line x1="{cx-s:.1f}" y1="{cy+s:.1f}" x2="{cx+s:.1f}" y2="{cy-s:.1f}" stroke="{col}" stroke-width="2.2"/>')
    return ""

svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="sans-serif">']
svg.append(f'<rect width="{W}" height="{H}" fill="#ffffff"/>')
svg.append(f'<text x="{MX}" y="34" font-size="22" font-weight="bold">Tone Trixter — power-domain map (bottom view, X-mirrored)</text>')
svg.append(f'<text x="{MX}" y="56" font-size="13" fill="#555">Each rail = distinct SHAPE + label (not colour). Dashed grey = GNDD region. A rail symbol inside a circle = its SOURCE — pour each region outward from there.</text>')
# board frame
svg.append(f'<rect x="{TX(maxX):.1f}" y="{TY(minY):.1f}" width="{(maxX-minX)*S:.1f}" height="{(maxY-minY)*S:.1f}" fill="none" stroke="#999" stroke-width="1.2"/>')
# GNDD region
if gndd:
    pstr = " ".join(f"{TX(x):.1f},{TY(y):.1f}" for x,y in gndd)
    svg.append(f'<polygon points="{pstr}" fill="#dddddd" fill-opacity="0.45" stroke="#888" stroke-width="1" stroke-dasharray="6 4"/>')
    cx = sum(TX(x) for x,y in gndd)/len(gndd); cy = sum(TY(y) for x,y in gndd)/len(gndd)
    svg.append(f'<text x="{cx:.0f}" y="{cy:.0f}" font-size="15" font-weight="bold" fill="#666" text-anchor="middle">GNDD region</text>')
# pads + cluster labels
for n,(shape,col,desc) in RAILS.items():
    for (x,y) in pads.get(n,[]):
        svg.append(marker(shape, TX(x), TY(y), col))
    if pads.get(n):
        cx = sum(TX(x) for x,y in pads[n])/len(pads[n]); cy = sum(TY(y) for x,y in pads[n])/len(pads[n])
        svg.append(f'<text x="{cx:.0f}" y="{cy-9:.0f}" font-size="14" font-weight="bold" fill="{col}" stroke="#fff" stroke-width="3" paint-order="stroke" text-anchor="middle">{n}</text>')
# rail sources (origins) — the rail's OWN symbol with a circle around it + label
for n,(sx,sy) in SOURCES.items():
    shape, col = RAILS[n][0], RAILS[n][1]; cx, cy = TX(sx), TY(sy)
    svg.append(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="10" fill="#fff" stroke="#000" stroke-width="2.4"/>')
    svg.append(marker(shape, cx, cy, col))
    svg.append(f'<text x="{cx:.0f}" y="{cy+25:.0f}" font-size="12" font-weight="bold" fill="{col}" stroke="#fff" stroke-width="3" paint-order="stroke" text-anchor="middle">{n} src</text>')

# legend
ly = MT + (maxY-minY)*S + 34
svg.append(f'<text x="{MX}" y="{ly-8:.0f}" font-size="15" font-weight="bold">Rails (shape — rail — role):</text>')
for i,(n,(shape,col,desc)) in enumerate(RAILS.items()):
    yy = ly + 14 + i*26
    svg.append(marker(shape, MX+10, yy-4, col))
    svg.append(f'<text x="{MX+28}" y="{yy:.0f}" font-size="14"><tspan font-weight="bold" fill="{col}">{n}</tspan>  ({len(pads.get(n,[]))} pads) — {desc}</text>')
svg.append('</svg>')

out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "out", "power_domain_map.svg")
open(out,"w").write("\n".join(svg))
print("wrote", out, f"({W}x{H})")
