#!/usr/bin/env python3
"""gen_place.py -- first-pass placement + functional clustering on tone_trixter.kicad_pcb.

Run with KiCad's bundled python (pcbnew) AFTER 'Update PCB from Schematic', with KiCad CLOSED:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
      tools/gen_place.py [board.kicad_pcb]

Two stages:
 1. ANCHORS -- parts whose position is FIXED by the enclosure (OLED, encoder, LEDs, rear jacks,
    footswitch/battery/bond connectors, Seed, SD) + the three OPAs + Ch-B jack, placed per the
    mock-up and on the correct side (jacks + Seed on the BACK, hang below the board).
 2. CLUSTERING -- every remaining passive is assigned to the device it shares the most SIGNAL nets
    with (power/ground rails excluded) and gridded next to it: front-end A around U1, front-end B
    around U2, output around U3, Seed decoupling around U4, power section near the DC entry.

⚠ FIRST PASS -- anchor X-Y are mock-up defaults; verify sides/rotations in 3D. Re-run any time
(idempotent by reference). Tune clusters by hand after; this just gets them onto the board by group.
"""
import sys, os
from collections import defaultdict
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import enclosure as enc          # SINGLE SOURCE OF TRUTH for enclosure-coupled positions
BOARD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "tone_trixter.kicad_pcb")

OX, OY = 30.0, 30.0        # board origin on the sheet (matches gen_board.py)
BW, BD = 132.0, 57.0

# anchor: ref -> (x, y, rot, side)  board-local mm; side F=top, B=bottom(hangs below).
# ENCLOSURE-COUPLED parts (OLED, encoder, LEDs, the TRS/DC jacks) come from enclosure.py so the board
# tracks the paper template exactly -- IN on the RIGHT, OUT on the LEFT, encoder right of the screen
# (locked 2026-10-12). The rest keep the builder's hand-placement (not panel-critical).
ANCHORS = {
    **enc.panel_anchors_board(),     # J6, SW1, D4, D5, J1(IN A), J2(OUT), J8(IN B), J3(DC)
    # --- NOT enclosure-coupled: builder's board-local placement (flying leads / internal / bottom) ---
    "J7":  (50.5, 3.0,  180, "B"),   # bypass sw lead  (footswitch is panel-mounted; lead not critical)
    "J9":  (93.5, 3.0,  180, "B"),   # tuner sw lead
    "J4":  (127.5, 17.5,180, "B"),   # battery lead
    "J5":  (7.0, 12.5,  180, "B"),   # enclosure bond
    "U4":  (95.0, 26.5,  90, "B"),   # Daisy Seed3 (USB on a flexible extender -- not pinned to the panel)
    "J10": (111.7, 8.9,   0, "B"),   # microSD
    # --- OPAs: near centre for now. ⚠ with IN now on the RIGHT, drag front-end U1/U2 toward the right
    #     (near IN A/IN B) and output U3 toward the left (near OUT) when you optimise routing. ---
    "U1":  (66.0, 11.0,   0, "F"),   # OPA front-end A
    "U2":  (66.0, 20.0,   0, "F"),   # OPA front-end B
    "U3":  (66.0, 29.0,   0, "F"),   # OPA output stage
}

# Passive clusters grid in the OPEN CENTRE (between the left controls and the right Seed), grouped
# by function, so they land on the board without colliding with the builder's edge placement. Drag
# each group to its final home afterwards. (grid top-left, board-local)
CLUSTER_ORIGIN = {
    "U1":   (48.0, 8.0),    # front-end A passives
    "U2":   (48.0, 18.0),   # front-end B passives
    "U3":   (48.0, 28.0),   # output passives
    "U4":   (48.0, 38.0),   # Seed decoupling + SD resistors
    "PWR":  (76.0, 8.0),    # power section
    "MISC": (76.0, 22.0),   # unassigned (film caps, star, TPs) -- sort by hand
}

def mm(x): return pcbnew.FromMM(x)
def at(x, y): return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

def is_rail(n):
    return (not n) or n.startswith("+") or "GND" in n or n.startswith("unconnected") \
        or n in ("VIN", "VREF", "VBUS", "VCC")

def main():
    board = pcbnew.LoadBoard(BOARD)
    fps = {f.GetReference(): f for f in board.GetFootprints()}

    def set_side_pos(f, x, y, rot, side):
        if (side == "B") != f.IsFlipped():
            f.Flip(f.GetPosition(), False)
        f.SetPosition(at(x, y)); f.SetOrientationDegrees(rot)

    # --- stage 1: anchors ---
    for ref, (x, y, rot, side) in ANCHORS.items():
        if ref in fps: set_side_pos(fps[ref], x, y, rot, side)

    # signal-net sets per footprint
    sig = {r: {p.GetNetname() for p in f.Pads() if not is_rail(p.GetNetname())}
           for r, f in fps.items()}
    rails = {r: {p.GetNetname() for p in f.Pads() if is_rail(p.GetNetname())}
             for r, f in fps.items()}

    HUBS = ("U1", "U2", "U3")   # the signal hubs that attract passives
    clusters = defaultdict(list)
    for ref, f in fps.items():
        if ref in ANCHORS:
            continue
        # best signal hub
        best, score = None, 0
        for h in HUBS:
            s = len(sig[ref] & sig[h])
            if s > score: best, score = h, s
        # also let the Seed attract its direct-signal passives
        s_u4 = len(sig[ref] & sig["U4"])
        if s_u4 > score: best, score = "U4", s_u4
        if best is None:
            # rail-only part: route by dominant rail
            rs = rails[ref]
            if any("+9" in n or n == "VIN" for n in rs):      best = "PWR"
            elif any("+3V3D" in n for n in rs):               best = "U4"
            elif any("+3V3A" in n or "VREF" in n for n in rs): best = "U1"
            else:                                             best = "MISC"
        clusters["U4" if best == "U4" else best].append(ref)

    # --- stage 2: grid each cluster near its origin ---
    placed = 0
    for key, refs in clusters.items():
        ox, oy = CLUSTER_ORIGIN.get(key, CLUSTER_ORIGIN["MISC"])
        refs.sort(key=lambda r: (r[0], int("".join(c for c in r if c.isdigit()) or 0)))
        cols, pitch = 5, 3.4
        for i, ref in enumerate(refs):
            f = fps[ref]
            if f.IsFlipped(): f.Flip(f.GetPosition(), False)
            f.SetPosition(at(ox + (i % cols) * pitch, oy + (i // cols) * pitch))
            f.SetOrientationDegrees(0)
            placed += 1

    pcbnew.SaveBoard(BOARD, board)
    print(f"anchors+hubs placed: {sum(1 for r in ANCHORS if r in fps)}")
    for k in sorted(clusters):
        print(f"  cluster {k:5}: {len(clusters[k])} parts -> {' '.join(clusters[k])}")
    print(f"clustered {placed} passives")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("ERROR:", e, file=sys.stderr); raise
