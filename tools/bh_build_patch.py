#!/usr/bin/env python3
"""Builds the English patch from your disc and the translation files: the byte ranges the runtime lays over the disc (--patch).

    bh_build_patch.py GAME OUT.patch [TRANSLATIONS_DIR]

TRANSLATIONS_DIR (default translations/GAME) holds *.tsv files: unit id, TAB, the English line ("\\n" for a line break), and
optionally TAB, a note. Units without a translation stay Japanese. Every line is checked: ASCII only, the Japanese's control
tags kept, and what is rebuilt must fit its container (an error names what does not). A game whose config names a `fallback`
game uses that game's English for the lines both games share and its own folder doesn't translate.
"""
import struct
import sys
from pathlib import Path

import bh_archive as archive
import bh_sources as sources
import bh_text as text

MAGIC = 0x54415044  # "DPAT"


def load_translations(folder, units, fallback=None):
    """{unit id: English bytes}; units = {unit id: original bytes} the disc has. `fallback`: another game's translations
    folder, whose English is used for the units `folder` doesn't have (the same Japanese line in both games); its units
    that this disc doesn't have are passed over."""
    out, handled = {}, set()
    for source in [folder] + ([fallback] if fallback else []):
        own = source == folder
        for tsv in sorted(Path(source).glob("*.tsv")):
            for n, line in enumerate(tsv.read_text().splitlines(), 1):
                if not line.strip() or line.startswith("#"):
                    continue
                uid, _, rest = line.partition("\t")
                english = rest.split("\t")[0]
                if not own and (uid not in units or uid in handled):
                    continue
                if uid not in units:
                    raise SystemExit(f"{tsv.name}:{n}: unit {uid} is not in this disc's text")
                if uid in out or (own and uid in handled):
                    raise SystemExit(f"{tsv.name}:{n}: unit {uid} translated twice")
                if own:
                    handled.add(uid)
                if english == "-":  # kept Japanese on purpose
                    continue
                try:
                    out[uid] = text.english_bytes(english, units[uid])
                except (UnicodeEncodeError, ValueError) as e:
                    raise SystemExit(f"{tsv.parent.name}/{tsv.name}:{n}: unit {uid}: {e}")
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
    pools, nodes = sources.string_pools(arc, game)
    files = sources.message_files(arc, game)
    units = {}
    for _, pool in pools:
        units.update(pool.texts())
    messages = []
    for path, node in files:
        msg = text.MsgBin(node.read())
        units.update(msg.texts())
        messages.append((path, node, msg))
    pits = sources.pit_entries(arc, game) + sources.item_msg_entries(arc, game)
    for _, _, _, table in pits:
        units.update(table.texts())
    fallback = sources.settings(game).get("fallback")
    new = load_translations(folder, units, fallback and archive.REPO / "translations" / fallback)
    patches = []  # (disc offset, bytes)
    try:
        images = {}  # image name: its new bytes (one image can be changed by several pools: strings, pointers)
        for _, pool in pools:
            for name, ranges in pool.rebuilt(new).items():
                image = images.setdefault(name, bytearray(nodes[name].read()))
                for off, data in ranges:  # in order: a slot is cleared, then strings are written into it
                    image[off:off + len(data)] = data
        for name, image in images.items():
            patches += [(nodes[name].offset + off, data) for off, data in changed_runs(nodes[name].read(), image)]
        for path, node, msg in messages:
            mine = {u: new[u] for u in msg.texts() if u in new}
            if mine:
                patches += [(node.offset + off, data) for off, data in changed_runs(node.read(), msg.rebuilt(mine))]
        for name, node, offset, table in pits:
            mine = {u: new[u] for u in table.texts() if u in new}
            if mine:
                patches += [(node.offset + offset + off, data)
                            for off, data in changed_runs(table.entry, table.rebuilt(mine))]
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
