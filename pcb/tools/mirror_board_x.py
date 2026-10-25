#!/usr/bin/env python3
"""mirror_board_x.py -- ONE-TIME: reflect the whole board left-right about its vertical centre.

The placement came out as a left-right MIRROR of the validated paper panel template: in the 3D
top view the encoder/IN sit on the player's LEFT, but the panel (enclosure.py / gen_mockup_svg.py,
validated against the real box) has them on the RIGHT. Root cause: gen_place.py placed parts with no
Y-flip while gen_mockup_svg.py applies one, so the board is the panel's mirror image.

This reflects every footprint's X about the board-edge centre and mirrors its rotation, so the layout
flips to the correct hand while:
  - KEEPING each part on its current copper side (F stays F, B stays B),
  - NOT mirror-imaging the footprints (each stays correctly built; pad->net is untouched),
  - preserving all relative clustering (a reflection is linear).
Electrically a no-op (no net changes). There is no routing/zones yet, so nothing else to mirror.
Panel-coupled parts land ~correct; re-run the fixed gen_place anchor stage to snap them exactly.

Run with KiCad's bundled python, KiCad CLOSED:
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
      tools/mirror_board_x.py [board.kicad_pcb]
"""
import sys, os
import pcbnew

BOARD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "tone_trixter.kicad_pcb")

WATCH = ("SW1", "J6", "J1", "J2", "J8", "U1", "U3", "U4")   # report these before/after

def main():
    board = pcbnew.LoadBoard(BOARD)

    if board.GetTracks() or board.Zones():
        print("!! board has tracks/zones -- this script only mirrors footprints; aborting", file=sys.stderr)
        sys.exit(1)

    bb = board.GetBoardEdgesBoundingBox()
    cx = bb.GetX() + bb.GetWidth() // 2          # vertical centre, internal units (nm)
    def mx(x): return 2 * cx - x

    before = {}
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    for r in WATCH:
        if r in fps:
            before[r] = (pcbnew.ToMM(fps[r].GetPosition().x), fps[r].GetOrientationDegrees(),
                         "B" if fps[r].IsFlipped() else "F")

    n = 0
    for f in board.GetFootprints():
        p = f.GetPosition()
        f.SetPosition(pcbnew.VECTOR2I(mx(p.x), p.y))
        f.SetOrientationDegrees((180.0 - f.GetOrientationDegrees()) % 360.0)
        n += 1

    pcbnew.SaveBoard(BOARD, board)
    print(f"mirrored {n} footprints about x = {pcbnew.ToMM(cx):.2f} mm (sides kept, nets untouched)\n")
    print(f"{'ref':5} {'X before':>9} -> {'X after':>9}   {'rot b->a':>12}   side")
    for r in WATCH:
        if r in fps:
            bx, brot, side = before[r]
            print(f"{r:5} {bx:9.2f} -> {pcbnew.ToMM(fps[r].GetPosition().x):9.2f}   "
                  f"{brot:5.0f} -> {fps[r].GetOrientationDegrees():5.0f}   {side}")

if __name__ == "__main__":
    main()
