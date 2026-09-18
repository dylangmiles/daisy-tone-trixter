"""Tiny s-expression helpers for pulling symbol definitions out of KiCad's libraries."""
import re, pathlib
KLIB = pathlib.Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols")

def _block(s, start):
    depth = 0; j = start
    while True:
        c = s[j]
        if c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0: return s[start:j+1]
        j += 1

_cache = {}
def lib_symbol(lib, name):
    """Return the (symbol "lib:name" ...) block, renamed for lib_symbols embedding."""
    key = (lib, name)
    if key in _cache: return _cache[key]
    s = open(KLIB / f"{lib}.kicad_sym").read()
    i = s.index(f'(symbol "{name}"')
    blk = _block(s, i)
    # Flatten derived symbols: KiCad resolves (extends) against the library, not against the
    # schematic's own lib_symbols, so a derived symbol embedded as-is has no pins. Take the
    # parent's body (units, pins, graphics), rename it, and keep the child's properties.
    m = re.search(r'\(extends "([^"]+)"\)', blk)
    if m:
        par = m.group(1)
        pblk = _block(s, s.index(f'(symbol "{par}"'))
        # child's property list (everything before its first sub-symbol / end)
        child_props = re.findall(r'\(property "[^"]+" "[^"]*".*?\n\t\t\)', blk, re.S)
        # parent body: from its first unit sub-symbol to the end
        i0 = pblk.index(f'(symbol "{par}_')
        units = pblk[i0:-1].replace(f'"{par}_', f'"{name}_')
        head = re.sub(r'\(property "[^"]+" "[^"]*".*?\n\t\t\)', '', pblk[:i0], flags=re.S)
        head = head.replace(f'(symbol "{par}"', f'(symbol "{name}"', 1)
        blk = head + "\n".join(child_props) + "\n" + units + ")"
    blk = blk.replace(f'(symbol "{name}"', f'(symbol "{lib}:{name}"', 1)
    _cache[key] = blk
    return blk

def parent_of(lib, name):
    return None   # derived symbols are flattened by lib_symbol()

def lib_pins(lib, name):
    """[(number, name, x, y, rot)] in library coords (y up). Derived symbols use the parent's pins."""
    blk = lib_symbol(lib, name)
    out = []
    for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)(.*?)\(number "([^"]+)"', blk, re.S):
        nm = re.search(r'\(name "([^"]*)"', m.group(4))
        out.append((m.group(5), nm.group(1) if nm else "", float(m.group(1)), float(m.group(2)), int(m.group(3))))
    return out
