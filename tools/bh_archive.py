#!/usr/bin/env python3
"""Reads the Biohazard Outbreak discs' archives straight from the disc image: files in the ISO tree, and the AFS archives
(nested: NETBIO00.DAT holds romdata.afs, which holds the stage data) addressed by a path of names or @indexes.

    bh_archive.py GAME ls   PATH            list an AFS (or the disc directory) at PATH
    bh_archive.py GAME get  PATH OUT        copy one file or member out
    bh_archive.py GAME dump PATH OUTDIR     copy every member of an AFS into OUTDIR as NNNN_name
    bh_archive.py GAME where LSN...         the file or AFS member (as a path) that holds each disc sector

GAME is file1 or file2 (the disc comes from local.json). PATH is "/NETBIO00.DAT/romdata.afs/@580" or ".../FONT8_8.TM2";
a name that occurs more than once in an archive must be given as @index.
As a library: `Archive(game).open(path)` gives a Node with .read(), .size, .members().
"""
import json
import os
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def runtime_tools():
    runtime = os.environ.get("PS2_RUNTIME_DIR")
    if not runtime:
        cache = REPO / "build" / "CMakeCache.txt"
        if not cache.exists():
            raise SystemExit("set PS2_RUNTIME_DIR (or configure build/ so its CMakeCache has it) to find tools/disc.py")
        for line in cache.read_text().splitlines():
            if line.startswith("PS2_RUNTIME_DIR:"):
                runtime = line.split("=", 1)[1]
    return Path(runtime) / "tools"


sys.path.insert(0, str(runtime_tools()))
import disc as discmod  # noqa: E402


def disc_path(game):
    return json.loads((REPO / "local.json").read_text())[game]


class Node:
    """A byte range of the disc image's data (an ISO file or an AFS member)."""

    def __init__(self, disc, offset, size, name):
        self.disc, self.offset, self.size, self.name = disc, offset, size, name

    def read(self, start=0, count=None):
        count = self.size - start if count is None else count
        if start < 0 or start + count > self.size:
            raise ValueError(f"{self.name}: range {start}+{count} outside {self.size}")
        first = (self.offset + start) // 2048
        last = (self.offset + start + count + 2047) // 2048
        raw = self.disc.read_sectors(first, last - first)
        skip = self.offset + start - first * 2048
        return raw[skip:skip + count]

    def is_afs(self):
        return self.size >= 8 and self.read(0, 4) == b"AFS\0"

    def members(self):
        """[(name, Node)] of an AFS; unnamed archives get names like m0012."""
        count = struct.unpack("<I", self.read(4, 4))[0]
        table = self.read(8, 8 * count + 8)
        ents = [struct.unpack_from("<II", table, 8 * i) for i in range(count)]
        name_off, name_size = struct.unpack_from("<II", table, 8 * count)
        names = [f"m{i:04d}" for i in range(count)]
        if name_off and name_size >= 48 * count:
            block = self.read(name_off, 48 * count)
            names = [block[i * 48:i * 48 + 32].split(b"\0")[0].decode("latin-1") for i in range(count)]
        return [(n, Node(self.disc, self.offset + o, s, n)) for n, (o, s) in zip(names, ents)]


class Archive:
    def __init__(self, game):
        self.disc = discmod.Disc(disc_path(game))

    def iso_entry(self, path):
        """(ISO entry, the rest of the path inside it): walks the directories until a file."""
        parts = [p for p in path.split("/") if p]
        entry = self.disc.root
        while parts and entry["dir"]:
            name = parts.pop(0).upper()
            for child in self.disc.listdir(entry):
                if child["name"].upper() == name:
                    entry = child
                    break
            else:
                raise FileNotFoundError(path)
        return entry, parts

    def open(self, path):
        entry, parts = self.iso_entry(path)
        if entry["dir"]:
            raise IsADirectoryError(path)
        node = Node(self.disc, entry["lba"] * 2048, entry["size"], entry["name"])
        for part in parts:
            members = node.members()
            if part.startswith("@"):
                node = members[int(part[1:])][1]
                continue
            hits = [n for name, n in members if name == part]
            if len(hits) != 1:
                raise FileNotFoundError(f"{part}: {len(hits)} matches in {node.name}")
            node = hits[0]
        return node


    def where(self, lsn):
        """The deepest path (ISO file, then AFS members as @index) holding the sector, with the offset in it."""
        at = lsn * 2048
        for path, child in self.disc.walk():
            if child["dir"]:
                continue
            node = Node(self.disc, child["lba"] * 2048, child["size"], child["name"])
            if node.offset <= at < node.offset + node.size:
                return self._descend(node, path, at)
        return None

    @staticmethod
    def _descend(node, path, at):
        while node.is_afs():
            for i, (name, member) in enumerate(node.members()):
                if member.offset <= at < member.offset + member.size:
                    node, path = member, f"{path}/@{i}({name})"
                    break
            else:
                return path + " (in no member)", at - node.offset
        return path, at - node.offset


def main(argv):
    if len(argv) < 4:
        print(__doc__)
        return 1
    game, cmd, path = argv[1:4]
    arc = Archive(game)
    if cmd == "where":
        for lsn in argv[3:]:
            print(lsn, *arc.where(int(lsn)))
        return 0
    if cmd == "ls":
        entry, rest = arc.iso_entry(path)
        if entry["dir"]:
            for child in arc.disc.listdir(entry):
                print(f"{child['size']:12d} {child['name']}")
        else:
            archive = arc.open(path)
            for i, (name, node) in enumerate(archive.members()):
                print(f"@{i:<5d} {node.offset - archive.offset:#12x} {node.size:10d} {name}")
    elif cmd == "get":
        Path(argv[4]).parent.mkdir(parents=True, exist_ok=True)
        Path(argv[4]).write_bytes(arc.open(path).read())
    elif cmd == "dump":
        out = Path(argv[4])
        out.mkdir(parents=True, exist_ok=True)
        for i, (name, node) in enumerate(arc.open(path).members()):
            (out / f"{i:04d}_{name}").write_bytes(node.read())
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
