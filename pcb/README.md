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

Layout comes after the enclosure mock-up (brief §1): outline ≤ 100 × 75 mm, top-face mounted, one plane,
star at the power-entry corner.
