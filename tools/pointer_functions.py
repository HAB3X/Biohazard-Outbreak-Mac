#!/usr/bin/env python3
"""Functions a game calls only through pointers: the recompiler starts functions at calls, jumps and the config's extra
starts, so a handler reached only through a table of function pointers is part of the function before it, and the run stops at
the first call of one ("call to ...: not a function start"). A data word pointing at a prologue (addiu sp,sp,-N), or at code right after a JR RA and its delay slot, in the text
is such a start. This finds them in the executable's data and the overlays' (a pointer may be in either, to either) and
prints (or with --write puts into the config) the `extra` lists. Code after zero padding that follows a JR RA is taken as a start as well.

usage: pointer_functions.py CONFIG ELF TABLE.c DISC [--write]
  CONFIG  config/file2.toml            ELF  the executable the build extracted (build/generated/SLPM_656.92)
  TABLE.c the build's generated table (build/generated/file2_code/table.c): the functions already started
  DISC    the disc image (the overlays are read from it)
"""
import re
import struct
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

RUNTIME = Path(__file__).resolve().parent.parent.parent / "ps2-native-runtime"


def prologue(word):
    return (word >> 16) == 0x27BD and (word & 0x8000) != 0


def load_elf(path):
    d = Path(path).read_bytes()
    phoff, = struct.unpack_from("<I", d, 0x1C)
    n, = struct.unpack_from("<H", d, 0x2C)
    segs = []
    for i in range(n):
        t, off, va, _pa, fs, ms, _fl, _al = struct.unpack_from("<8I", d, phoff + i * 32)
        if t == 1 and fs:
            segs.append((va, d[off:off + fs]))
    return segs


def words(base, blob):
    for i in range(0, len(blob) - 3, 4):
        yield base + i, struct.unpack_from("<I", blob, i)[0]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    write = "--write" in sys.argv
    if len(args) != 4:
        sys.exit(__doc__)
    config_path, elf, table, disc = args
    cfg = tomllib.loads(Path(config_path).read_text())
    tab = Path(table).read_text()
    starts = {}
    for m in re.finditer(r"\{0x([0-9a-f]{8})u, 0x[0-9a-f]{8}u, (\w+?)_?([0-9a-f]{8})\b", tab):
        starts.setdefault(m.group(2).rstrip("_"), set()).add(int(m.group(1), 16))
    # regions: name -> (text ranges, a function's first word source)
    exe_text = [tuple(int(x, 0) for x in r) for r in cfg["executable"]["text"]]
    segs = load_elf(elf)
    image = {}
    for va, blob in segs:
        for a, w in words(va, blob):
            image[a] = w
    regions = {"f": (exe_text, image)}
    sources = [(a, w) for a, w in image.items() if not any(lo <= a < hi for lo, hi in exe_text)]
    with tempfile.TemporaryDirectory() as tmp:
        for ov in cfg.get("overlays", []):
            if ov.get("encrypted"):
                continue
            path = Path(tmp) / ov["file"].replace("/", "_")
            subprocess.run([sys.executable, str(RUNTIME / "tools" / "disc.py"), "get", disc, ov["file"], str(path)],
                           check=True, capture_output=True)
            base = int(ov["base"], 0)
            text = [tuple(int(x, 0) for x in ov["text"])]
            img = dict(words(base, path.read_bytes()))
            regions[ov["name"]] = (text, img)
            sources += [(a, w) for a, w in img.items() if not any(lo <= a < hi for lo, hi in text)]
    found = {}
    for name, (text, img) in regions.items():
        have = starts.get(name, set())
        for _a, w in sources:
            if w % 4 == 0 and any(lo <= w < hi for lo, hi in (text[:1] if name == "f" else text)) and w not in have \
                    and (prologue(img.get(w, 0)) or img.get(w - 8) == 0x03E00008):
                found.setdefault(name, set()).add(w)
    # A prologue after zero padding is a start whether or not anything points at it
    for name, (text, img) in regions.items():
        have = starts.get(name, set())
        # the executable's later ranges are libkernel's kernel mode code: no starts of its own are wanted there
        for lo, hi in (text[:1] if name == "f" else text):
            for a in range(lo + 4, hi, 4):
                if a in have or img.get(a, 0) == 0 or img.get(a - 4, 1) != 0:
                    continue
                # a prologue, or (a leaf function) any code after the zero padding that follows a JR RA and its delay slot
                b = a - 4
                while img.get(b, 1) == 0 and a - b < 64:
                    b -= 4
                if prologue(img[a]) or 0x03E00008 in (img.get(b), img.get(b - 4)):
                    found.setdefault(name, set()).add(a)
    for name, addrs in found.items():
        print(f"{name}: {len(addrs)} starts")
    if not write:
        return
    s = Path(config_path).read_text()
    for name, addrs in found.items():
        if name == "f":
            block = re.search(r"\[functions\]\nextra = \[[^\]]*\]\n", s)
            old = {int(x, 0) for x in cfg.get("functions", {}).get("extra", [])}
        else:
            block = re.search(r'\[\[overlays\]\]\nname = "%s"\n.*?(?=\n\[\[overlays\]\]|\n\[|\Z)' % name, s, re.S)
            old = {int(x, 0) for o in cfg["overlays"] if o["name"] == name for x in o.get("extra", [])}
        merged = sorted(old | addrs)
        rows = [", ".join(f'"0x{a:x}"' for a in merged[i:i + 8]) for i in range(0, len(merged), 8)]
        lst = "extra = [\n" + "".join(f"    {r},\n" for r in rows) + "]\n"
        if name == "f":
            s = s.replace(block.group(0), "[functions]\n" + lst)
        else:
            blk = block.group(0)
            blk = re.sub(r"extra = \[[^\]]*\]\n", "", blk).rstrip("\n") + "\n" + lst
            s = s.replace(block.group(0), blk)
    Path(config_path).write_text(s)


main()
