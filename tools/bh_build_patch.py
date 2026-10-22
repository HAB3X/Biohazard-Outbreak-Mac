#!/usr/bin/env python3
"""Builds the English patch from your disc and the translation files: the byte ranges the runtime lays over the disc (--patch).

    bh_build_patch.py GAME OUT.patch [TRANSLATIONS_DIR]

TRANSLATIONS_DIR (default translations/GAME) holds *.tsv files: unit id, TAB, the English line ("\\n" for a line break), and
optionally TAB, a note. Units without a translation stay Japanese. Every line is checked: ASCII only, the Japanese's control
tags kept, and what is rebuilt must fit its container (an error names what does not). A game whose config names a `fallback`
game uses that game's English for the lines both games share and its own folder doesn't translate. The pictures of
translations/GAME/pictures.toml are redrawn too (bh_pictures.py), and the fonts' button glyphs drawn as keys (bh_font.py). It
ends with the coverage: per kind of container, how many of its units are translated.

The patch is version 2 of the runtime's disc patch: "DPAT", u32 2, u32 count, then per range u64 disc offset, u32 length, u32
variant (0 always; 1 laid while the prompts are the keyboard's, 2 a PlayStation pad's, 3 an Xbox pad's: the runtime's Button
prompts, the device used last unless one is chosen; a picture with the PS2's buttons drawn on it has all three, at the same
offset and size), the bytes.
"""
import os
import sys
from pathlib import Path

import bh_archive as archive
import bh_font as font
from bh_patchfile import write_patch
import bh_pictures as pictures
import bh_sources as sources
import bh_text as text

def load_translations(folder, units, fallback=None):
    """({unit id: English bytes}, {unit ids kept Japanese on purpose}, {unit ids dropped: text.DROP}); units = {unit id: original bytes} the disc has. `fallback`: another game's translations
    folder, whose English is used for the units `folder` doesn't have (the same Japanese line in both games); its units
    that this disc doesn't have are passed over."""
    out, handled, kept, dropped = {}, set(), set(), set()
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
                    kept.add(uid)
                    continue
                if english == text.DROP:
                    dropped.add(uid)
                try:
                    out[uid] = text.english_bytes(english, units[uid])
                except (UnicodeEncodeError, ValueError) as e:
                    raise SystemExit(f"{tsv.parent.name}/{tsv.name}:{n}: unit {uid}: {e}")
    return out, kept, dropped


def drop_lines(pools, nodes, images, rows, dropped):
    """Takes the dropped lines (text.DROP) out of the message tables the config lists (`message_rows`: an image and a
    region of its file, see text.drop_lines), in the rebuilt `images`. Returns how many lines were taken out."""
    at = {}  # image name: (its base, the addresses of its dropped strings, as on the disc)
    for _, pool in pools:
        base, addresses = at.setdefault(pool.own, (pool.base, set()))
        addresses.update(pool.starts[i] + base for i in pool.translatable() if text.unit_id(pool.raw[i]) in dropped)
    count = 0
    for table in rows:
        base, addresses = at[table["file"]]
        image = images.setdefault(table["file"], bytearray(nodes[table["file"]].read()))
        count += text.drop_lines(nodes[table["file"]].read(), image, tuple(table["region"]), base, addresses)
    return count


def coverage(kinds, new, kept):
    """Lines "kind: translated/total" (and how many are kept Japanese on purpose) of the units by the kind of container they
    were first found in: what is left to translate."""
    rows = {}
    for uid, kind in kinds.items():
        row = rows.setdefault(kind, [0, 0, 0])
        row[0] += uid in new
        row[1] += 1
        row[2] += uid in kept
    return [f"  {kind:12} {done:6} / {total:<6}" + (f" ({k} kept Japanese)" if k else "")
            for kind, (done, total, k) in sorted(rows.items())]


def message_kind(path):
    """A stage message file's kind, by its name: rooms (rNNN), subtitles (dmNNN), speech (spm...)."""
    name = path.rsplit("(", 1)[1]
    return "speech" if name.startswith("spm") else "subtitles" if name.startswith("dm") else "rooms"


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


def progress(fraction, step):
    """How far the build is, for the launcher's bar: a line "fraction<TAB>step" appended to $BH_PROGRESS's file, if set"""
    path = os.environ.get("BH_PROGRESS")
    if path:
        with open(path, "a") as f:
            f.write(f"{fraction:.2f}\t{step}\n")


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__)
        return 1
    game, out_path = argv[1], argv[2]
    folder = argv[3] if len(argv) == 4 else archive.REPO / "translations" / game
    arc = archive.Archive(game)
    progress(0.02, "Reading the game's text from your disc")
    pools, nodes = sources.string_pools(arc, game)
    progress(0.15, "Reading the stage messages")
    files = sources.message_files(arc, game)
    units, kinds = {}, {}  # kinds: unit id -> the kind of container it was first found in, for the coverage report
    for label, pool in pools:
        found = pool.texts()
        units.update(found)
        kinds.update({u: label for u in found if u not in kinds})
    messages = []
    for path, node in files:
        msg = text.MsgBin(node.read())
        found = msg.texts()
        units.update(found)
        kinds.update({u: message_kind(path) for u in found if u not in kinds})
        messages.append((path, node, msg))
    progress(0.30, "Reading the files and documents")
    pits = sources.table_entries(arc, game)
    for name, _, _, table in pits:
        found = table.texts()
        units.update(found)
        kind = ("html" if isinstance(table, text.HtmlPage) else "pit" if name.lower().startswith("pit_")
                else "item files" if name.lower().endswith(".dat") else name)
        kinds.update({u: kind for u in found if u not in kinds})
    fallback = sources.settings(game).get("fallback")
    new, kept, dropped = load_translations(folder, units, fallback and archive.REPO / "translations" / fallback)
    progress(0.45, "Writing the English text")
    patches = []  # (disc offset, bytes)
    try:
        images = {}  # image name: its new bytes (one image can be changed by several pools: strings, pointers)
        for _, pool in pools:
            for name, ranges in pool.rebuilt(new).items():
                image = images.setdefault(name, bytearray(nodes[name].read()))
                for off, data in ranges:  # in order: a slot is cleared, then strings are written into it
                    image[off:off + len(data)] = data
        taken = drop_lines(pools, nodes, images, sources.settings(game).get("message_rows", []), dropped)
        for name, image in images.items():
            patches += [(nodes[name].offset + off, data) for off, data in changed_runs(nodes[name].read(), image)]
        for path, node, msg in messages:
            mine = {u: new[u] for u in msg.texts() if u in new}
            if mine:
                patches += [(node.offset + off, data) for off, data in changed_runs(node.read(), msg.rebuilt(mine))]
        for name, node, offset, table in pits:
            mine = {u: new[u] for u in table.texts() if u in new}
            if mine:
                try:
                    rebuilt = table.rebuilt(mine)
                except text.DoesNotFit as e:
                    raise text.DoesNotFit(f"{name}: {e}")
                patches += [(node.offset + offset + off, data) for off, data in changed_runs(table.entry, rebuilt)]
        progress(0.65, "Redrawing the pictures in English")
        drawn = pictures.patches(arc, game)
        progress(0.90, "Drawing the button prompts")
        patches = [(off, data, 0) for off, data in patches] + drawn + font.patches(arc, game)
    except (text.DoesNotFit, pictures.DoesNotFit) as e:
        raise SystemExit(f"does not fit: {e}")
    progress(0.97, "Saving the patch")
    write_patch(out_path, patches)
    keyed = sum(1 for p in patches if p[2])
    print(f"{len(new)} of {len(units)} units translated; {len(patches)} ranges ({keyed} for one device), "
          f"{sum(len(p[1]) for p in patches)} bytes -> {out_path}")
    print(f"{len(dropped)} lines dropped (the PS2's memory card slots), taken out of message tables {taken} times")
    print("\n".join(coverage(kinds, new, kept)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
