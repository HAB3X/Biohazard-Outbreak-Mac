#!/usr/bin/env python3
"""A summary of the games' script instruction handlers, read off the recompiled game code (the C that `cmake` generates from the disc):

    bh_script_ops.py GAME OPTABLE.json [OPS...]
    bh_script_ops.py GAME OPTABLE.json --markdown    a table of every op (name, uses in the rooms' scripts, what its handler touches)

Every script (the rooms' section 1 and the scenario's .evb) is a list of records that the game's interpreter (File 2: game_006d72b0 in the
game overlay) runs: a record's first word is (class << 24) | op, its length 2 ** class words, and the op number indexes a table of
8 byte entries (the handler's address and three flag bytes) at 0x74CCE0 in File 2's memory; the handler is called with a0 pointing at the
words after the op word and returns 0 (done), 2 (carry on) and so on (read from the interpreter, not all cases understood).
OPTABLE.json is {op: [handler address, flags]} read from a memory dump of the running game (--control's `ram`): tools/bh_script_ops.py
doesn't make it. For each handler this prints what can be read mechanically: the argument words it loads (offset and width), the fixed
memory addresses it touches and the functions it calls. What an op MEANS is in docs/research/script-ops.md, from reading these and the code.
"""
import glob
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def source(game, address):
    """The C text of the function at `address` in the generated code of `game` (the game overlay's name is game_, the executable's f_)"""
    for prefix in ("game", "f", "demo"):
        for path in sorted(glob.glob(str(ROOT / "build" / "generated" / f"{game}_code" / "code_*.c"))):
            text = Path(path).read_text()
            start = text.find(f"void {prefix}_{address:08x}(")
            if start >= 0:
                end = text.find("\nvoid ", start + 10)
                return text[start:end if end > 0 else None]
    return None


LOAD = re.compile(r"rd(8|16|32|64)\(\(u32\)\(c->r\[(\d+)\]\.uw\[0\] \+ (-?\d+)\)\)")
STORE = re.compile(r"wr(8|16|32|64)\(\(u32\)\(c->r\[(\d+)\]\.uw\[0\] \+ (-?\d+)\), ")
LUI = re.compile(r"c->r\[(\d+)\]\.sd\[0\] = \(s64\)\(\(s32\)0x([0-9a-f]+)u\);")
MOVE = re.compile(r"c->r\[(\d+)\]\.sd\[0\] = \(s64\)\(c->r\[(\d+)\]\.ud\[0\] \+ 0\);")
CALL = re.compile(r"\b((?:game|f|demo)_[0-9a-f]{8})\(c\)")


def summarise(game, address):
    text = source(game, address)
    if text is None:
        return None
    aliases, lui, args, fixed, gp, calls = {4}, {}, {}, set(), set(), []
    for line in text.split("\n"):
        line = line.strip()
        if line.startswith("//") or line.startswith("PS2_"):
            continue
        m = LUI.search(line)
        if m:
            lui[int(m.group(1))] = int(m.group(2), 16)
        for rx, kind in ((LOAD, "load"), (STORE, "store")):
            for width, reg, offset in rx.findall(line):
                reg, offset, width = int(reg), int(offset), int(width)
                if reg in aliases and kind == "load":
                    args[offset] = max(args.get(offset, 0), width)
                elif reg == 28:
                    gp.add(f"gp{offset:+d}")
                elif reg in lui:
                    fixed.add(f"{(lui[reg] + offset) & 0xFFFFFFFF:#x}" + ("w" if kind == "store" else "r"))
        m = MOVE.search(line)
        if m and int(m.group(2)) in aliases:
            aliases.add(int(m.group(1)))
        elif (m := re.search(r"c->r\[(\d+)\]\.sd\[0\] = ", line)) and int(m.group(1)) in aliases - {4} and not MOVE.search(line):
            aliases.discard(int(m.group(1)))
        for call in CALL.findall(line):
            if call not in calls and not call.endswith(f"{address:08x}"):
                calls.append(call)
    return {"args": dict(sorted(args.items())), "fixed": sorted(fixed), "gp": sorted(gp), "calls": calls}


def markdown(game, table):
    """The table of all ops as Markdown: what each handler reads and touches (mechanical) next to the names bh_rdt.OPS gives"""
    import collections

    import bh_rdt

    uses = collections.Counter(ins.op for _, _, node in bh_rdt.rooms(game) for ins in bh_rdt.Room(node.read()).script_instructions())
    print(f"# Script ops of {game}: what each handler touches\n\nMade by `tools/bh_script_ops.py {game} docs/research/{game}-optable.json --markdown`. "
          "`uses` counts the records in the rooms' scripts; `args` are the handler's loads from its argument words (offset:bits); `memory` the fixed addresses "
          "it reads (r) or writes (w); `calls` the functions it calls. The names are `bh_rdt.OPS`'s (what was read or measured: docs/research/script-ops.md).\n")
    print("| op | name | uses | handler | args | memory | calls |\n|---|---|---|---|---|---|---|")
    for op in sorted(int(k) for k in table):
        handler, flags = table[str(op)]
        info = summarise(game, handler) or {"args": {}, "fixed": [], "gp": [], "calls": []}
        name = bh_rdt.OPS[op][0] if op in bh_rdt.OPS else ""
        args = " ".join(f"{o}:{w}" for o, w in info["args"].items())
        print(f"| {op:#x} | {name} | {uses.get(op, 0)} | {handler:#x} | {args} | {' '.join(info['fixed'] + info['gp'])} | {' '.join(info['calls'])} |")


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    game, table = argv[1], json.load(open(argv[2]))
    if "--markdown" in argv:
        markdown(game, table)
        return 0
    wanted = [int(a, 0) for a in argv[3:]] or sorted(int(k) for k in table)
    for op in wanted:
        handler, flags = table[str(op)]
        info = summarise(game, handler)
        print(f"op {op:#x}  handler {handler:#x}  flags {flags:#x}  {info}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
