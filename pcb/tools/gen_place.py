#!/usr/bin/env python3
"""gen_place.py -- first-pass placement of the enclosure-constrained parts on tone_trixter.kicad_pcb.

Run with KiCad's bundled python AFTER 'Update PCB from Schematic' has imported the footprints:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
      tools/gen_place.py [board.kicad_pcb]

It positions the parts whose location is FIXED by the enclosure (OLED window, encoder, LEDs, the
rear-wall jacks, the footswitch/battery connectors, the Seed, the SD) at the mock-up coordinates and
on the correct side, and moves everything else into a tidy staging grid to the right of the board so
the board area is clear and you can drag the functional clusters in by hand.

⚠ FIRST PASS: anchor X-Y comes from gen_mockup_svg.py (still default dims); jacks + Seed are placed
on the BACK copper (they hang below the board) -- verify side/rotation (esp. Seed USB -> back wall,
and each K3599 barrel -> back wall) in the 3D view. Re-run any time; it is idempotent by reference.
"""
import sys, os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "tone_trixter.kicad_pcb")

# board origin on the sheet (must match gen_board.py), board-local 0..132 (X) x 0..57 (Y, 0=front)
OX, OY = 30.0, 30.0
BW, BD = 132.0, 57.0

# anchor: ref -> (x, y, rot_deg, side)   board-local mm; side "F"=top, "B"=bottom (hangs below)
ANCHORS = {
    # --- panel face (top), from the mock-up ---
    "J6":  (66.0, 28.5,   0, "F"),   # OLED 1.3" header (window centred, toward the rear)
    "SW1": (105.0, 28.5,  0, "F"),   # PEC11R encoder, beside the OLED
    "D4":  (52.0, 10.0,   0, "F"),   # LED (bypass/engaged)
    "D5":  (80.0, 10.0,   0, "F"),   # LED (looper/record)
    # --- rear wall, bottom-mounted jacks (barrel hangs below, points out the back) ---
    "J1":  (49.0, 50.0,   0, "B"),   # IN  (K3599)
    "J3":  (71.0, 50.0,   0, "F"),   # DC  (still a JST lead for now)
    "J2":  (115.0, 50.0,  0, "B"),   # OUT (K3599)
    # --- front-edge connectors to the panel footswitches (flying leads) ---
    "J7":  (18.0, 5.0,    0, "F"),   # bypass switch lead
    "J9":  (114.0, 5.0,   0, "F"),   # tuner switch lead
    # --- power / battery / bond connectors, rear area ---
    "J4":  (90.0, 52.0,   0, "F"),   # battery lead
    "J5":  (100.0, 52.0,  0, "F"),   # enclosure bond
    # --- the brain + card, interior ---
    "U4":  (62.0, 34.0,  90, "B"),   # Daisy Seed3, hangs below, long axis across the width;
                                     # ⚠ verify USB faces the back wall (may need 90/270 flip)
    "J10": (26.0, 40.0,   0, "F"),   # microSD (USB-drive mode; no panel slot, so interior)
}

def mm(x): return pcbnew.FromMM(x)
def at(x, y): return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))

def main():
    board = pcbnew.LoadBoard(BOARD)
    fps = {f.GetReference(): f for f in board.GetFootprints()}

    placed = []
    for ref, (x, y, rot, side) in ANCHORS.items():
        f = fps.get(ref)
        if not f:
            print(f"  !! {ref} not on board (skipped)"); continue
        on_back = f.IsFlipped()
        if side == "B" and not on_back:
            f.Flip(f.GetPosition(), False)
        elif side == "F" and on_back:
            f.Flip(f.GetPosition(), False)
        f.SetPosition(at(x, y))
        f.SetOrientationDegrees(rot)
        placed.append(ref)

    # everything else -> staging grid to the RIGHT of the board, sorted by reference
    rest = sorted((r for r in fps if r not in ANCHORS),
                  key=lambda r: (r[0], int("".join(c for c in r if c.isdigit()) or 0)))
    gx0, gy0, pitch, cols = BW + 14.0, 0.0, 6.0, 14
    for i, ref in enumerate(rest):
        col, row = i % cols, i // cols
        f = fps[ref]
        if f.IsFlipped():
            f.Flip(f.GetPosition(), False)
        f.SetPosition(at(gx0 + col * pitch, gy0 + row * pitch))
        f.SetOrientationDegrees(0)

    pcbnew.SaveBoard(BOARD, board)
    print(f"placed {len(placed)} anchors: {', '.join(placed)}")
    print(f"staged {len(rest)} other footprints in a {cols}-wide grid to the right of the board")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("ERROR:", e, file=sys.stderr); raise
