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
out/schematic.pdf               # review print (netlist.net + 1:1 board prints also in out/)
```

Check: `kicad-cli sch erc tone_trixter.kicad_sch` (only "symbol differs from library" notices expected —
the op-amp and SD symbols are flattened copies) and `kicad-cli sch export netlist`. `gen_schematic.py`
also runs a wire-through-pin check and prints `CHECK:` lines if any wire crosses a pin it should not.

## Layout (started 2026-10-09)

⚠ **`../enclosure/enclosure.py` is the SINGLE SOURCE OF TRUTH for panel-part positions** (2026-10-12;
enclosure tooling moved to a sibling `enclosure/` dir 2026-10-31). The paper template
(`../enclosure/tools/gen_template.py`) imports it, so the template and the board **cannot drift** —
change a position once, re-render, both follow. Convention locked there: **INPUT on the RIGHT,
OUTPUT on the LEFT** (signal R→L), encoder right of the centred screen.

The enclosure mock-up revised the brief's board envelope. **Current geometry** (see
`../enclosure/enclosure.py`, `../enclosure/tools/gen_template.py` + `gen_side_elevation.py`, rendered
in `../enclosure/out/`):

- 1590XX **landscape** (internal top-face plane 138 × 114, **2.33 mm walls**, internal cavity **33 mm**
  tall, Ø7 corner bosses). All measured off the box 2026-10-10.
- **Full-width board ~132 × 57 mm**, hugging the **back wall**, rear corners notched **10 × 10** for the
  bosses (Ø7 post → 3 mm play). Mounted high under the top face (**gap 8.24 mm**, set by the OLED glass
  flush + encoder body clearing); **Seed hangs ~16.5 below**; OLED + encoder reach up (screen centred on
  the board, toward the rear); footswitches panel-mounted up front at 90 mm centres.
- **TRS + DC jacks bottom-mounted** (barrel 12.4 below the board → axis **~33 % up the wall, lower
  third**, z ≈ 10.8), legs up through the PCB, isolated plastic bushings. **USB = Seed's own port** on a
  flexible extender. **Two high-Z inputs drilled in v1** — IN A (AIN1) + IN B (AIN2), both K3599 TRS;
  the XLR mic reserve is dropped (a v2 board).
- ✅ All Z-stack heights (OLED/encoder/Seed/jack/cavity) **measured** 2026-10-10; see the side-elevation.
- The mock-up is an **OUTSIDE drilling template** (tape outside, ink up, drill from outside; register by
  the box **centre** — mark the diagonals). The rear-wall strip is **mirrored** (drawn as seen from
  behind the pedal). For board-mounted jacks the most accurate method is to drill through the finished board.

**Assembly plan (decided 2026-10-15):** **0805** passives (kept over 0603 for hand-reworkability).
**JLC assembles the SMD** — keep it **single-side (F.Cu)** if it fits, prefer JLC Basic parts (small
Extended fee for the OPA1642 etc.); parts are LCSC-coded in `gen_schematic.py`. **Hand-solder the
through-hole** — jacks, encoder, Seed, footswitch/battery/DC connectors, test points. For the order,
export Gerbers + BOM + CPL from KiCad. Finish = **ENIG** (flatter pads for assembly). See brief §4.

**The outline is GENERATED** — `tools/gen_board.py` (run with KiCad's bundled python; has `pcbnew`):

```
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
    tools/gen_board.py          # writes tone_trixter.kicad_pcb: Edge.Cuts outline + M3 holes only
```

Then in KiCad: open `tone_trixter.kicad_pcb`, **Tools → Update PCB from Schematic (F8)** to pull in
the 102 footprints (the schematic has them all assigned), place per the mock-up, route, DRC, 3D-check
against the box height, Gerbers. One plane (bottom pour), star at power entry; no copper within 1 mm
of the edge; mounting holes become plane-bonded once the enclosure bond is finalised.
