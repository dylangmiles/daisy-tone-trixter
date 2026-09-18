"""Minimal KiCad 8-format schematic writer. Symbols come from KiCad's own libraries (kisym),
placed on a 1.27 mm grid; nets are made with wires + labels. Verified with kicad-cli (ERC + netlist)."""
import uuid, math
from kisym import lib_symbol, lib_pins, parent_of

def U(): return str(uuid.uuid4())
def P(a): return (round(a[0], 3), round(a[1], 3))   # every coordinate goes through here

def _rot(px, py, r):
    # library coords are y-up; schematic is y-down. Rotate then flip y.
    a = math.radians(r)
    x = px*math.cos(a) - py*math.sin(a)
    y = px*math.sin(a) + py*math.cos(a)
    return round(x, 4), round(-y, 4)

class Sym:
    def __init__(self, lib, name, ref, value, at, rot=0, unit=1, footprint="", fields=None, mirror=None):
        self.lib, self.name, self.ref, self.value = lib, name, ref, value
        self.at, self.rot, self.unit, self.fp = P(at), rot, unit, footprint
        self.fields = fields or {}
        self.mirror = mirror   # None | "x" | "y"
        self.dnp = False
        self.uuid = U()
        self.pins = {p[0]: p for p in lib_pins(lib, name)}
        # unit filter: keep pins that belong to this unit (unit 0 = common)
        self._unit_pins = self._pins_of_unit()

    def _pins_of_unit(self):
        import re
        base = self.name
        blk = lib_symbol(self.lib, base)
        keep = {}
        for m in re.finditer(r'\(symbol "%s_(\d+)_\d+"' % re.escape(base), blk):
            u = int(m.group(1))
            if u not in (0, self.unit): continue
            i = m.start(); depth = 0; j = i
            while True:
                c = blk[j]
                if c == '(': depth += 1
                elif c == ')':
                    depth -= 1
                    if depth == 0: break
                j += 1
            sub = blk[i:j+1]
            for pm in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)(.*?)\(number "([^"]+)"', sub, re.S):
                keep[pm.group(5)] = (float(pm.group(1)), float(pm.group(2)), int(pm.group(3)))
        return keep

    def pin(self, num):
        """Schematic coordinates of a pin's connection point."""
        px, py, _ = self._unit_pins[str(num)]
        if self.mirror == "y": px = -px
        if self.mirror == "x": py = -py
        dx, dy = _rot(px, py, self.rot)
        return (round(self.at[0] + dx, 3), round(self.at[1] + dy, 3))

class Sch:
    def __init__(self, title, paper="A3"):
        self.title, self.paper = title, paper
        self.uuid = U()
        self.syms, self.wires, self.labels, self.glabels, self.junctions, self.texts, self.nc = [], [], [], [], [], [], []
        self.plabels = []
        self.libs = {}

    def add(self, sym):
        self.syms.append(sym); self.libs[(sym.lib, sym.name)] = 1; return sym

    def wire(self, a, b, via=None):
        """Manhattan wire a→b. via='h' goes horizontal first, 'v' vertical first; straight if aligned."""
        a, b = P(a), P(b)
        if a == b: return
        if a[0] == b[0] or a[1] == b[1]:
            self.wires.append((a, b)); return
        if via is None: via = 'h'
        m = (b[0], a[1]) if via == 'h' else (a[0], b[1])
        self.wires.append((a, m)); self.wires.append((m, b))

    def label(self, name, at, rot=0): self.labels.append((name, P(at), rot))
    def glabel(self, name, at, rot=0, shape="bidirectional"): self.glabels.append((name, P(at), rot, shape))
    def junction(self, at): self.junctions.append(P(at))
    def text(self, s, at, size=1.27): self.texts.append((s, at, size))
    def noconnect(self, at): self.nc.append(P(at))

    def power(self, net, at, rot=0, name=None):
        """Place a power symbol (power:<net>) whose pin lands on `at`."""
        lib_name = name or net
        s = Sym("power", lib_name, "#PWR", net, at, rot)
        s.value = net
        self.add(s); return s

    def _sym_sexp(self, s):
        m = {"x": " (mirror x)", "y": " (mirror y)", None: ""}[s.mirror]
        props = [("Reference", s.ref, (s.at[0]+2.54, s.at[1]-2.54), False),
                 ("Value", s.value, (s.at[0]+2.54, s.at[1]+2.54), False),
                 ("Footprint", s.fp, s.at, True),
                 ("Datasheet", "~", s.at, True)]
        for k, v in s.fields.items(): props.append((k, v, s.at, True))
        ps = ""
        for k, v, at, hide in props:
            h = " (hide yes)" if hide or s.ref.startswith("#") else ""
            v = str(v).replace('"', '\\"')
            ps += f'    (property "{k}" "{v}" (at {at[0]} {at[1]} 0) (effects (font (size 1.27 1.27)){h}))\n'
        pins = "".join(f'    (pin "{n}" (uuid "{U()}"))\n' for n in s._unit_pins)
        return (f'  (symbol (lib_id "{s.lib}:{s.name}") (at {s.at[0]} {s.at[1]} {s.rot}){m} (unit {s.unit})\n'
                f'    (exclude_from_sim no) (in_bom {"no" if s.ref.startswith("#") or s.dnp else "yes"}) (on_board yes) (dnp {"yes" if s.dnp else "no"})\n'
                f'    (uuid "{s.uuid}")\n{ps}{pins}'
                f'    (instances (project "{self.title}" (path "/{self.uuid}" (reference "{s.ref}") (unit {s.unit}))))\n  )\n')

    def check(self):
        """Flag pins that a wire passes THROUGH (an unintended connection), and label/junction points off any wire."""
        pins = []
        for sy in self.syms:
            for n in sy._unit_pins: pins.append((sy.ref, n, sy.pin(n)))
        bad = []
        for a, b in self.wires:
            for ref, n, p in pins:
                if p == a or p == b: continue
                if a[0] == b[0] == p[0] and min(a[1], b[1]) < p[1] < max(a[1], b[1]): bad.append(("wire through pin", ref, n, p))
                if a[1] == b[1] == p[1] and min(a[0], b[0]) < p[0] < max(a[0], b[0]): bad.append(("wire through pin", ref, n, p))
        ends = set()
        for a, b in self.wires: ends.add(a); ends.add(b)
        for ref, n, p in pins: ends.add(p)
        for name, at, rot, shape in self.glabels:
            if at not in ends: bad.append(("label off wire", name, at))
        return bad

    def write(self, path):
        out = [f'(kicad_sch (version 20231120) (generator "tt_gen") (generator_version "8.0")\n  (uuid "{self.uuid}")\n  (paper "{self.paper}")\n']
        out.append('  (title_block (title "%s") (date "2026-09-18") (rev "A") (company "Tone Trixter"))\n' % self.title)
        out.append("  (lib_symbols\n")
        done = set()
        for lib, name in list(self.libs):
            par = parent_of(lib, name)
            if par and (lib, par) not in done:
                out.append(lib_symbol(lib, par) + "\n"); done.add((lib, par))
            if (lib, name) not in done:
                out.append(lib_symbol(lib, name) + "\n"); done.add((lib, name))
        out.append("  )\n")
        for a in self.junctions:
            out.append(f'  (junction (at {a[0]} {a[1]}) (diameter 0) (color 0 0 0 0) (uuid "{U()}"))\n')
        for a in self.nc:
            out.append(f'  (no_connect (at {a[0]} {a[1]}) (uuid "{U()}"))\n')
        for a, b in self.wires:
            out.append(f'  (wire (pts (xy {a[0]} {a[1]}) (xy {b[0]} {b[1]})) (stroke (width 0) (type default)) (uuid "{U()}"))\n')
        for s, at, size in self.texts:
            s = s.replace('"', '\\"')
            out.append(f'  (text "{s}" (exclude_from_sim no) (at {at[0]} {at[1]} 0) (effects (font (size {size} {size})) (justify left bottom)) (uuid "{U()}"))\n')
        for name, at, rot in self.labels:
            out.append(f'  (label "{name}" (at {at[0]} {at[1]} {rot}) (fields_autoplaced yes) (effects (font (size 1.27 1.27)) (justify left bottom)) (uuid "{U()}"))\n')
        for name, at, rot, shape in self.glabels:
            just = "left" if rot in (0, 90) else "right"
            out.append(f'  (global_label "{name}" (shape {shape}) (at {at[0]} {at[1]} {rot}) (fields_autoplaced yes) (effects (font (size 1.27 1.27)) (justify {just})) (uuid "{U()}")\n'
                       f'    (property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {at[0]} {at[1]} 0) (effects (font (size 1.27 1.27)) (hide yes)))\n  )\n')
        for s in self.syms: out.append(self._sym_sexp(s))
        out.append('  (sheet_instances (path "/" (page "1")))\n)\n')
        open(path, "w").write("".join(out))
