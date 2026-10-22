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
