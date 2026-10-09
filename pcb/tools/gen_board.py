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
OUT  = os.path.join(HERE, "..", "tone_trixter.kicad_pcb")

# ---- board outline (board-local mm) ----------------------------------------------------------
W, D   = 132.0, 57.0      # width (X), depth (Y)
NW, ND = 12.0, 12.0       # rear-corner notch (clears the O7 bosses)
ORIGIN = (30.0, 30.0)     # offset on the sheet so the board sits in the positive quadrant

# notched rectangle, Y=0 front .. Y=D rear; two rear corners bitten out
OUTLINE = [
    (0, 0), (W, 0),
    (W, D-ND), (W-NW, D-ND), (W-NW, D),
    (NW, D), (NW, D-ND), (0, D-ND),
]
M3 = [(6, 6), (W-6, 6), (26, D-6), (W-26, D-6)]   # front corners + rear-inboard (clear of notches)
M3_DRILL = 3.2

def mm(x): return pcbnew.FromMM(x)
def V(x, y): return pcbnew.VECTOR2I(mm(ORIGIN[0]+x), mm(ORIGIN[1]+y))

def main():
    board = pcbnew.CreateEmptyBoard()

    # 1.6 mm, 2 layers (one plane = a bottom pour; refine later)
    board.GetDesignSettings().SetBoardThickness(mm(1.6))

    # --- Edge.Cuts outline ---
    pts = OUTLINE + [OUTLINE[0]]
    for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
        seg = pcbnew.PCB_SHAPE(board)
        seg.SetShape(pcbnew.SHAPE_T_SEGMENT)
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetStart(V(x1, y1))
        seg.SetEnd(V(x2, y2))
        seg.SetWidth(mm(0.1))
        board.Add(seg)

    # --- M3 mounting holes as Edge.Cuts circles (non-plated cutouts for now; plane-bonded
    #     MountingHole footprints come later once the enclosure bond is finalised) ---
    for (x, y) in M3:
        c = pcbnew.PCB_SHAPE(board)
        c.SetShape(pcbnew.SHAPE_T_CIRCLE)
        c.SetLayer(pcbnew.Edge_Cuts)
        c.SetCenter(V(x, y))
        c.SetEnd(V(x + M3_DRILL/2.0, y))   # radius point
        c.SetWidth(mm(0.1))
        board.Add(c)

    # --- a couple of labels on the User.Comments layer for orientation ---
    for (x, y, txt) in [(W/2, -4, "FRONT (footswitches, toward player)"),
                        (W/2, D+5, "REAR WALL -- bottom-mounted jacks (XLR/IN/DC/USB/OUT)")]:
        t = pcbnew.PCB_TEXT(board)
        t.SetLayer(pcbnew.Cmts_User)
        t.SetText(txt)
        t.SetPosition(V(x, y))
        t.SetTextSize(pcbnew.VECTOR2I(mm(2.0), mm(2.0)))
        board.Add(t)

    pcbnew.SaveBoard(OUT, board)
    print(f"wrote {os.path.relpath(OUT)}  ({W:g} x {D:g} mm outline, {len(M3)} M3 holes)")

if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print("ERROR:", e, file=sys.stderr); raise
