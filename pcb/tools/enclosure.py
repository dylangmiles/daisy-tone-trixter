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
OLED_W, OLED_H = 33.0, 17.0                   # window CUT = black glass 34.44 x 18.66 minus a ~0.7 mm
                                              #   retaining lip; active area 29.42 x 14.7 shows through
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
JACK_Z   = 10.8         # barrel axis above the base (bottom-mount)
USB_Z    = 16.0         # Seed USB-C height above base -- ⚠ CONFIRM from the Daisy mount (side elevation)
REAR = [  # (board_ref, label, x_enclosure, dia_or_wh, kind, z_axis)
    ("J2",  "OUT",  24.2,  10.0,        "trs", JACK_Z),
    (None,  "USB",  46.0,  (13.0, 8.0), "usb", USB_Z),   # Seed's own USB-C, DIRECT through the rear wall
    ("J3",  "DC",   64.0,  12.0,        "dc",  JACK_Z),  # panel jack + flying lead -- x is a design choice
    ("J8",  "IN B", 90.3,  10.0,        "trs", JACK_Z),   # IN B inner
    ("J1",  "IN A", 113.8, 10.0,        "trs", JACK_Z),   # IN A outermost-right (primary guitar input)
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
