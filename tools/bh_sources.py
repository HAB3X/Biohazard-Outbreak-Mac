"""Where a game's translatable text is on the disc: the executable's string pool and the stage message files (config/
translate.toml). Used by bh_extract.py and bh_build_patch.py, so what one finds the other rewrites."""
import re
import tomllib

import bh_archive as archive
import bh_pit as pit
import bh_text as text

CONFIG = archive.REPO / "config" / "translate.toml"
RUNTIME_CONFIG = archive.REPO / "config"


def settings(game):
    return tomllib.loads(CONFIG.read_text())[game]


def exe_pool(arc, game):
    """(ExePool, {image name: Node}) of the game: the executable and the overlays whose data can point at the pool."""
    cfg = settings(game)
    run = tomllib.loads((RUNTIME_CONFIG / f"{game}.toml").read_text())
    exe_node = arc.open("/" + cfg["executable"])
    elf = exe_node.read()
    nodes = {"elf": exe_node}
    # The executable is one segment (file offset 0x200 at its first address): its data is what its text ranges leave
    first, size = 0x100000, 0x14AC00
    cuts = sorted([(int(a, 16), int(b, 16)) for a, b in run["executable"]["text"]] +
                  [tuple(int(x, 16) for x in run["executable"]["vutext"])])
    ranges, pos = [], first
    for lo, hi in cuts:
        if lo > pos:
            ranges.append((pos, lo))
        pos = max(pos, hi)
    ranges.append((pos, first + size))
    images = {"elf": (elf, first - 0x200, ranges)}
    for ov in run["overlays"]:
        node = arc.open("/" + ov["file"])
        base, (tlo, thi) = int(ov["base"], 16), [int(x, 16) for x in ov["text"]]
        images[ov["file"]] = (node.read(), base, [(base, tlo), (thi, base + node.size)])
        nodes[ov["file"]] = node
    pool = text.ExePool(elf, tuple(cfg["pool"]), cfg["base"], images)
    return pool, nodes


def message_files(arc, game):
    """[(path, Node)] of the stage message files of the game."""
    cfg = settings(game)
    wanted = re.compile(cfg["message_files"])
    out = []
    for i, (name, stage) in enumerate(arc.open("/NETBIO00.DAT").members()):
        if re.fullmatch(r"r\d{3}\.afs", name):
            for j, (member, node) in enumerate(stage.members()):
                if wanted.match(member):
                    out.append((f"/NETBIO00.DAT/@{i}({name})/@{j}({member})", node))
    return out


def pit_entries(arc, game):
    """[(name, Node, entry offset, SlotTable or DocChain)] (a document entry may grow into the room before the next entry) of the text entries of the game's pit files."""
    out = []
    members = [("pit_comn.dat", node) for name, node in arc.open("/NETBIO00.DAT/romdata.afs").members() if name == "pit_comn.dat"]
    for name, stage in arc.open("/NETBIO00.DAT").members():
        if re.fullmatch(r"r\d{3}\.afs", name):
            members += [(m, node) for m, node in stage.members() if m.lower().startswith("pit_s")]
    for name, node in members:
        data = node.read()
        entries = pit.momo(data)
        for index, (kind, limit) in pit.kinds_of(name).items():
            offset, size = entries[index]
            if size:
                entry = data[offset:offset + size]
                later = [o for o, _ in entries if o > offset]
                room = (min(later) if later else len(data)) - offset
                out.append((name, node, offset, pit.DocChain(entry, room) if kind == "docs" else pit.SlotTable(entry, limit)))
    return out
