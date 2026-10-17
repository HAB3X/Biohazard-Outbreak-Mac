#!/usr/bin/env python3
"""Adds translated lines to a translation file.

    bh_merge_translation.py UNITS.jsonl IN.txt OUT.tsv

IN.txt: lines "unit id (or its first 4+ characters) TAB English". Each is checked against bh_extract.py's units.jsonl (the id
must match exactly one unit) and appended to OUT.tsv as "full unit id TAB English"; a unit already in OUT.tsv or in another
.tsv next to it is an error. "-" as the English keeps the unit Japanese on purpose (names, text the game edits in place).
"""
import json
import sys
from pathlib import Path


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 1
    ids = [json.loads(line)["id"] for line in open(argv[1], encoding="utf-8")]
    out_path = Path(argv[3])
    have = {line.split("\t")[0] for tsv in out_path.parent.glob("*.tsv") for line in tsv.read_text().splitlines()
            if line.strip() and line[0] != "#"}
    lines = []
    for n, line in enumerate(open(argv[2], encoding="utf-8").read().splitlines(), 1):
        if not line.strip():
            continue
        prefix, tab, english = line.partition("\t")
        found = [i for i in ids if i.startswith(prefix)]
        if len(prefix) < 4 or len(found) != 1 or not tab:
            raise SystemExit(f"{argv[2]}:{n}: {prefix!r} is not one unit (or no TAB)")
        if found[0] in have:
            raise SystemExit(f"{argv[2]}:{n}: unit {found[0]} is already translated")
        have.add(found[0])
        lines.append(f"{found[0]}\t{english}\n")
    with open(out_path, "a", encoding="utf-8") as out:
        out.writelines(lines)
    print(f"{len(lines)} lines added to {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
