# Tone Trixter PCB — rev A

KiCad 10 project. **The schematic is GENERATED**: edit `tools/gen_schematic.py`, run it, never hand-edit
`tone_trixter.kicad_sch` (the next run overwrites it). Design intent and every value's reason are in
`private/docs/daisy_pcb_brief_2026-09-16.md`; pin numbers come from `../board.h`.

```
tools/gen_schematic.py          # the source of truth for the schematic
tools/kisch.py, tools/kisym.py  # tiny writer + KiCad-library symbol extractor
tone_trixter.kicad_sch          # generated
tone_trixter.kicad_pro          # project (created once)
TT.kicad_sym, sym-lib-table     # the Daisy Seed3 symbol, built from the pinout CSV
tone_trixter_schematic.pdf      # review print
```

Check: `kicad-cli sch erc tone_trixter.kicad_sch` (only "symbol differs from library" notices expected —
the op-amp and SD symbols are flattened copies) and `kicad-cli sch export netlist`. `gen_schematic.py`
also runs a wire-through-pin check and prints `CHECK:` lines if any wire crosses a pin it should not.

## Layout (started 2026-10-09)

The enclosure mock-up revised the brief's board envelope. **Current geometry** (see
`tools/gen_mockup_svg.py` + `tools/gen_side_elevation_svg.py`, rendered in `mockup/`):

- 1590XX **landscape** (internal top-face plane 138 × 114, 2 mm walls, Ø7 corner bosses).
- **Full-width board ~132 × 57 mm**, hugging the **back wall**, rear corners notched 12 × 12 for the
  bosses. Mounted high under the top face; **Seed hangs below**; OLED + encoder reach up (screen
  centred on the board, toward the rear); footswitches panel-mounted up front at 90 mm centres.
- **TRS + DC jacks bottom-mounted** (barrel below the board → axis mid-wall), legs up through the PCB,
  isolated plastic bushings. **USB = Seed's own port** oriented to the back wall. **XLR mic input
  reserved** next to IN (v2, not drilled).
- ⚠ heights (OLED/encoder/Seed/jack) still DEFAULT pending the paper test-fit + real part measurements.

**The outline is GENERATED** — `tools/gen_board.py` (run with KiCad's bundled python; has `pcbnew`):

```
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
    tools/gen_board.py          # writes tone_trixter.kicad_pcb: Edge.Cuts outline + M3 holes only
```

Then in KiCad: open `tone_trixter.kicad_pcb`, **Tools → Update PCB from Schematic (F8)** to pull in
the 102 footprints (the schematic has them all assigned), place per the mock-up, route, DRC, 3D-check
against the box height, Gerbers. One plane (bottom pour), star at power entry; no copper within 1 mm
of the edge; mounting holes become plane-bonded once the enclosure bond is finalised.
