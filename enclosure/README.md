# Tone Trixter — enclosure

1590XX landscape box. **`enclosure.py` is the single source of truth** for the box + panel-part
positions, shared with the PCB (the board placement in `../pcb/` is made to match it, so the template
and the board cannot drift). Change a position once and re-render.

Convention locked here: **INPUT on the RIGHT, OUTPUT on the LEFT** (signal R→L), encoder right of the
centred screen.

## Tools (`tools/`)
- `gen_template.py` — 1:1 **OUTSIDE drilling template** (box net: top face + rear-wall fold-up flap). → `out/drilling_template.svg`
- `gen_side_elevation.py` — 1:1 **side elevation** (Z-stack of OLED/encoder/Seed/jack/cavity). → `out/side_elevation.svg`
- `gen_place.py` — ⚠ **dead** (old auto-placer; board placement is hand-tuned in pcbnew). Needs KiCad's `pcbnew`.

## Regenerate
```sh
python3 tools/gen_template.py
python3 tools/gen_side_elevation.py
# SVG -> PDF (1:1) + PNG preview:
rsvg-convert -f pdf out/drilling_template.svg -o out/drilling_template.pdf
rsvg-convert -f png -w 2000 -b white out/drilling_template.svg -o out/drilling_template_preview.png
rsvg-convert -f pdf out/side_elevation.svg -o out/side_elevation.pdf
rsvg-convert -f png -w 1800 -b white out/side_elevation.svg -o out/side_elevation_preview.png
```
⚠ Print the template at **100% / actual size** and confirm the 100 mm ruler reads 100.0 before drilling.

## Outputs (`out/`)
- `drilling_template.{svg,pdf,png}` — the panel drilling template (tape outside, ink up, drill from outside; register by the box centre — mark the diagonals).
- `side_elevation.{svg,pdf,png}` — the Z-stack side view.
