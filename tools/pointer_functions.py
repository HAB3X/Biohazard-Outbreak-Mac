#!/usr/bin/env python3
"""Functions a game calls only through pointers: the recompiler starts functions at calls, jumps and the config's extra
starts, so a handler reached only through a table of function pointers is part of the function before it, and the run stops at
the first call of one ("call to ...: not a function start"). A data word pointing at a prologue (addiu sp,sp,-N), or at code right after a JR RA and its delay slot, in the text
is such a start (never a zero word: padding before a function isn't its start). This finds them in the executable's data and the overlays' (a pointer may be in either, to either) and
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


def after_function_end(img, a):
    """Whether code at `a` follows a function's last jump and its delay slot: a JR RA, with or without zero padding after it,
    or a J (a tail call) with zero padding after its delay slot (File 2's game overlay ends 0x67E330 with one, and the
    handler after it at 0x67E350 is in a table). A J with code right after its delay slot is a jump inside a function"""
    b = a - 4
    while img.get(b, 1) == 0 and a - b < 64:
        b -= 4
    for jump, padding in ((img.get(b, 0), a - b - 8), (img.get(b - 4, 0), a - b - 4)):
        if jump == 0x03E00008 and padding >= 0 or jump >> 26 == 2 and padding > 0:
            return True
    return False


def words(base, blob):
    for i in range(0, len(blob) - 3, 4):
        yield base + i, struct.unpack_from("<I", blob, i)[0]


def switch_cases(cfg, regions):
    """The case labels of the switches the recompiler finds in each overlay (its jump tables' targets)"""
    sys.path.insert(0, str(RUNTIME / "recomp"))
    from analysis import Program
    from image import CodeImage
    cases = {}
    for ov in cfg.get("overlays", []):
        if ov["name"] not in regions:
            continue
        text, img = regions[ov["name"]]
        base = min(img)
        data = b"".join(struct.pack("<I", img[base + 4 * k]) for k in range(len(img)))
        program = Program(CodeImage.from_overlay(data, base, text[0]), {"extra_functions": [], "not_functions": []})
        program.analyse()
        cases[ov["name"]] = {t for targets in program.jump_tables.values() for t in targets}
    return cases


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
    # The pointers' sources: the executable's data, and each overlay's (by the overlay they're in, None for the executable)
    sources = [(None, a, w) for a, w in image.items() if not any(lo <= a < hi for lo, hi in exe_text)]
    spans = {}
    with tempfile.TemporaryDirectory() as tmp:
        for ov in cfg.get("overlays", []):
            if ov.get("encrypted") or ov.get("generated"):
                continue  # (a generated overlay isn't on the disc: its starts come with it, in its own .json)
            path = Path(tmp) / ov["file"].replace("/", "_")
            subprocess.run([sys.executable, str(RUNTIME / "tools" / "disc.py"), "get", disc, ov["file"], str(path)],
                           check=True, capture_output=True)
            base = int(ov["base"], 0)
            text = [tuple(int(x, 0) for x in ov["text"])]
            data = path.read_bytes()
            if ov.get("decrypt"):
                # an overlay that decrypts blocks of itself: its code as it runs (the recompiler's own decryption)
                sys.path.insert(0, str(RUNTIME / "recomp"))
                from selfdecrypt import decrypt
                data, _blocks = decrypt(data, base, [int(x, 0) for x in ov["decrypt"]], text[0])
            img = dict(words(base, data))
            regions[ov["name"]] = (text, img)
            spans[ov["name"]] = (base, base + len(data))
            # its overrides are functions too (named by the game, not <overlay>_<address> in the table)
            starts.setdefault(ov["name"], set()).update(int(a, 0) for a in ov.get("overrides", {}))
            sources += [(ov["name"], a, w) for a, w in img.items() if not any(lo <= a < hi for lo, hi in text)]

    def together(source, target):
        """Whether a pointer in `source` can name code in `target`: two overlays over the same addresses are never in
        memory together"""
        if source is None or target == "f" or source == target:
            return True
        (a0, a1), (b0, b1) = spans[source], spans[target]
        return a1 <= b0 or b1 <= a0
    cases = switch_cases(cfg, regions)
    words_at = {(source, a): w for source, a, w in sources}
    found = {}
    for name, (text, img) in regions.items():
        have = starts.get(name, set())
        for source, a, w in sources:
            if not together(source, name):
                continue
            if w % 4 or not any(lo <= w < hi for lo, hi in (text[:1] if name == "f" else text)) or w in have \
                    or img.get(w, 0) == 0:
                continue
            # a word naming code after a function's end (a JR RA or a tail call's J) is a start unless it's a case of a
            # switch (a jump table's entry, which the data holds too): a function's prologue always is. Code after a register
            # jump that isn't JR RA (a dispatcher ending in its own jump through a table, File 1's game overlay at 0x665870)
            # is a start when the word is in a table of starts (a known start beside it): a jump table's neighbours are
            # cases, not starts
            after_end = after_function_end(img, w)
            in_table_of_starts = any(words_at.get((source, a + d)) in have for d in (-4, 4))
            after_jump = (img.get(w - 8, 0) & 0xFC1FFFFF) == 0x00000008 and img.get(w - 4) == 0 and in_table_of_starts
            # (the recompiler takes such a table for the dispatcher's own switch when an entry falls inside it: the table
            # of starts says it isn't)
            # (a dispatcher that jumps through a table of functions, File 2's game overlay at 0x67D000, is taken for a switch
            # too: its entries are starts when the table's are, the leaf ones as well as those with a prologue)
            if prologue(img[w]) or (after_end and (w not in cases.get(name, ()) or in_table_of_starts)) or after_jump:
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
        print(f"{name}: {len(addrs)} starts: {' '.join(hex(a) for a in sorted(addrs))}")
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
