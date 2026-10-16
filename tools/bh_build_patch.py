#!/usr/bin/env python3
"""Builds the English patch from your disc and the translation files: the byte ranges the runtime lays over the disc (--patch).

    bh_build_patch.py GAME OUT.patch [TRANSLATIONS_DIR]

TRANSLATIONS_DIR (default translations/GAME) holds *.tsv files: unit id, TAB, the English line ("\\n" for a line break), and
optionally TAB, a note. Units without a translation stay Japanese. Every line is checked: ASCII only, the Japanese's control
tags kept, and what is rebuilt must fit its container (an error names what does not).
"""
import struct
import sys
from pathlib import Path

import bh_archive as archive
import bh_sources as sources
import bh_text as text

MAGIC = 0x54415044  # "DPAT"


def load_translations(folder, units):
    """{unit id: English bytes}; units = {unit id: original bytes} the disc has."""
    out = {}
    for tsv in sorted(Path(folder).glob("*.tsv")):
        for n, line in enumerate(tsv.read_text().splitlines(), 1):
            if not line.strip() or line.startswith("#"):
                continue
            uid, _, rest = line.partition("\t")
            english = rest.split("\t")[0]
            if english == "-":  # kept Japanese on purpose
                continue
            if uid not in units:
                raise SystemExit(f"{tsv.name}:{n}: unit {uid} is not in this disc's text")
            if uid in out:
                raise SystemExit(f"{tsv.name}:{n}: unit {uid} translated twice")
            try:
                out[uid] = text.english_bytes(english, units[uid])
            except (UnicodeEncodeError, ValueError) as e:
                raise SystemExit(f"{tsv.name}:{n}: unit {uid}: {e}")
    return out


def changed_runs(old, new, gap=16):
    """[(offset, bytes)]: where new differs from old, runs closer than `gap` bytes joined."""
    runs, i = [], 0
    while i < len(old):
        if old[i] == new[i]:
            i += 1
            continue
        j = i
        while j < len(old) and (old[j] != new[j] or any(old[k] != new[k] for k in range(j, min(j + gap, len(old))))):
            j += 1
        runs.append((i, bytes(new[i:j])))
        i = j
    return runs


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__)
        return 1
    game, out_path = argv[1], argv[2]
    folder = argv[3] if len(argv) == 4 else archive.REPO / "translations" / game
    arc = archive.Archive(game)
    pool, nodes = sources.exe_pool(arc, game)
    files = sources.message_files(arc, game)
    units = dict(pool.texts())
    messages = []
    for path, node in files:
        msg = text.MsgBin(node.read())
        units.update(msg.texts())
        messages.append((path, node, msg))
    new = load_translations(folder, units)
    patches = []  # (disc offset, bytes)
    try:
        for name, ranges in pool.rebuilt(new).items():
            old = nodes[name].read()
            image = bytearray(old)
            for off, data in ranges:  # in order: a slot is cleared, then strings are written into it
                image[off:off + len(data)] = data
            patches += [(nodes[name].offset + off, data) for off, data in changed_runs(old, image)]
        for path, node, msg in messages:
            mine = {u: new[u] for u in msg.texts() if u in new}
            if mine:
                patches += [(node.offset + off, data) for off, data in changed_runs(node.read(), msg.rebuilt(mine))]
    except text.DoesNotFit as e:
        raise SystemExit(f"does not fit: {e}")
    patches.sort(key=lambda p: p[0])
    for (a, da), (b, _) in zip(patches, patches[1:]):
        if a + len(da) > b:
            raise SystemExit(f"two changes overlap at disc offset {b:#x}")
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, 1, len(patches)))
        for off, data in patches:
            f.write(struct.pack("<QI", off, len(data)) + data)
    print(f"{len(new)} of {len(units)} units translated; {len(patches)} ranges, {sum(len(d) for _, d in patches)} bytes -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
