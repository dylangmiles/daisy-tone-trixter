#!/usr/bin/env python3
"""gen_board.py -- create tone_trixter.kicad_pcb with the board OUTLINE from the enclosure mock-up.

Run with KiCad's bundled python (has pcbnew):
  /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
      tools/gen_board.py

Writes the Edge.Cuts outline + M3 mounting holes only. Footprints are pulled in from the schematic
inside KiCad (Tools -> Update PCB from Schematic / F8) -- the schematic is the source of truth and
already has all 102 footprints assigned.

Geometry (board-local mm, from gen_mockup_svg.py, landscape 1590XX):
  full-width board 132 (X, left-right) x 57 (Y, front->rear) deep, hugging the back wall;
  rear corners notched 12 x 12 to clear the O7 corner lid bosses;
  Y=0 = front edge (toward player), Y=57 = rear edge (back wall, bottom-mounted jacks).
⚠ Dimensions still DEFAULT pending the paper test-fit; re-run after any change.
"""
import os, sys
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
OUT  = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "tone_trixter.kicad_pcb")

# ---- board outline (board-local mm) ----------------------------------------------------------
W, D   = 132.0, 57.0      # width (X), depth (Y)
NW, ND = 10.0, 10.0       # rear-corner notch: O7 screw post -> 3 mm play; smaller notch = more rear-wall edge
THICK  = 1.5              # PCB thickness (mm) -- builder spec 2026-10-10
ORIGIN = (30.0, 30.0)     # offset on the sheet so the board sits in the positive quadrant

# notched rectangle, Y=0 front .. Y=D rear; two rear corners bitten out
OUTLINE = [
    (0, 0), (W, 0),
    (W, D-ND), (W-NW, D-ND), (W-NW, D),
    (NW, D), (NW, D-ND), (0, D-ND),
]
# No mounting holes for now -- the board is registered by the bottom-mounted jacks.

def mm(x): return pcbnew.FromMM(x)
def V(x, y): return pcbnew.VECTOR2I(mm(ORIGIN[0]+x), mm(ORIGIN[1]+y))

def add_outline(board):
    """Draw the Edge.Cuts outline. Caller has already cleared any existing Edge.Cuts shapes."""
    pts = OUTLINE + [OUTLINE[0]]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetStart(V(x1, y1))
        seg.SetEnd(V(x2, y2))
        seg.SetWidth(mm(0.1))
        board.Add(seg)

def main():
    if os.path.exists(OUT):
        # UPDATE IN PLACE: replace only the Edge.Cuts outline, keep footprints/tracks/zones/text.
        board = pcbnew.LoadBoard(OUT)
        board.GetDesignSettings().SetBoardThickness(mm(THICK))
        removed = 0
        for d in list(board.GetDrawings()):
            if d.GetLayer() == pcbnew.Edge_Cuts:
                board.Remove(d); removed += 1
        add_outline(board)
        pcbnew.SaveBoard(OUT, board)
        print(f"updated outline in {os.path.relpath(OUT)}  ({W:g} x {D:g}, notch {NW:g}x{ND:g}, no mounting holes); "
              f"removed {removed} old Edge.Cuts shapes, footprints preserved")
    else:
        board = pcbnew.CreateEmptyBoard()
        board.GetDesignSettings().SetBoardThickness(mm(THICK))
        add_outline(board)
        for (x, y, txt) in [(W/2, -4, "FRONT (footswitches, toward player)"),
                            (W/2, D+5, "REAR WALL -- bottom-mounted jacks (XLR/IN/DC/OUT)")]:
            t = pcbnew.PCB_TEXT(board)
            t.SetLayer(pcbnew.Cmts_User)
            t.SetText(txt)
            t.SetPosition(V(x, y))
            t.SetTextSize(pcbnew.VECTOR2I(mm(2.0), mm(2.0)))
            board.Add(t)
        pcbnew.SaveBoard(OUT, board)
        print(f"wrote fresh {os.path.relpath(OUT)}  ({W:g} x {D:g} outline, notch {NW:g}x{ND:g}, no mounting holes)")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("ERROR:", e, file=sys.stderr); raise
