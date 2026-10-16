#!/usr/bin/env python3
"""Dumps every translatable string of a game's disc, for translating and for checking coverage.

    bh_extract.py GAME OUTDIR

OUTDIR/units.jsonl: one line per unit (a distinct Japanese string): its id, the Japanese text, how many times and in which
containers it occurs. Run it on your own disc: the output is the game's text, so it belongs in the ignored work/ folder.
"""
import json
import sys
from pathlib import Path

import bh_archive as archive
import bh_sources as sources
import bh_text as text


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 1
    game, out = argv[1], Path(argv[2])
    arc = archive.Archive(game)
    units = {}

    def add(raw, where):
        u = units.setdefault(text.unit_id(raw), {"jp": raw.decode("cp932"), "raw": raw.hex(), "where": {}})
        u["where"][where] = u["where"].get(where, 0) + 1

    pool, _ = sources.exe_pool(arc, game)
    for i in pool.translatable():
        add(pool.raw[i], "exe")
    for path, node in sources.message_files(arc, game):
        msg = text.MsgBin(node.read())
        if msg.rebuilt({}) != node.read():
            raise SystemExit(f"{path}: does not rebuild to itself")
        for _, raw in msg.strings:
            if text.has_japanese(raw):
                add(raw, path.rsplit("/", 1)[1].split("(")[1].rstrip(")"))
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "units.jsonl", "w") as f:
        for uid, u in units.items():
            f.write(json.dumps({"id": uid, **u}, ensure_ascii=False) + "\n")
    print(f"{len(units)} units, {sum(sum(u['where'].values()) for u in units.values())} occurrences -> {out / 'units.jsonl'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
