"""The runtime's disc patch file (--patch): "DPAT", u32 version 2, u32 count, then per range u64 disc offset, u32 length, u32
variant (0 always; 1 laid while the prompts are the keyboard's, 2 a PlayStation pad's, 3 an Xbox pad's), the bytes."""
import struct

MAGIC = 0x54415044  # "DPAT", version 2: each range has a variant (0 always, 1 keyboard, 2 PlayStation, 3 Xbox)


def check_overlaps(patches):
    """Sorts the ranges; two may cover the same bytes only as different prompts' variants of one range (same offset and
    size)."""
    patches.sort(key=lambda p: (p[0], p[2]))
    for (a, da, va), (b, db, vb) in zip(patches, patches[1:]):
        if a + len(da) > b and not (a == b and len(da) == len(db) and va != vb and 0 not in (va, vb)):
            raise SystemExit(f"two changes overlap at disc offset {b:#x}")


def write_patch(path, patches):
    """The ranges [(disc offset, bytes, variant)] written as a version 2 patch (checked for overlaps first)"""
    check_overlaps(patches)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, 2, len(patches)))
        for off, data, variant in patches:
            f.write(struct.pack("<QII", off, len(data), variant) + data)


def write_patch_v3(path, entries):
    """A version 3 patch (tools/bh_vdisc.py): entries [(disc offset, ("data", bytes) or ("copy", source offset, length), variant)] (the variant may be left out: 0): a copy range
    is laid from the image itself by the runtime, so a whole moved file costs a few entries, not its bytes. Entries are laid in the order given"""
    entries = [(e[0], e[1], e[2] if len(e) > 2 else 0) for e in entries]
    with open(path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, 3, len(entries)))
        for offset, what, variant in entries:
            if what[0] == "data":
                f.write(struct.pack("<QII", offset, len(what[1]), variant) + what[1])
            else:
                f.write(struct.pack("<QII", offset, what[2], variant | 0x100) + struct.pack("<Q", what[1]))


def read_patch(path):
    """[(disc offset, bytes, variant)] of a version 1 or 2 patch file (the kind the tools write for a patch in place)"""
    with open(path, "rb") as f:
        magic, version, count = struct.unpack("<III", f.read(12))
        if magic != MAGIC or version not in (1, 2):
            raise ValueError(f"{path}: not a version 1 or 2 patch")
        out = []
        for _ in range(count):
            offset, length = struct.unpack("<QI", f.read(12))
            variant = struct.unpack("<I", f.read(4))[0] if version == 2 else 0
            out.append((offset, f.read(length), variant))
    return out
