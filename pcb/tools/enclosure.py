#!/usr/bin/env python3
"""enclosure.py -- SINGLE SOURCE OF TRUTH for the 1590XX enclosure + panel-part positions.

The paper template (gen_mockup_svg.py) imports this. Everything is in the ENCLOSURE FRAME, viewed
from ABOVE / OUTSIDE (the player looking down at the top face):

    X: 0 = LEFT wall .. 138 = RIGHT wall        (X+  = player's RIGHT)
    Y: 0 = FRONT wall .. 114 = BACK wall         (Y+  = toward the jacks)

All mm. Box measured off the real part 2026-10-10 (recalibrated caliper).

CONVENTION (locked 2026-10-12): INPUT on the RIGHT, OUTPUT on the LEFT (signal flows R->L, BOSS
convention). Encoder to the RIGHT of the centred screen.

⚠ PANEL POSITIONS SYNCED TO THE HAND-TUNED BOARD 2026-10-27.
The KiCad board (tone_trixter.kicad_pcb) is X-MIRRORED relative to this enclosure frame, and its
panel parts were hand-tuned after auto-placement. The mapping between the committed board (KiCad mm)
and this enclosure frame is:
        enclosure_x = 165 - kicad_x         kicad_x = 165 - enclosure_x
        enclosure_y = kicad_y + 25          kicad_y = enclosure_y - 25
The panel values below were derived from the board through that mapping, so the template matches what
is actually fabricated.
⚠ gen_place.py / to_board() / panel_anchors_board() are SUPERSEDED: they reproduce the ORIGINAL
auto-placement (pre-mirror, pre-hand-tuning), NOT the current board. Do NOT re-run gen_place on the
committed board -- the board is now the source of truth for part positions.
"""

# ---- box: internal top-face plane + cavity ---------------------------------------------------
BOX_W, BOX_D = 138.0, 114.0        # internal top-face plane (measured)
WALL_TOP  = 2.33                   # TOP-FACE thickness (the show face carrying the holes)
WALL      = WALL_TOP               # back-compat alias (side elevation T_TOP uses e.WALL)
WALL_FR   = 2.5                    # FRONT & REAR wall thickness (measured 2026-10-27)
WALL_SIDE = 2.3                    # LEFT & RIGHT wall thickness (measured 2026-10-27)
CAVITY_H  = 33.0                   # internal cavity height (35.30 box - 2.3 top)
BOTTOM_LID = 4.23                  # base-plate / bottom-lid thickness (measured 2026-10-27)
# External top-face outline = internal plane + the REAL walls (NOT catalog nominal), and the mock-up
# draws it CENTRED on the box centre so it matches the box when registered by the centre crosshair.
EXT_W = BOX_W + 2*WALL_SIDE        # 142.6  (was catalog 145)
EXT_D = BOX_D + 2*WALL_FR          # 119.0  (was catalog 121)
EXT_H = BOTTOM_LID + CAVITY_H + WALL_TOP   # ~39.6 (lid + cavity + top face); reference only
BOSS_D, BOSS_IN = 7.0, 6.0         # O7 corner lid bosses, centre inset from each wall

# ---- board within the enclosure --------------------------------------------------------------
BRD_INSET_X = 3.0                  # board edge in from each side wall (clearance + fab tol)
BRD_Y0, BRD_Y1 = 45.0, 112.0       # front edge .. rear edge (enclosure Y). Front extended to 45 for
                                   #   the Daisy (was 55); rear edge 2 mm off the back wall.
BRD_W, BRD_D = BOX_W - 2*BRD_INSET_X, BRD_Y1 - BRD_Y0   # 132 x 67
NOTCH = 10.0                       # rear-corner SQUARE notch for the O7 bosses (right-angle)

def to_board(x, y):
    """⚠ SUPERSEDED (see header). Enclosure (x, y) -> ORIGINAL board-local (pre-mirror)."""
    return (x - BRD_INSET_X, y - BRD_Y0)

# ---- panel parts (ENCLOSURE frame, synced to the board 2026-10-27) ---------------------------
# ⚠ OLED is now TOP-FACE MOUNTED on a Dupont cable (decided 2026-10-28), NOT board-registered:
#   the screen sticks to the top-face inner surface (foam tape) with its glass flush to the window,
#   and J6 is a 1x4 I2C header on the board linked by a short flat cable. So the window is placed on
#   the top face in its own right; the old header->window offset no longer applies.
OLED_CTR    = (69.0, 88.0)                    # OLED window centre on the top face (free placement)
OLED_W, OLED_H = 33.0, 17.0                   # window CUT -- sits BETWEEN the active area (shows through) and the
                                              #   flat black "nice" glass (lip all round):
                                              #   active 29.42 x 14.7  <  WINDOW 33 x 17  <  nice glass 35.58 x 18.66
                                              #   -> ~1.8/1.2 mm black border over active; ~1.3/0.8 mm lip onto glass.
# ⚠ OLED mechanicals MEASURED off the CURRENT 7-PIN SPI module 2026-10-29 (re-verify on the 4-pin I2C build part;
#   glass + active transfer, but perf size / header offsets / thickness will differ):
#   module(perf) 35.58w x 33.61h (datasheet rounds to 36x34); whole glass 35.58 x 22.93; flat black "nice" viewable
#   glass 35.58 x 18.66; active area 29.42 x 14.7 (STD 1.3in SH1106 -- NOT on the Joy-IT/SH1106 sheets, confirm later).
#   perf-top->glass-top 5.2; perf-top->hdr-row-ctr 2.0; side->nearest hdr-pin-ctr 10.16 (7-pin hdr; 4-pin differs).
#   DEPTH: glass 1.83 proud of perf-top; module 3.10 thick (4.12 incl. lowest discrete) -> clears the 8.24 mm
#   board->top-face gap (STANDOFF) with room. PCB ghost keep-clear 36x34 still covers the measured module.
OLED_HEADER = (69.0, 67.0)                    # J6 1x4 header, just FWD of the window (cable folds flat
                                              #   rearward to the screen). ⚠ board is source of truth.
OLED_HDR_W, OLED_HDR_H = 10.0, 2.6            # 1x4 right-angle header strip (4 x 2.54), for the ghost
ENCODER    = (108.0, 88.0)                    # encoder SHAFT centre. Board SW1 footprint ORIGIN is a
                                              #   corner pin (64.5,65.5); the real shaft/bushing is the
                                              #   F.Fab circle at board (57.0,63.0) -> (165-57, 63+25).
ENC_DIA    = 7.0
FS_CTR     = 90.0
FOOTSW     = [(BOX_W/2 - FS_CTR/2, 22.0), (BOX_W/2 + FS_CTR/2, 22.0)]   # 90 mm centres, up front (PANEL)
FS_DIA     = 12.0
LEDS       = [(55.0, 56.5), (83.0, 56.5)]     # status LED lens centres (board D4/D5), fwd of the screen
LED_DIA    = 3.0

# Rear-wall holes, LEFT -> RIGHT: OUT, USB, DC, IN B, IN A  -- INPUT on the RIGHT.
JACK_Y   = 102.9        # enclosure depth where the board-mounted jack bodies sit (barrel reaches wall)
JACK_Z   = 9.31         # barrel axis above the base (bottom-mount). MEASURED 2026-10-29: K3599 barrel
                        #   centre to board plane = 13.85 below the board bottom (Z_BRD_BOT 23.16) -> 9.31.
                        #   (Was 10.8; 13.85 supersedes the earlier inferred 13.9 and the stale 12.39.)
# USB is now a PANEL-MOUNT extension (Micro Robotics PAN-USBCC), NOT the Seed's own port (decided 2026-10-29):
#   the Seed sits crosswise on the board UNDERSIDE with its USB-C facing player-LEFT; a 150 mm flexible lead
#   loops from that port to this nut-mount USB-C socket on the rear wall. So this hole is a free choice on the
#   panel; it stays at the old x=46 (builder's call -- "exit where the existing USB exits"). Mechanicals
#   (PAN-USBCC datasheet): drilled hole O12.0, front bezel O13.9 x 1.8 proud, barrel O11.6 x 16.9 deep behind
#   the wall + locking nut (A/F ~14). Lead: 6x AWG22, USB 2.0 + CC -> DFU + PD both work.
USB_BEZEL_OD = 13.9     # finished outer bezel (keep-clear vs neighbours); the DRILLED hole is 12.0
USB_Z    = 16.0         # panel-hole centre above base -- sits just UNDER the board (board top 24.76, bottom
                        #   23.16; barrel O11.6 tops out ~21.8) so the 16.9 mm barrel runs in the under-board
                        #   space between the hanging jack barrels, rearward of the Seed (Y<=95). ~1.4 mm under
                        #   the PCB -- revisit if the board height drops. Bezels clear OUT (22.4) + DC (18.7).
# IN B is now the Neutrik NCJ9FI-H COMBO (XLR + switching 1/4in) -- a big Neutrik D / Combo-A cut-out,
# NOT a small TRS hole (2026-10-30).
COMBO_D = 24.0          # main connector hole O24 (standard Neutrik D / Combo-A cut-out).
COMBO_Z = 17.0          # ⚠ PROVISIONAL barrel-axis height: deliberately higher than the TRS jacks (9.31)
                        #   so the O24 clears the 33 mm wall. Set precisely from the NCJ9FI-H jack-axis-
                        #   above-PCB + the board Z, then confirm on the real part. Plus 2x M3 mounting
                        #   holes -- MARK THOSE FROM THE CONNECTOR (spacing per the NCJ9FI-H datasheet).
REAR = [  # (board_ref, label, x_enclosure, dia_or_wh, kind, z_axis)
    ("J2",  "OUT",  24.2,  10.0,        "trs",  JACK_Z),
    (None,  "USB",  46.0,  12.0,        "usbc", USB_Z),  # PAN-USBCC panel socket, O12 nut-mount; Seed USB via lead
    ("J3",  "DC",   64.0,  12.0,        "dc",   JACK_Z),  # panel jack + flying lead -- x is a design choice
    ("J8",  "IN B", 88.0,  COMBO_D,     "combo",COMBO_Z), # IN B = NCJ9FI-H combo (O24 + 2x M3, mark from part); x reverse-synced from the board (combo centre kicad 77.0, hard against IN A) 2026-10-30
    ("J1",  "IN A", 113.8, 10.0,        "trs",  JACK_Z),   # IN A outermost-right (primary guitar input)
]

# LEFT wall holes (X=0 wall), viewed from outside. HP jack on a 3.5 mm panel socket + flying lead to J11.
# Toward the REAR but FWD of the OUT jack body so it is clear in front of it.
HP_Z = 16.5             # ⚠ mid-wall; verify internal clearance (board bottom ~23, under-board barrels/Seed)
LEFT = [  # (board_ref, label, y_enclosure, dia, kind, z_axis)
    ("J11", "HP", 96.0, 6.5, "hp", HP_Z),   # y=96 is FWD of the OUT body (JACK_Y 102.9); 3.5 mm -> O6.5
]

def panel_anchors_board():
    """⚠ SUPERSEDED (see header). Returns the ORIGINAL auto-placement, NOT the hand-tuned board.
    Kept only so gen_place.py still imports; do not use it to reposition the committed board."""
    a = {
        "J6":  (*to_board(*OLED_HEADER), 90, "F"),   # OLED header
        "SW1": (*to_board(*ENCODER),      0, "F"),   # encoder
        "D4":  (*to_board(*LEDS[0]),      0, "F"),   # LED
        "D5":  (*to_board(*LEDS[1]),      0, "F"),   # LED
    }
    for ref, label, x, size, kind, z in REAR:
        if ref and kind in ("trs", "dc"):
            a[ref] = (*to_board(x, JACK_Y), 180, "B")
    return a
