#!/usr/bin/env python3
"""enclosure.py -- SINGLE SOURCE OF TRUTH for the 1590XX enclosure + panel-part positions.

Both the paper template (gen_mockup_svg.py) and the PCB placement (gen_place.py) import this, so
the template and the board CANNOT drift. Everything is in the ENCLOSURE FRAME, viewed from
ABOVE / OUTSIDE (the player looking down at the top face):

    X: 0 = LEFT wall .. 138 = RIGHT wall        (X+  = player's RIGHT)
    Y: 0 = FRONT wall .. 114 = BACK wall         (Y+  = toward the jacks)

All mm. Box measured off the real part 2026-10-10 (recalibrated caliper).

CONVENTION (locked 2026-10-12): INPUT on the RIGHT, OUTPUT on the LEFT (signal flows R->L, BOSS
convention). Encoder to the RIGHT of the centred screen (validated on paper 2026-10-10).

gen_place converts enclosure -> board-local with to_board(); the board sits 3 mm in from each side
wall with its rear edge 2 mm off the back wall.
"""

# ---- box: internal top-face plane + cavity ---------------------------------------------------
BOX_W, BOX_D = 138.0, 114.0        # internal top-face plane (measured)
WALL      = 2.33                   # wall / top-face thickness (measured)
CAVITY_H  = 33.0                   # internal cavity height (35.30 box - 2.3 top)
EXT_W, EXT_D, EXT_H = 145.0, 121.0, 39.0   # catalog nominal -- reference only; NOT used to place holes
BOSS_D, BOSS_IN = 7.0, 6.0         # O7 corner lid bosses, centre inset from each wall

# ---- board within the enclosure --------------------------------------------------------------
BRD_INSET_X = 3.0                  # board edge in from each side wall (clearance + fab tol)
BRD_Y0, BRD_Y1 = 55.0, 112.0       # front edge .. rear edge (enclosure Y; rear edge 2 mm off wall)
BRD_W, BRD_D = BOX_W - 2*BRD_INSET_X, BRD_Y1 - BRD_Y0   # 132 x 57
NOTCH = 10.0                       # rear-corner SQUARE notch for the O7 bosses (right-angle)

def to_board(x, y):
    """Enclosure (x, y) -> board-local (x, y). Board origin = board front-left corner."""
    return (x - BRD_INSET_X, y - BRD_Y0)

# ---- panel parts (ENCLOSURE frame) -----------------------------------------------------------
# Top-face controls -- validated on paper 2026-10-10. Encoder RIGHT of the centred screen.
OLED_CTR   = (BOX_W/2, (BRD_Y0 + BRD_Y1)/2)   # (69, 83.5) display-window centre; centred on the board
OLED_W, OLED_H = 35.0, 20.0                   # visible display window
ENCODER    = (108.0, OLED_CTR[1])             # right of the OLED, same depth
ENC_DIA    = 7.0
FS_CTR     = 90.0
FOOTSW     = [(BOX_W/2 - FS_CTR/2, 22.0), (BOX_W/2 + FS_CTR/2, 22.0)]   # 90 mm centres, up front (PANEL)
FS_DIA     = 12.0
LEDS       = [(55.0, 63.0), (83.0, 63.0)]     # status LEDs, just in front of the screen
LED_DIA    = 3.0

# Rear-wall holes, LEFT -> RIGHT: OUT, USB, DC, IN A, IN B  -- INPUT on the RIGHT.
JACK_Y   = 98.5         # enclosure depth where the board-mounted jack bodies sit (barrel reaches wall)
JACK_Z   = 10.8         # barrel axis above the base (bottom-mount, 12.4 drop -> ~33% up the 33 wall)
USB_Z    = 16.0         # Seed USB on a flexible extender -- free height; this is a layout choice
REAR = [  # (board_ref, label, x_enclosure, dia_or_wh, kind, z_axis)
    ("J2",  "OUT",  20.0,  10.0,        "trs", JACK_Z),
    (None,  "USB",  42.0,  (13.0, 8.0), "usb", USB_Z),   # Seed's own port, flexible extender (ref=None)
    ("J3",  "DC",   64.0,  12.0,        "dc",  JACK_Z),
    ("J1",  "IN A", 86.0,  10.0,        "trs", JACK_Z),
    ("J8",  "IN B", 112.0, 10.0,        "trs", JACK_Z),
]

def panel_anchors_board():
    """ref -> (board_x, board_y, rot, side) for enclosure-COUPLED parts, IN on the RIGHT.
    side F = top, B = bottom (hangs below). Jacks keep rot 180 / side B (barrel -> back wall)."""
    a = {
        "J6":  (*to_board(*OLED_CTR), 90, "F"),   # OLED header -- at the WINDOW centre; nudge for the
                                                  #   module's header->window offset when fitting it
        "SW1": (*to_board(*ENCODER),   0, "F"),   # encoder
        "D4":  (*to_board(*LEDS[0]),   0, "F"),   # LED
        "D5":  (*to_board(*LEDS[1]),   0, "F"),   # LED
    }
    for ref, label, x, size, kind, z in REAR:
        if ref and kind in ("trs", "dc"):
            a[ref] = (*to_board(x, JACK_Y), 180, "B")
    return a
