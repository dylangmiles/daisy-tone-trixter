#!/usr/bin/env python3
"""export_fab.py -- versioned JLCPCB fab export for the Tone Trixie board.

Produces a dated / revisioned / sequenced trio in pcb/out/jlc/releases/:

    tone-trixie-gerbers.<REV>.<YYYYMMDD>.<SEQ>.zip   <- upload to JLC (PCB)
    tone-trixie-bom.<REV>.<YYYYMMDD>.<SEQ>.csv       <- upload to JLC (assembly)
    tone-trixie-cpl.<REV>.<YYYYMMDD>.<SEQ>.csv       <- upload to JLC (assembly)

  REV  = board title-block revision (read from the .kicad_pcb, so it stays in sync)
  DATE = today, YYYYMMDD, zero-padded so the files sort chronologically
  SEQ  = next number for that REV+DATE, so re-exporting the same day never clashes

Each run also appends a line to out/jlc/releases/EXPORTS.log tying the export to the
exact git commit of the board (+dirty if the tree had uncommitted changes), so every
upload is traceable to a revision, a date, and a board state.

Gerber zips are gitignored (binary, regenerable from the committed board); the BOM/CPL
csvs and the log are tracked as the per-release record.

  python3 pcb/tools/export_fab.py          # run from the daisy repo or from pcb/
"""
import subprocess, sys, re, glob, datetime, pathlib, shutil, tempfile

PROD  = "tone-trixie"
HERE  = pathlib.Path(__file__).resolve().parent
PCB   = HERE.parent
BOARD = PCB / "tone_trixter.kicad_pcb"
SCH   = PCB / "tone_trixter.kicad_sch"
REL   = PCB / "out" / "jlc" / "releases"
CLI   = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"


def board_rev():
    m = re.search(r'\(rev "([^"]*)"', BOARD.read_text())
    rev = (m.group(1).strip() if m else "")
    if not rev:
        print("WARN: board title-block revision is empty -> using 'X'")
        rev = "X"
    return rev


def next_seq(rev, date):
    REL.mkdir(parents=True, exist_ok=True)
    seqs = []
    for p in glob.glob(str(REL / f"{PROD}-gerbers.{rev}.{date}.*.zip")):
        m = re.search(rf"\.{re.escape(date)}\.(\d+)\.zip$", p)
        if m:
            seqs.append(int(m.group(1)))
    return (max(seqs) + 1) if seqs else 1


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = "\n".join(l for l in (r.stdout + r.stderr).splitlines() if "traits" not in l)
    if r.returncode != 0:
        print(out)
        sys.exit(f"FAILED: {' '.join(str(c) for c in cmd)}")


def git(*args):
    try:
        return subprocess.run(["git", "-C", str(PCB), *args],
                              capture_output=True, text=True).stdout.strip()
    except Exception:
        return ""


def main():
    rev  = board_rev()
    date = datetime.date.today().strftime("%Y%m%d")
    seq  = next_seq(rev, date)
    tag  = f"{rev}.{date}.{seq}"
    REL.mkdir(parents=True, exist_ok=True)

    gerber_zip = REL / f"{PROD}-gerbers.{tag}.zip"
    bom_csv    = REL / f"{PROD}-bom.{tag}.csv"
    cpl_csv    = REL / f"{PROD}-cpl.{tag}.csv"

    # Gerbers + drill -> temp dir -> single zip (files at zip root, as JLC expects).
    with tempfile.TemporaryDirectory() as td:
        run([CLI, "pcb", "export", "gerbers", "-o", td + "/", str(BOARD)])
        run([CLI, "pcb", "export", "drill", "--format", "excellon",
             "--drill-origin", "absolute", "--excellon-units", "mm", "-o", td + "/", str(BOARD)])
        shutil.make_archive(str(gerber_zip)[:-4], "zip", td)

    run([CLI, "sch", "export", "bom",
         "--fields", "Value,Reference,Footprint,LCSC,${QUANTITY}",
         "--labels", "Comment,Designator,Footprint,LCSC,Quantity",
         "--group-by", "Value,Footprint,LCSC", "--exclude-dnp", "-o", str(bom_csv), str(SCH)])
    run([CLI, "pcb", "export", "pos", "--format", "csv", "--units", "mm",
         "--use-drill-file-origin", "--side", "both", "--exclude-dnp", "-o", str(cpl_csv), str(BOARD)])

    commit = git("rev-parse", "--short", "HEAD") or "nogit"
    dirty  = "+dirty" if git("status", "--porcelain", str(BOARD)) else ""
    stamp  = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    with open(REL / "EXPORTS.log", "a") as f:
        f.write(f"{stamp}  rev {rev}  #{seq}  board={commit}{dirty}  {gerber_zip.name}\n")

    print(f"Rev {rev} - {date} - export #{seq}  (board {commit}{dirty})")
    for p in (gerber_zip, bom_csv, cpl_csv):
        print(f"  {p.relative_to(PCB.parent)}  ({p.stat().st_size // 1024} KB)")
    print("Upload the .zip to the JLC PCB quote; the bom/cpl to PCB Assembly.")


if __name__ == "__main__":
    main()
