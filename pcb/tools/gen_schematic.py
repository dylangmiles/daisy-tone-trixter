#!/usr/bin/env python3
"""
Tone Trixter PCB rev A — schematic generator.

Drawn from private/docs/daisy_pcb_brief_2026-09-16.md §3 (front end = Seed3 datasheet Fig 3.3 with
rev D's Vref; output = Fig 3.6 at unity; parallel diode-OR power; one plane with a NetTie star; I²C
series R + pull-ups; SD socket on the SDMMC pins). Pin numbers come from daisy/board.h.

Output: ../tone_trixter.kicad_sch  (KiCad 8 format; opens in KiCad 10). Re-run after any change —
the .kicad_sch is generated, never hand-edited. Check: kicad-cli sch erc / export netlist.
"""
import pathlib, csv, re, sys
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from kisch import Sch, Sym, U
import kisym

HERE = pathlib.Path(__file__).resolve().parent
OUT  = HERE.parent / "tone_trixter.kicad_sch"
CSV  = HERE / "../../../private/docs/reference/Seed3_pinout.csv"

# ---------------------------------------------------------------- footprints (JLC-assemblable)
FP = {
    "R":     "Resistor_SMD:R_0805_2012Metric",
    "C":     "Capacitor_SMD:C_0805_2012Metric",
    "Cfilm": "Capacitor_THT:C_Rect_L7.2mm_W7.2mm_P5.00mm_FKS2_FKP2_MKS2_MKP2",   # Wima MKS2 10u/50V
    "Cel":   "Capacitor_THT:CP_Radial_D6.3mm_P2.50mm",
    "OPA":   "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "SMA":   "Diode_SMD:D_SMA",
    "PF":    "Fuse:Fuse_1812_4532Metric",
    "LED":   "LED_THT:LED_D3.0mm",
    "JST2":  "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
    "JST3":  "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
    "HDR7":  "Connector_PinSocket_2.54mm:PinSocket_1x07_P2.54mm_Vertical",
    "HDR20": "Connector_PinSocket_2.54mm:PinSocket_1x20_P2.54mm_Vertical",
    "ENC":   "Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm_CircularMountingHoles",
    "SD":    "Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5",
    "TP":    "TestPoint:TestPoint_Pad_D1.5mm",
    "NT":    "NetTie:NetTie-2_SMD_Pad0.5mm",
    "JP":    "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
}

# ---------------------------------------------------------------- Seed3 symbol from the pinout CSV
def seed3_symbol():
    rows = list(csv.DictReader(open(CSV)))
    names = {}
    for r in rows:
        n = int(r["PINOUT"]); dn = r["DAISY PIN NAME*"].replace('"', '').replace(", ", "/")
        fn = r["PRIMARY FUNCTION"]
        if dn == "NC": nm = fn.replace("AUDIO IN L", "AUDIO_IN_1").replace("AUDIO IN R", "AUDIO_IN_2") \
                              .replace("AUDIO OUT L", "AUDIO_OUT_1").replace("AUDIO OUT R", "AUDIO_OUT_2")
        else: nm = dn
        names[n] = nm.replace(" ", "")
    names[40] = "DGND"
    # extra hints from board.h
    hint = {2: "D1/SD_DAT3", 3: "D2/SD_DAT2", 4: "D3/SD_DAT1", 5: "D4/SD_DAT0", 6: "D5/SD_CMD", 7: "D6/SD_CLK",
            12: "D11/SCL", 13: "D12/SDA", 36: "D29/USB_D-", 37: "D30/USB_D+"}
    names.update(hint)
    pins = []
    W, H = 25.4, 27.94   # half width, half height (fits 20 pins per side at 2.54)
    for n in range(1, 41):
        side_a = n <= 20
        i = (n - 1) if side_a else (40 - n)          # top→bottom on A; bottom→top on B (matches the header)
        y = round(H - 2.54 - i * 2.54, 2)
        x = round(-W - 2.54 if side_a else W + 2.54, 2)
        rot = 0 if side_a else 180
        kind = "power_in" if names[n] in ("VIN", "AGND", "DGND") else ("power_out" if names[n].startswith("+3V3") else
               ("input" if "AUDIO_IN" in names[n] else ("output" if "AUDIO_OUT" in names[n] else "bidirectional")))
        pins.append(f'''      (pin {kind} line (at {x} {y} {rot}) (length 2.54)
        (name "{names[n]}" (effects (font (size 1.016 1.016))))
        (number "{n}" (effects (font (size 1.016 1.016)))))''')
    body = f'''(symbol "TT:Daisy_Seed3"
    (pin_names (offset 0.762)) (exclude_from_sim no) (in_bom yes) (on_board yes)
    (property "Reference" "U" (at 0 {H+1.27} 0) (effects (font (size 1.27 1.27))))
    (property "Value" "Daisy_Seed3" (at 0 {-H-1.27} 0) (effects (font (size 1.27 1.27))))
    (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (property "Datasheet" "" (at 0 0 0) (effects (font (size 1.27 1.27)) (hide yes)))
    (symbol "Daisy_Seed3_0_1"
      (rectangle (start {-W} {H}) (end {W} {-H}) (stroke (width 0.254) (type default)) (fill (type background))))
    (symbol "Daisy_Seed3_1_1"
{chr(10).join(pins)}
    )
  )'''
    return body

# monkey-patch the library lookup so Sym() can see the custom symbol
_seed_blk = seed3_symbol()
_orig_sym, _orig_pins = kisym.lib_symbol, kisym.lib_pins
def _lib_symbol(lib, name):
    return _seed_blk if lib == "TT" else _orig_sym(lib, name)
def _lib_pins(lib, name):
    if lib != "TT": return _orig_pins(lib, name)
    return [(m.group(5), m.group(4), float(m.group(1)), float(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'\(pin \w+ line \(at ([-\d.]+) ([-\d.]+) (\d+)\).*?\(name "([^"]+)".*?\(number "([^"]+)"', _seed_blk, re.S)]
kisym.lib_symbol, kisym.lib_pins = _lib_symbol, _lib_pins
import kisch; kisch.lib_symbol, kisch.lib_pins = _lib_symbol, _lib_pins

# ---------------------------------------------------------------- helpers
S = Sch("tone_trixter", paper="A2")
G = 2.54
def g(x, y): return (round(x * G, 4), round(y * G, 4))       # grid units → mm

refs = {"D": 3}     # D1–D3 are the power diodes, named explicitly; LEDs continue from D4
def ref(prefix):
    refs[prefix] = refs.get(prefix, 0) + 1
    return f"{prefix}{refs[prefix]}"

def R(value, at, rot=0, fp="R", dnp=False, **f):
    s = S.add(Sym("Device", "R", ref("R"), value, at, rot, footprint=FP[fp], fields=f)); s.dnp = dnp; return s
def C(value, at, rot=0, fp="C", polar=False, **f):
    return S.add(Sym("Device", "C_Polarized" if polar else "C", ref("C"), value, at, rot, footprint=FP[fp], fields=f))
def gnd(at, net="GNDA"):
    return S.power(net, at, 0)
def rail(net, at):     # +9VA / +3V3D / VIN as power symbols pointing up; VREF as a global label
    if net == "VREF":      # driven by an op-amp OUTPUT, so a power symbol (power_in) would trip ERC
        S.glabel("VREF", at, 90); return None
    lib_name = {"+3V3D": "+3V3", "VIN": "VDC", "+9VA": "+9V", "+3V3A": "+3V3"}[net]
    s = Sym("power", lib_name, "#PWR", net, at, 0); s.value = net
    S.add(s); return s
def tp(name, at):
    s = S.add(Sym("Connector", "TestPoint", ref("TP"), name, at, 0, footprint=FP["TP"])); return s

def opamp_half(refname, unit, at, rot=0):
    return S.add(Sym("Amplifier_Operational", "OPA2134", refname, "OPA1642AIDR", at, rot, unit=unit, footprint=FP["OPA"],
                     fields={"LCSC": "C126004", "Note": "TI OPA1642 SoundPlus JFET dual; Fig 3.3/3.6 topology"}))
def opamp_power(refname, at):
    return S.add(Sym("Amplifier_Operational", "OPA2134", refname, "OPA1642AIDR", at, 0, unit=3, footprint=FP["OPA"]))

# ================================================================ 1. POWER (top-left)
S.text("POWER — parallel diode-OR (pinmap §4n), no regulator. VIN abs max 17 V: 12 V adapters yes, 18 V no.", g(4, 3), 1.8)
# DC jack lead (panel jack → JST 2-pin): pin1 = centre (−, Boss convention: sleeve +), pin2 = sleeve
j_dc = S.add(Sym("Connector", "Conn_01x02_Pin", "J3", "DC 2.1mm lead", g(6, 8), 0, footprint=FP["JST2"], fields={"Note": "panel DC jack, centre-negative: pin1 +(sleeve) pin2 −(centre)"}))
f1   = S.add(Sym("Device", "Polyfuse", "F1", "500mA", g(14, 7), 90, footprint=FP["PF"], fields={"LCSC": "C883116", "Note": "1812 PTC 0.5 A hold"}))
d1   = S.add(Sym("Device", "D_Schottky", "D1", "SS34", g(22, 7), 180, footprint=FP["SMA"], fields={"LCSC": "C8678", "Note": "= 1N5822 role: adapter OR"}))
j_bt = S.add(Sym("Connector", "Conn_01x02_Pin", "J4", "Battery lead", g(6, 14), 0, footprint=FP["JST2"], fields={"Note": "Beston 9 V pack: pin1 +, pin2 − (goes to OUT ring, not to GND)"}))
d2   = S.add(Sym("Device", "D_Schottky", "D2", "SS34", g(22, 13), 180, footprint=FP["SMA"], fields={"LCSC": "C8678", "Note": "battery OR"}))
d3   = S.add(Sym("Device", "D_TVS", "D3", "SMAJ15A", g(30, 10), 90, footprint=FP["SMA"], fields={"LCSC": "C129213"}))
c_b1 = C("100u/25V", g(34, 10), 0, fp="Cel", polar=True)
c_b2 = C("10u", g(38, 10), 0)
c_b3 = C("100n", g(42, 10), 0)
# wires: DC + → fuse → D1 → VIN ; DC − → GNDD
S.wire(j_dc.pin(1), f1.pin(1), via='h'); S.wire(f1.pin(2), d1.pin(2), via='h')
S.wire(j_dc.pin(2), (j_dc.pin(2)[0] + G, j_dc.pin(2)[1])); gnd((j_dc.pin(2)[0] + G, j_dc.pin(2)[1]), "GNDD")
S.wire(j_bt.pin(1), d2.pin(2), via='h')
S.glabel("BATT-", j_bt.pin(2), 0)
vin_bus_y = d1.pin(1)[1]
S.wire(d1.pin(1), (c_b3.pin(1)[0], vin_bus_y))
S.wire(d2.pin(1), (d2.pin(1)[0] + G, d2.pin(1)[1])); S.wire((d2.pin(1)[0] + G, d2.pin(1)[1]), (d2.pin(1)[0] + G, vin_bus_y)); S.junction((d2.pin(1)[0] + G, vin_bus_y))
for c in (d3, c_b1, c_b2, c_b3):
    top = c.pin(1) if c is not d3 else c.pin(2)
    bot = c.pin(2) if c is not d3 else c.pin(1)
    S.wire((top[0], vin_bus_y), top); S.junction((top[0], vin_bus_y))
    gnd(bot, "GNDD")
rail("VIN", (c_b3.pin(1)[0], vin_bus_y))
S.add(Sym("power", "PWR_FLAG", "#FLG1", "PWR_FLAG", (c_b3.pin(1)[0] + G, vin_bus_y), 0)); S.wire((c_b3.pin(1)[0], vin_bus_y), (c_b3.pin(1)[0] + G, vin_bus_y))
S.add(Sym("power", "PWR_FLAG", "#FLG2", "PWR_FLAG", (j_dc.pin(2)[0] + G, j_dc.pin(2)[1]), 0))
S.text("Rail = VIN (≈8.6 V battery / ≈11.6 V adapter). Battery − returns via the OUT ring (the power switch).", g(4, 20), 1.4)

# +9VA filtered analog rail: VIN → 100R → 100u → +9VA
r_f  = R("100R", g(50, 8), 90)
c_f  = C("100u/25V", g(56, 10), 0, fp="Cel", polar=True)
rail("VIN", r_f.pin(1)); S.wire(r_f.pin(2), (c_f.pin(1)[0], r_f.pin(2)[1])); S.wire((c_f.pin(1)[0], r_f.pin(2)[1]), c_f.pin(1)); gnd(c_f.pin(2), "GNDA")
rail("+9VA", (c_f.pin(1)[0], r_f.pin(2)[1])); S.junction((c_f.pin(1)[0], r_f.pin(2)[1]))
S.text("+9VA: RC-filtered op-amp rail (brief §3.4).", g(48, 14), 1.4)
S.add(Sym("power", "PWR_FLAG", "#FLG3", "PWR_FLAG", (c_f.pin(1)[0] + 2*G, r_f.pin(2)[1]), 0)); S.wire((c_f.pin(1)[0], r_f.pin(2)[1]), (c_f.pin(1)[0] + 2*G, r_f.pin(2)[1]))

# Ground star: NetTie AGND ↔ DGND, ONE point (Seed3 datasheet Fig 1.5). Enclosure bond on AGND.
nt = S.add(Sym("Device", "NetTie_2", "NT1", "STAR", g(72, 10), 0, footprint=FP["NT"], fields={"Note": "the ONE AGND↔DGND join; place at power entry next to Seed DGND"}))
gnd(nt.pin(1), "GNDA"); gnd(nt.pin(2), "GNDD")
S.add(Sym("power", "PWR_FLAG", "#FLG4", "PWR_FLAG", (nt.pin(1)[0], nt.pin(1)[1] - 2*G), 0)); S.wire(nt.pin(1), (nt.pin(1)[0], nt.pin(1)[1] - 2*G))
S.text("GROUND STAR — NT1 is the single AGND↔DGND join (pinmap §4l). Place at the power-entry corner.", g(48, 17), 1.4)
j_bond = S.add(Sym("Connector", "Conn_01x02_Pin", "J5", "Enclosure bond", g(84, 8), 0, footprint=FP["JST2"], fields={"Note": "both pins AGND (pinmap §4j2) → one lug on the casting"}))
gnd(j_bond.pin(1), "GNDA"); gnd(j_bond.pin(2), "GNDA")

# ================================================================ 2. SEED3 (centre-left)
seed = S.add(Sym("TT", "Daisy_Seed3", "U4", "Daisy Seed3", g(30, 50), 0, footprint=FP["HDR20"],
                 fields={"Note": "2 × 20-way 0.1\" sockets (Sullins PPTC201LFBN-RC); Seed plugs in component-side up"}))
S.text("DAISY SEED3 — pin numbers per daisy/board.h. Unused GPIO marked NC.", g(4, 24), 1.8)
def seed_net(num, net, stub=2, rot=None, glabel=True):
    p = seed.pin(num)
    left = p[0] < seed.at[0]
    q = (p[0] - stub * G, p[1]) if left else (p[0] + stub * G, p[1])
    S.wire(p, q)
    if glabel: S.glabel(net, q, 180 if left else 0)
    return q
# side A
seed_net(2, "SD_DAT3"); seed_net(3, "SD_DAT2"); seed_net(4, "SD_DAT1"); seed_net(5, "SD_DAT0"); seed_net(6, "SD_CMD"); seed_net(7, "SD_CLK")
seed_net(8, "SD_CD")
seed_net(12, "SCL_U"); seed_net(13, "SDA_U"); seed_net(14, "OLED_RES"); seed_net(15, "ENC_SW")
seed_net(16, "AIN1"); seed_net(17, "AIN2"); seed_net(18, "AOUT1")
for n in (1, 9, 10, 11, 19): S.noconnect(seed.pin(n))
q = seed.pin(20); S.wire(q, (q[0], q[1] + G)); gnd((q[0], q[1] + G), "GNDA")
# side B
q = seed.pin(21); S.wire(q, (q[0] + G, q[1])); rail("+3V3A", (q[0] + G, q[1]))
seed_net(22, "ENC_A"); seed_net(23, "ENC_B")
seed_net(26, "LED1"); seed_net(27, "LED2")
seed_net(28, "FSW_R"); seed_net(32, "FSW_L")
for n in (24, 25, 29, 30, 31, 33, 34, 35, 36, 37): S.noconnect(seed.pin(n))
q = seed.pin(38); S.wire(q, (q[0] + G, q[1])); rail("+3V3D", (q[0] + G, q[1]))
q = seed.pin(39); S.wire(q, (q[0] + G, q[1])); rail("VIN", (q[0] + G, q[1]))
q = seed.pin(40); S.wire(q, (q[0] + G, q[1])); S.wire((q[0] + G, q[1]), (q[0] + G, q[1] - 2*G)); gnd((q[0] + G, q[1] - 2*G), "GNDD")
S.text("USB: the Seed's own USB-C port via the panel extender — no board connection (D29/D30 NC).", g(4, 76), 1.4)

# ================================================================ 3. FRONT END — Fig 3.3 with rev D Vref
def front_end(chan, y0, uref, jack_ref, jack_note, in_net, ain_net):
    """One channel of the Seed3 Fig 3.3 instrument input. y0 in grid units. Returns nothing; draws."""
    x = 4
    S.text(f"FRONT END CH {chan} — Seed3 Fig 3.3 (RF stopper · unity follower · pole+pad · inverting unity · 100R+33n), Vref = rev D 2.7 V. Rin 1 M (K&K 24.5 nF → 6.5 Hz).", g(x, y0 - 3), 1.8)
    j = S.add(Sym("Connector", "Conn_01x03_Pin", jack_ref, f"IN{chan} jack lead", g(x + 2, y0 + 2), 0, footprint=FP["JST3"], fields={"Note": jack_note}))
    # pins: 1 tip, 2 ring, 3 sleeve
    tip = j.pin(1); ring = j.pin(2); slv = j.pin(3)
    S.wire(slv, (slv[0] + G, slv[1])); gnd((slv[0] + G, slv[1]), "GNDA")
    S.wire(ring, (ring[0] + G, ring[1])); S.noconnect((ring[0] + G, ring[1]))    # ring link is AT THE JACK (pinmap §4e)
    S.glabel(in_net, tip, 0)
    tp(in_net, (tip[0] + 2*G, tip[1] - 2*G)); S.wire(tip, (tip[0] + 2*G, tip[1])); S.wire((tip[0] + 2*G, tip[1]), (tip[0] + 2*G, tip[1] - 2*G))
    # chain along y = yc
    yc = tip[1]
    cin = C("10u film", (tip[0] + 5*G, yc), 90, fp="Cfilm")          # rot 90 → horizontal, pin1 left
    S.wire(tip, cin.pin(1)) if cin.pin(1)[0] < cin.pin(2)[0] else S.wire(tip, cin.pin(2))
    node_a = cin.pin(2) if cin.pin(1)[0] < cin.pin(2)[0] else cin.pin(1)
    rin = R("1M", (node_a[0] + 2*G, yc + 4*G), 0)                     # vertical, to VREF
    S.wire(node_a, (rin.pin(1)[0], yc)); S.wire((rin.pin(1)[0], yc), rin.pin(1)); S.junction((rin.pin(1)[0], yc))
    S.wire(rin.pin(2), (rin.pin(2)[0], rin.pin(2)[1] + G)); rail("VREF", (rin.pin(2)[0], rin.pin(2)[1] + G))
    rstop = R("100R", (rin.pin(1)[0] + 4*G, yc), 90)
    S.wire((rin.pin(1)[0], yc), min(rstop.pin(1), rstop.pin(2)))
    node_b = max(rstop.pin(1), rstop.pin(2))
    cstop = C("100p", (node_b[0] + 2*G, yc + 4*G), 0)
    S.wire(node_b, (cstop.pin(1)[0], yc)); S.wire((cstop.pin(1)[0], yc), cstop.pin(1)); S.junction((cstop.pin(1)[0], yc)); gnd(cstop.pin(2), "GNDA")
    # follower
    a = opamp_half(uref, 1, (cstop.pin(1)[0] + 6*G, yc + G))          # pin 3 '+' sits at at.y - 2.54 = yc
    S.wire((cstop.pin(1)[0], yc), a.pin(3))
    out_a = a.pin(1)
    # feedback: out → − (unity)
    m = a.pin(2)
    S.wire(out_a, (out_a[0] + G, out_a[1])); S.wire((out_a[0] + G, out_a[1]), (out_a[0] + G, m[1] + 2*G)); S.wire((out_a[0] + G, m[1] + 2*G), (m[0] - G, m[1] + 2*G)); S.wire((m[0] - G, m[1] + 2*G), (m[0] - G, m[1])); S.wire((m[0] - G, m[1]), m)
    S.junction((out_a[0] + G, out_a[1]))
    tp(f"A{chan}_OUT", (out_a[0] + G, out_a[1] - 3*G)); S.wire((out_a[0] + G, out_a[1]), (out_a[0] + G, out_a[1] - 3*G))
    # 3k3 → node_c ; 1n to GND ; 4k7 to VREF (pad, DNP)
    r33 = R("3k3", (out_a[0] + 4*G, out_a[1]), 90)
    S.wire((out_a[0] + G, out_a[1]), min(r33.pin(1), r33.pin(2)))
    node_c = max(r33.pin(1), r33.pin(2)); yc2 = node_c[1]
    c1n = C("1n", (node_c[0] + 2*G, yc2 + 4*G), 0)
    S.wire(node_c, (c1n.pin(1)[0], yc2)); S.wire((c1n.pin(1)[0], yc2), c1n.pin(1)); S.junction((c1n.pin(1)[0], yc2)); gnd(c1n.pin(2), "GNDA")
    rpad = R("4k7 (DNP=no pad)", (c1n.pin(1)[0] + 3*G, yc2 + 4*G), 0, dnp=True, Note="fit for −4.6 dB pad (brief §3.1); DNP = as measured")
    S.wire((c1n.pin(1)[0], yc2), (rpad.pin(1)[0], yc2)); S.wire((rpad.pin(1)[0], yc2), rpad.pin(1)); S.junction((rpad.pin(1)[0], yc2))
    S.wire(rpad.pin(2), (rpad.pin(2)[0], rpad.pin(2)[1] + G)); rail("VREF", (rpad.pin(2)[0], rpad.pin(2)[1] + G))
    # 10u film → 10k → inverting stage
    c2 = C("10u film", (rpad.pin(1)[0] + 4*G, yc2), 90, fp="Cfilm")
    S.wire((rpad.pin(1)[0], yc2), min(c2.pin(1), c2.pin(2)))
    r10 = R("10k", (max(c2.pin(1), c2.pin(2))[0] + 4*G, yc2), 90)
    S.wire(max(c2.pin(1), c2.pin(2)), min(r10.pin(1), r10.pin(2)))
    node_d = max(r10.pin(1), r10.pin(2))
    b = opamp_half(uref, 2, (node_d[0] + 5*G, yc2 + G), 0)            # − pin (6) at at.y+2.54... wants node_d at pin 6
    # pin 6 '-' is at (-7.62, -2.54) lib → sch y = at.y + 2.54 ; put at.y = yc2 - G so pin6.y = yc2
    b.at = (round(node_d[0] + 5*G, 3), round(yc2 - G, 3))
    S.wire(node_d, b.pin(6))
    S.wire(b.pin(5), (b.pin(5)[0] - G, b.pin(5)[1])); S.wire((b.pin(5)[0] - G, b.pin(5)[1]), (b.pin(5)[0] - G, b.pin(5)[1] - 2*G)); rail("VREF", (b.pin(5)[0] - G, b.pin(5)[1] - 2*G))
    # feedback 10k ∥ 330p from out (pin 7) to − (pin 6)
    o = b.pin(7)
    rf = R("10k", (node_d[0] + 5*G, yc2 - 5*G), 90)
    cf = C("330p", (node_d[0] + 5*G, yc2 - 8*G), 90)
    S.wire(node_d, (node_d[0], yc2 - 8*G)); S.junction(node_d)
    S.wire((node_d[0], yc2 - 5*G), min(rf.pin(1), rf.pin(2))); S.junction((node_d[0], yc2 - 5*G))
    S.wire((node_d[0], yc2 - 8*G), min(cf.pin(1), cf.pin(2)))
    S.wire(max(rf.pin(1), rf.pin(2)), (o[0] + G, yc2 - 5*G)); S.wire(max(cf.pin(1), cf.pin(2)), (o[0] + G, yc2 - 8*G))
    S.wire((o[0] + G, yc2 - 8*G), (o[0] + G, o[1])); S.wire(o, (o[0] + G, o[1])); S.junction((o[0] + G, yc2 - 5*G)); S.junction((o[0] + G, o[1]))
    # 100R + 33n → AIN
    r100 = R("100R", (o[0] + 4*G, o[1]), 90)
    S.wire((o[0] + G, o[1]), min(r100.pin(1), r100.pin(2)))
    node_e = max(r100.pin(1), r100.pin(2))
    c33 = C("33n", (node_e[0] + 2*G, node_e[1] + 4*G), 0)
    S.wire(node_e, (c33.pin(1)[0], node_e[1])); S.wire((c33.pin(1)[0], node_e[1]), c33.pin(1)); S.junction((c33.pin(1)[0], node_e[1])); gnd(c33.pin(2), "GNDA")
    S.wire((c33.pin(1)[0], node_e[1]), (c33.pin(1)[0] + 3*G, node_e[1]))
    S.glabel(ain_net, (c33.pin(1)[0] + 3*G, node_e[1]), 0)
    tp(ain_net, (c33.pin(1)[0] + 2*G, node_e[1] - 2*G)); S.wire((c33.pin(1)[0] + 2*G, node_e[1]), (c33.pin(1)[0] + 2*G, node_e[1] - 2*G)); S.junction((c33.pin(1)[0] + 2*G, node_e[1]))
    S.text("Second stage inverts → firmware flips the sign (brief §3.1).", g(x + 62, y0 + 2), 1.2)

front_end("A", 86, "U1", "J1", "IN jack, TRS switched, plastic body. pin1 tip · pin2 ring (sleeve–ring link AT THE JACK) · pin3 sleeve", "IN1_TIP", "AIN1")
front_end("B", 112, "U2", "J8", "SECOND HIGH-Z CHANNEL — on the board only, NOT drilled in v1 (brief §3.2)", "IN2_TIP", "AIN2")

# ================================================================ 4. VREF + OUTPUT (Fig 3.6)
y0 = 138; x = 4
S.text("VREF — rev D bias: 1 M / 470 k from +9VA → 2.7 V. Buffered by U3B.", g(x, y0 - 3), 1.8)
rb1 = R("1M", g(x + 4, y0 + 2), 0); rb2 = R("470k", g(x + 4, y0 + 8), 0)
rail("+9VA", rb1.pin(1)); S.wire(rb1.pin(2), rb2.pin(1)); gnd(rb2.pin(2), "GNDA")
mid = rb1.pin(2); cb = C("100n", (mid[0] + 3*G, rb2.at[1]), 0)
S.wire(mid, (cb.pin(1)[0], mid[1])); S.wire((cb.pin(1)[0], mid[1]), cb.pin(1)); S.junction(mid); gnd(cb.pin(2), "GNDA")
ub = opamp_half("U3", 2, (cb.pin(1)[0] + 6*G, mid[1] + G))
S.wire((cb.pin(1)[0], mid[1]), ub.pin(5)); S.junction((cb.pin(1)[0], mid[1]))
o = ub.pin(7); m = ub.pin(6)
S.wire(o, (o[0] + G, o[1])); S.wire((o[0] + G, o[1]), (o[0] + G, m[1] + 2*G)); S.wire((o[0] + G, m[1] + 2*G), (m[0] - G, m[1] + 2*G)); S.wire((m[0] - G, m[1] + 2*G), (m[0] - G, m[1])); S.wire((m[0] - G, m[1]), m)
cv = C("10u", (o[0] + 3*G, o[1] + 3*G), 0)
S.wire((o[0] + G, o[1]), (cv.pin(1)[0], o[1])); S.wire((cv.pin(1)[0], o[1]), cv.pin(1)); S.junction((o[0] + G, o[1])); gnd(cv.pin(2), "GNDA")
S.wire((cv.pin(1)[0], o[1]), (cv.pin(1)[0] + 2*G, o[1])); rail("VREF", (cv.pin(1)[0] + 2*G, o[1])); S.junction((cv.pin(1)[0], o[1]))
tp("VREF", (cv.pin(1)[0] + 2*G, o[1] - 2*G)); S.wire((cv.pin(1)[0] + 2*G, o[1]), (cv.pin(1)[0] + 2*G, o[1] - 2*G))

x = 44
S.text("OUTPUT — Seed3 Fig 3.6 instrument-level stage, UNITY (Rin = Rf1 15k, Rf2 0R link; fit 18k for +7 dB). 100R source, 10u/10k out. OUT ring = battery −.", g(x, y0 - 6), 1.8)
yo = g(0, y0 + 2)[1]
S.glabel("AOUT1", g(x + 2, y0 + 2), 180)
co = C("10u film", g(x + 5, y0 + 2), 90, fp="Cfilm")
S.wire(g(x + 2, y0 + 2), min(co.pin(1), co.pin(2)))
ri = R("15k", (max(co.pin(1), co.pin(2))[0] + 4*G, yo), 90)
S.wire(max(co.pin(1), co.pin(2)), min(ri.pin(1), ri.pin(2)))
nd = max(ri.pin(1), ri.pin(2))
ua = opamp_half("U3", 1, (nd[0] + 5*G, yo + G)); ua.at = (nd[0] + 5*G, yo + G)   # pin 2 '-' at at.y+2.54? pin2 lib y=-2.54 → sch y = at.y+2.54
ua.at = (round(nd[0] + 5*G, 3), round(yo - G, 3))
S.wire(nd, ua.pin(2))
S.wire(ua.pin(3), (ua.pin(3)[0] - G, ua.pin(3)[1])); S.wire((ua.pin(3)[0] - G, ua.pin(3)[1]), (ua.pin(3)[0] - G, ua.pin(3)[1] - 2*G)); rail("VREF", (ua.pin(3)[0] - G, ua.pin(3)[1] - 2*G))
o = ua.pin(1)
rf1 = R("15k", (nd[0] + 3*G, yo - 5*G), 90); rf2 = R("0R (18k = +7 dB)", (nd[0] + 8*G, yo - 5*G), 90, Note="link for unity; 18k adds +7 dB as Fig 3.6")
cfo = C("100p", (nd[0] + 5*G, yo - 8*G), 90)
S.wire(nd, (nd[0], yo - 8*G)); S.junction(nd)
S.wire((nd[0], yo - 5*G), min(rf1.pin(1), rf1.pin(2))); S.junction((nd[0], yo - 5*G))
S.wire(max(rf1.pin(1), rf1.pin(2)), min(rf2.pin(1), rf2.pin(2)))
S.wire((nd[0], yo - 8*G), min(cfo.pin(1), cfo.pin(2)))
S.wire(max(rf2.pin(1), rf2.pin(2)), (o[0] + G, yo - 5*G)); S.wire(max(cfo.pin(1), cfo.pin(2)), (o[0] + G, yo - 8*G))
S.wire((o[0] + G, yo - 8*G), (o[0] + G, o[1])); S.wire(o, (o[0] + G, o[1])); S.junction((o[0] + G, yo - 5*G)); S.junction((o[0] + G, o[1]))
ro = R("100R", (o[0] + 4*G, o[1]), 90)
S.wire((o[0] + G, o[1]), min(ro.pin(1), ro.pin(2)))
co2 = C("10u film", (max(ro.pin(1), ro.pin(2))[0] + 4*G, o[1]), 90, fp="Cfilm")
S.wire(max(ro.pin(1), ro.pin(2)), min(co2.pin(1), co2.pin(2)))
ne = max(co2.pin(1), co2.pin(2))
r10k = R("10k", (ne[0] + 2*G, ne[1] + 4*G), 0)
S.wire(ne, (r10k.pin(1)[0], ne[1])); S.wire((r10k.pin(1)[0], ne[1]), r10k.pin(1)); S.junction((r10k.pin(1)[0], ne[1])); gnd(r10k.pin(2), "GNDA")
tp("OUT_TIP", (r10k.pin(1)[0] + 2*G, ne[1] - 2*G)); S.wire((r10k.pin(1)[0], ne[1]), (r10k.pin(1)[0] + 2*G, ne[1])); S.wire((r10k.pin(1)[0] + 2*G, ne[1]), (r10k.pin(1)[0] + 2*G, ne[1] - 2*G)); S.junction((r10k.pin(1)[0] + 2*G, ne[1]))
jo = S.add(Sym("Connector", "Conn_01x03_Pin", "J2", "OUT jack lead", (r10k.pin(1)[0] + 9*G, ne[1] - G), 180, footprint=FP["JST3"],
               fields={"Note": "OUT jack, TRS SWITCHED, plastic body: pin1 tip · pin2 ring = BATTERY − (switches the pedal on) · pin3 sleeve"}))
# rot 180: pins on the left side; pin1 at at.y+2.54? compute
S.wire((r10k.pin(1)[0] + 2*G, ne[1]), jo.pin(1))
S.wire(jo.pin(2), (jo.pin(2)[0] - 2*G, jo.pin(2)[1])); S.glabel("BATT-", (jo.pin(2)[0] - 2*G, jo.pin(2)[1]), 180)
S.wire(jo.pin(3), (jo.pin(3)[0] - 2*G, jo.pin(3)[1])); gnd((jo.pin(3)[0] - 2*G, jo.pin(3)[1]), "GNDA")

# op-amp power units (3 chips), decoupling
x = 4; y = 154
S.text("OP-AMP POWER — +9VA / AGND, 100n at each chip.", g(x, y - 3), 1.8)
for i, u in enumerate(("U1", "U2", "U3")):
    p = opamp_power(u, g(x + 4 + i * 10, y + 4))
    top = p.pin(8); bot = p.pin(4)
    S.wire(top, (top[0], top[1] - G)); rail("+9VA", (top[0], top[1] - G))
    S.wire(bot, (bot[0], bot[1] + G)); gnd((bot[0], bot[1] + G), "GNDA")
    c = C("100n", (top[0] + 3*G, g(0, y + 4)[1]), 0)
    S.wire((top[0], top[1] - G), (c.pin(1)[0], top[1] - G)); S.wire((c.pin(1)[0], top[1] - G), c.pin(1)); S.junction((top[0], top[1] - G)); gnd(c.pin(2), "GNDA")

# ================================================================ 5. DIGITAL I/O (right column)
X = 100
# --- I2C OLED
y = 24
S.text("OLED 1.3\" SH1106 I²C module on a 7-pin socket. DC + CS hard to GND → 0x3C (board.h). 33R series at the Seed, 4k7 pull-ups at the header (brief §3.6).", g(X, y - 3), 1.8)
r_scl = R("33R", g(X + 4, y + 2), 90); r_sda = R("33R", g(X + 4, y + 5), 90)
S.glabel("SCL_U", min(r_scl.pin(1), r_scl.pin(2)), 180); S.glabel("SDA_U", min(r_sda.pin(1), r_sda.pin(2)), 180)
scl = max(r_scl.pin(1), r_scl.pin(2)); sda = max(r_sda.pin(1), r_sda.pin(2))
pu1 = R("4k7", (scl[0] + 2*G, scl[1] - 4*G), 0); pu2 = R("4k7", (scl[0] + 4*G, scl[1] - 4*G), 0)
S.wire(scl, (pu1.pin(2)[0], scl[1])); S.wire((pu1.pin(2)[0], scl[1]), pu1.pin(2)); S.junction((pu1.pin(2)[0], scl[1]))
S.wire(sda, (pu2.pin(2)[0], sda[1])); S.wire((pu2.pin(2)[0], sda[1]), pu2.pin(2)); S.junction((pu2.pin(2)[0], sda[1]))
S.wire(pu1.pin(1), (pu1.pin(1)[0], pu1.pin(1)[1] - G)); S.wire(pu2.pin(1), (pu2.pin(1)[0], pu2.pin(1)[1] - G)); S.wire((pu1.pin(1)[0], pu1.pin(1)[1] - G), (pu2.pin(1)[0], pu2.pin(1)[1] - G)); rail("+3V3D", (pu1.pin(1)[0], pu1.pin(1)[1] - G))
j_oled = S.add(Sym("Connector", "Conn_01x07_Pin", "J6", "OLED 1.3\" 7-pin", (scl[0] + 12*G, scl[1] + 4*G), 180, footprint=FP["HDR7"],
                   fields={"Note": "module pins: 1 GND · 2 VCC · 3 SCL(CLK) · 4 SDA(MOSI) · 5 RES · 6 DC · 7 CS"}))
# rot 180: pins to the left; pin1 top? lib pin1 at y=+7.62 (top) → rot180 → sch y = at.y + 7.62 (bottom). fine, just wire by pin()
S.wire(j_oled.pin(1), (j_oled.pin(1)[0] - G, j_oled.pin(1)[1])); gnd((j_oled.pin(1)[0] - G, j_oled.pin(1)[1]), "GNDD")
S.wire(j_oled.pin(2), (j_oled.pin(2)[0] - G, j_oled.pin(2)[1])); rail("+3V3D", (j_oled.pin(2)[0] - G, j_oled.pin(2)[1]))
S.wire((pu1.pin(2)[0], scl[1]), (pu1.pin(2)[0] + 3*G, scl[1])); S.wire((pu1.pin(2)[0] + 3*G, scl[1]), (pu1.pin(2)[0] + 3*G, j_oled.pin(3)[1])); S.wire((pu1.pin(2)[0] + 3*G, j_oled.pin(3)[1]), j_oled.pin(3))
S.wire((pu2.pin(2)[0], sda[1]), (pu2.pin(2)[0] + 2*G, sda[1])); S.wire((pu2.pin(2)[0] + 2*G, sda[1]), (pu2.pin(2)[0] + 2*G, j_oled.pin(4)[1])); S.wire((pu2.pin(2)[0] + 2*G, j_oled.pin(4)[1]), j_oled.pin(4))
S.wire(j_oled.pin(5), (j_oled.pin(5)[0] - 3*G, j_oled.pin(5)[1])); S.glabel("OLED_RES", (j_oled.pin(5)[0] - 3*G, j_oled.pin(5)[1]), 180)
S.wire(j_oled.pin(6), (j_oled.pin(6)[0] - G, j_oled.pin(6)[1])); gnd((j_oled.pin(6)[0] - G, j_oled.pin(6)[1]), "GNDD")
S.wire(j_oled.pin(7), (j_oled.pin(7)[0] - G, j_oled.pin(7)[1])); gnd((j_oled.pin(7)[0] - G, j_oled.pin(7)[1]), "GNDD")
c_ol = C("100n", (j_oled.pin(2)[0] - 3*G, j_oled.pin(2)[1] + 3*G), 0)
S.wire((j_oled.pin(2)[0] - G, j_oled.pin(2)[1]), (c_ol.pin(1)[0], j_oled.pin(2)[1])); S.wire((c_ol.pin(1)[0], j_oled.pin(2)[1]), c_ol.pin(1)); S.junction((j_oled.pin(2)[0] - G, j_oled.pin(2)[1])); gnd(c_ol.pin(2), "GNDD")

# --- Encoder
y = 46
S.text("ENCODER — PEC11R, board-mounted. A/B/SW pulled up 10k, 10n debounce. ⚠ bushing must not bond to the casting (brief §2).", g(X, y - 3), 1.8)
enc = S.add(Sym("Device", "RotaryEncoder_Switch", "SW1", "PEC11R-4220F-S0024", g(X + 8, y + 4), 0, footprint=FP["ENC"], fields={"Note": "20 mm D-shaft, detent + switch"}))
for pin, net in (("A", "ENC_A"), ("B", "ENC_B"), ("S1", "ENC_SW")):
    p = enc.pin(pin); left = pin in ("A", "B")
    q = (p[0] - 6*G, p[1]) if left else (p[0] + 6*G, p[1])
    S.wire(p, q); S.glabel(net, q, 180 if left else 0)
    # pull-up + cap on the way
    px = (p[0] - (3*G if pin == "A" else 5*G)) if left else p[0] + 3*G
    r = R("10k", (px, p[1] - 4*G), 0); S.wire((px, p[1]), r.pin(2)); S.junction((px, p[1]))
    S.wire(r.pin(1), (r.pin(1)[0], r.pin(1)[1] - G)); rail("+3V3D", (r.pin(1)[0], r.pin(1)[1] - G))
    c = C("10n", (px, p[1] + 4*G), 0); S.wire((px, p[1]), c.pin(1)); gnd(c.pin(2), "GNDD")
for pin in ("C", "S2"):
    p = enc.pin(pin); q = (p[0] - G, p[1]) if pin == "C" else (p[0] + G, p[1])
    S.wire(p, q); gnd(q, "GNDD")

# --- Footswitches
y = 66
S.text("FOOTSWITCHES — panel-mounted momentary SPST on JST 2-pin leads. 10k pull-up + 100n. LEFT = bypass (D25), RIGHT = tuner (D21).", g(X, y - 3), 1.8)
for i, (jr, net, lab) in enumerate((("J7", "FSW_L", "SW-L bypass"), ("J9", "FSW_R", "SW-R tuner"))):
    j = S.add(Sym("Connector", "Conn_01x02_Pin", jr, lab, g(X + 24 + i * 22, y + 4), 180, footprint=FP["JST2"]))
    p = j.pin(1); q = (p[0] - 6*G, p[1]); S.wire(p, q); S.glabel(net, q, 180)
    px = p[0] - 3*G
    r = R("10k", (px, p[1] - 4*G), 0); S.wire((px, p[1]), r.pin(2)); S.junction((px, p[1]))
    S.wire(r.pin(1), (r.pin(1)[0], r.pin(1)[1] - G)); rail("+3V3D", (r.pin(1)[0], r.pin(1)[1] - G))
    c = C("100n", (px, p[1] + 4*G), 0); S.wire((px, p[1]), c.pin(1)); gnd(c.pin(2), "GNDD")
    p2 = j.pin(2); S.wire(p2, (p2[0] - G, p2[1])); gnd((p2[0] - G, p2[1]), "GNDD")

# --- LEDs
y = 84
S.text("STATUS LEDs — D19 = bypass/engaged, D20 = looper/record. 3 mm through the face; shape/position distinguish them, not colour.", g(X, y - 3), 1.8)
for i, (net, lab) in enumerate((("LED1", "engaged"), ("LED2", "record"))):
    r = R("1k", g(X + 6 + i * 16, y + 3), 90)
    S.glabel(net, min(r.pin(1), r.pin(2)), 180)
    d = S.add(Sym("Device", "LED", ref("D"), f"3mm {lab}", (max(r.pin(1), r.pin(2))[0] + 3*G, max(r.pin(1), r.pin(2))[1]), 180, footprint=FP["LED"]))
    S.wire(max(r.pin(1), r.pin(2)), d.pin(2))
    S.wire(d.pin(1), (d.pin(1)[0] + G, d.pin(1)[1])); gnd((d.pin(1)[0] + G, d.pin(1)[1]), "GNDD")

# --- microSD
y = 100
S.text("microSD — push-push socket ON THE SDMMC PINS (D1–D6). Bit-bang SPI fallback = 4 pin numbers in sd_spi.h: CS=D1 MOSI=D5 SCK=D6 MISO=D4 (brief §3.7).", g(X, y - 3), 1.8)
sd = S.add(Sym("Connector", "Micro_SD_Card_Det_Hirose_DM3AT", "J10", "microSD DM3AT", g(X + 16, y + 8), 0, footprint=FP["SD"], fields={"LCSC": "C114218"}))
for pin, net in (("1", "SD_DAT2"), ("2", "SD_DAT3"), ("3", "SD_CMD"), ("5", "SD_CLK"), ("7", "SD_DAT0"), ("8", "SD_DAT1")):
    p = sd.pin(pin); q = (p[0] - 4*G, p[1]); S.wire(p, q); S.glabel(net, q, 180)
p = sd.pin("4"); S.wire(p, (p[0] - 2*G, p[1])); rail("+3V3D", (p[0] - 2*G, p[1]))
c_sd = C("100n", (p[0] - 2*G - 0*G, p[1] + 0), 0); c_sd.at = (round(p[0] - 6*G, 3), round(p[1] + 2*G, 3))
S.wire((p[0] - 2*G, p[1]), (c_sd.pin(1)[0], p[1])); S.wire((c_sd.pin(1)[0], p[1]), c_sd.pin(1)); S.junction((p[0] - 2*G, p[1])); gnd(c_sd.pin(2), "GNDD")
p = sd.pin("6"); S.wire(p, (p[0] - 2*G, p[1])); gnd((p[0] - 2*G, p[1]), "GNDD")
p = sd.pin("9"); S.wire(p, (p[0] - 2*G, p[1])); gnd((p[0] - 2*G, p[1]), "GNDD")
p = sd.pin("10"); q = (p[0] - 4*G, p[1]); S.wire(p, q); S.glabel("SD_CD", q, 180)
r_cd = R("10k", (p[0] - 2*G, p[1] - 4*G), 0); S.wire((p[0] - 2*G, p[1]), r_cd.pin(2)); S.junction((p[0] - 2*G, p[1]))
S.wire(r_cd.pin(1), (r_cd.pin(1)[0], r_cd.pin(1)[1] - G)); rail("+3V3D", (r_cd.pin(1)[0], r_cd.pin(1)[1] - G))
p = sd.pin("SH"); S.wire(p, (p[0] + G, p[1])); gnd((p[0] + G, p[1]), "GNDD")

# --- Test points on rails
y = 124
S.text("TEST POINTS — every pad has an adjacent GND pad on the layout (feedback_design_for_probeability).", g(X, y - 3), 1.8)
for i, net in enumerate(("VIN", "+9VA", "+3V3D", "+3V3A")):
    t = tp(net, g(X + 4 + i * 8, y + 4)); rail(net, t.pin(1))
for i, net in enumerate(("GNDA", "GNDD")):
    t = tp(net, g(X + 40 + i * 8, y + 4)); gnd(t.pin(1), net)

# ---------------------------------------------------------------- write
# --- project files: TT symbol library, sym-lib-table, .kicad_pro (created once, never overwritten)
LIB = HERE.parent / "TT.kicad_sym"
LIB.write_text('(kicad_symbol_lib (version 20231120) (generator "tt_gen")\n' + _seed_blk.replace('(symbol "TT:Daisy_Seed3"', '(symbol "Daisy_Seed3"', 1) + '\n)\n')
(HERE.parent / "sym-lib-table").write_text('(sym_lib_table (version 7)\n  (lib (name "TT") (type "KiCad") (uri "${KIPRJMOD}/TT.kicad_sym") (options "") (descr "Tone Trixter custom symbols"))\n)\n')
pro = HERE.parent / "tone_trixter.kicad_pro"
if not pro.exists():
    pro.write_text('{\n  "meta": {"filename": "tone_trixter.kicad_pro", "version": 1},\n  "board": {"design_settings": {}},\n  "sheets": [["' + S.uuid + '", "Root"]],\n  "text_variables": {}\n}\n')
for b in S.check(): print("CHECK:", b)
S.write(OUT)
print("wrote", OUT, "symbols", len(S.syms), "wires", len(S.wires))
