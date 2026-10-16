#!/usr/bin/env python3
"""Cuts a batch of untranslated units out of bh_extract.py's units.jsonl, to translate.

    bh_batch.py UNITS.jsonl TRANSLATIONS_DIR OUT.txt [--where SUBSTRING] [--skip N] [--count N]

Writes "unit id TAB Japanese TAB where" lines (a newline in a string as \\n; where: the speakers' numbers of the spm files it is
in, or the other files' names) of the units no .tsv in TRANSLATIONS_DIR has, those found in a container whose name contains
SUBSTRING, most used first.
"""
import json
import sys
from pathlib import Path


FEMALE = {"05", "06", "07", "14", "15", "16"}


def main(argv):
    args, opts = [], {}
    it = iter(argv[1:])
    for a in it:
        if a.startswith("--"):
            opts[a[2:]] = next(it)
        else:
            args.append(a)
    if len(args) != 3:
        print(__doc__)
        return 1
    done = {line.split("\t")[0] for tsv in Path(args[1]).glob("*.tsv") for line in tsv.read_text().splitlines() if line.strip()}
    units = [json.loads(line) for line in open(args[0], encoding="utf-8")]
    units = [u for u in units if u["id"] not in done and any(opts.get("where", "") in w for w in u["where"])]
    units.sort(key=lambda u: (-sum(u["where"].values()), u["id"]))
    units = units[int(opts.get("skip", 0)):][:int(opts.get("count", len(units)))]
    def where(u):
        speakers = sorted({w[7:9] for w in u["where"] if w.startswith("spm")})
        others = sorted(w.split(".")[0] for w in u["where"] if not w.startswith("spm"))
        if not speakers:
            return " ".join(others[:4])
        # Speakers 00-04 and 09-13 are the men, 05-07 and 14-16 the women (by their lines); a few are named
        sexes = {"F" if s in FEMALE else "M" for s in speakers}
        return "".join(sorted(sexes)) + (" " + ",".join(speakers) if len(speakers) <= 3 else "")

    with open(args[2], "w", encoding="utf-8") as out:
        for u in units:
            out.write(f"{u['id']}\t{u['jp'].replace(chr(10), chr(92) + 'n')}\t{where(u)}\n")
    print(f"{len(units)} units -> {args[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
