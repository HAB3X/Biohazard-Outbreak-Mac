#!/usr/bin/env python3
"""Builds the English patch from your disc and the translation files: the byte ranges the runtime lays over the disc (--patch).

    bh_build_patch.py GAME OUT.patch [TRANSLATIONS_DIR]

TRANSLATIONS_DIR (default translations/GAME) holds *.tsv files: unit id, TAB, the English line ("\\n" for a line break), and
optionally TAB, a note. Units without a translation stay Japanese. Every line is checked: ASCII only, the Japanese's control
tags kept, and what is rebuilt must fit its container (an error names what does not). A game whose config names a `fallback`
game uses that game's English for the lines both games share and its own folder doesn't translate. The pictures of
translations/GAME/pictures.toml are redrawn too (bh_pictures.py), and the fonts' button glyphs drawn as keys (bh_font.py).

The patch is version 2 of the runtime's disc patch: "DPAT", u32 2, u32 count, then per range u64 disc offset, u32 length, u32
variant (0 always; 1 laid while the keyboard and mouse are the device used last, 2 while a gamepad is: a picture with the
PS2's buttons drawn on it has both, at the same offset and size), the bytes.
"""
import struct
import sys
from pathlib import Path

import bh_archive as archive
import bh_font as font
import bh_pictures as pictures
import bh_sources as sources
import bh_text as text

MAGIC = 0x54415044  # "DPAT", version 2: each range has a variant (0 always, 1 keyboard and mouse, 2 gamepad)


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


def check_overlaps(patches):
    """Sorts the ranges; two may cover the same bytes only as the two devices' variants of one range (same offset and size)."""
    patches.sort(key=lambda p: (p[0], p[2]))
    for (a, da, va), (b, db, vb) in zip(patches, patches[1:]):
        if a + len(da) > b and not (a == b and len(da) == len(db) and {va, vb} == {1, 2}):
            raise SystemExit(f"two changes overlap at disc offset {b:#x}")


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
        patches = [(off, data, 0) for off, data in patches] + pictures.patches(arc, game) + font.patches(arc, game)
    except (text.DoesNotFit, pictures.DoesNotFit) as e:
        raise SystemExit(f"does not fit: {e}")
    check_overlaps(patches)
    with open(out_path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, 2, len(patches)))
        for off, data, variant in patches:
            f.write(struct.pack("<QII", off, len(data), variant) + data)
    keyed = sum(1 for p in patches if p[2])
    print(f"{len(new)} of {len(units)} units translated; {len(patches)} ranges ({keyed} for one device), "
          f"{sum(len(p[1]) for p in patches)} bytes -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
