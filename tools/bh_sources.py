"""Where a game's translatable text is on the disc: the programs' string pools, the stage message files, the pit files,
File 1's item and message files, the programs' inverted lines and the network browser's pages (config/translate.toml). Used by
bh_extract.py and bh_build_patch.py, so what one finds the other rewrites."""
import re
import struct
import tomllib

import bh_archive as archive
import bh_itemmsg as itemmsg
import bh_pit as pit
import bh_text as text

CONFIG = archive.REPO / "config" / "translate.toml"
RUNTIME_CONFIG = archive.REPO / "config"


def settings(game):
    return tomllib.loads(CONFIG.read_text())[game]


def elf_segment(elf):
    """(file offset, address, size in the file) of the executable's first program header."""
    if elf[:4] != b"\x7fELF":
        raise ValueError("the executable is not an ELF file")
    phoff = struct.unpack_from("<I", elf, 0x1C)[0]
    _, offset, vaddr, _, filesz = struct.unpack_from("<5I", elf, phoff)
    return offset, vaddr, filesz


def string_pools(arc, game):
    """([(label, ExePool)], {image name: Node}) of the game: the executable's pool ("exe"), then the overlays' pools the config
    lists (labelled by the overlay's file), and the images whose data can point at them."""
    cfg = settings(game)
    run = tomllib.loads((RUNTIME_CONFIG / f"{game}.toml").read_text())
    exe_node = arc.open("/" + cfg["executable"])
    elf = exe_node.read()
    nodes = {"elf": exe_node}
    # The executable's first program header is its loaded segment (the others are the overlays' load areas, with no bytes in
    # the file): its data is what its text ranges leave
    file_offset, first, size = elf_segment(elf)
    cuts = sorted([(int(a, 16), int(b, 16)) for a, b in run["executable"]["text"]] +
                  [tuple(int(x, 16) for x in run["executable"]["vutext"])])
    ranges, pos = [], first
    for lo, hi in cuts:
        if lo > pos:
            ranges.append((pos, lo))
        pos = max(pos, hi)
    ranges.append((pos, first + size))
    images = {"elf": (elf, first - file_offset, ranges)}
    for ov in run["overlays"]:
        node = arc.open("/" + ov["file"])
        base, (tlo, thi) = int(ov["base"], 16), [int(x, 16) for x in ov["text"]]
        images[ov["file"]] = (node.read(), base, [(base, tlo), (thi, base + node.size)])
        nodes[ov["file"]] = node
    pools = [("exe", text.ExePool(elf, tuple(cfg["pool"]), cfg["base"], images))]
    # An overlay's strings are pointed at from its own data only: the overlays share load addresses, so another image's word
    # with the same value means another overlay's byte
    for ov in cfg.get("overlay_pools", []):
        data, base, data_ranges = images[ov["file"]]
        pools.append((ov["file"], text.ExePool(data, tuple(ov["region"]), base, {ov["file"]: images[ov["file"]]},
                                               own=ov["file"], in_place=ov.get("in_place", False))))
    return pools, nodes


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
    """[(name, Node, entry offset, SlotTable or DocChain)] of the text entries of the game's pit files."""
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
                out.append((name, node, offset, pit.DocChain(entry) if kind == "docs" else pit.SlotTable(entry, limit)))
    return out


def item_msg_entries(arc, game):
    """[(name, Node, entry offset, SlotTable or DocChain)] of the text of File 1's item and message files (bh_itemmsg), in
    romdata.afs and in the stage archives; [] for a game whose config doesn't ask for them."""
    if not settings(game).get("item_msg"):
        return []
    members = list(arc.open("/NETBIO00.DAT/romdata.afs").members())
    for name, stage in arc.open("/NETBIO00.DAT").members():
        if re.fullmatch(r"r\d{3}\.afs", name):
            members += stage.members()
    out = []
    for name, node in members:
        if not name.lower().endswith(".dat") or node.size > 0x80000:
            continue
        for offset, table in itemmsg.tables(name, node.read()):
            out.append((name, node, offset, table))
    return out


def program_inverted(arc, game):
    """[(file name, Node, offset, SlotTable)] of the programs' inverted Shift-JIS lines (as in the pit files; config
    `inverted`)."""
    out = []
    for item in settings(game).get("inverted", []):
        node = arc.open("/" + item["file"])
        lo, hi = item["region"]
        out.append((item["file"].rsplit("/", 1)[-1], node, lo, pit.SlotTable(node.read(lo, hi - lo), None)))
    return out


def html_pages(arc, game):
    """[(page name, Node, 0, HtmlPage)] of the network browser's pages with Japanese in them (config `html`: their archive)."""
    path = settings(game).get("html")
    if not path:
        return []
    pages = [(name, node, 0, text.HtmlPage(node.read())) for name, node in arc.open(path).members()
             if name.upper().endswith(".HTM")]
    return [p for p in pages if p[3].units]


def table_entries(arc, game):
    """Every container rebuilt in its own bytes: the pit files' entries, File 1's item and message files, the programs'
    inverted lines, the browser's pages."""
    return pit_entries(arc, game) + item_msg_entries(arc, game) + program_inverted(arc, game) + html_pages(arc, game)
