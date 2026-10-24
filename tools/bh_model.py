#!/usr/bin/env python3
"""A room model (.nbd) taken apart and put back together, so objects and the pictures they need can be added: the chunk table, the TID chunk (a count and the pack's picture numbers),
the AMO chunk (a tree: the meshes' list, and the materials' two lists, 272 and 268 byte records, one of each for every material), the AHI chunk (the skeleton).

    m = Model(data); m.import_object(source_model, mesh_number, offset) -> the new object's number; m.to_bytes()

Measured in the game (docs/research/rdt-format.md): a copied object needs its bone linked into the skeleton's chain and its own object number in the bone; a material is a TID entry
(the picture), a 272 byte record in the AMO's kind 9 list and a 268 byte record in its kind 10 list (the TID entry's number, the picture's width and height).
"""
import struct

import bh_nbd as nbd

ALIGN = 64


def _align(n):
    return (n + ALIGN - 1) & ~(ALIGN - 1)


def _u32(b, at):
    return struct.unpack_from("<I", b, at)[0]


def _children(node):
    """[bytearray] of a list node's children (a node of 12 bytes and then nodes, each carrying its own size)"""
    out, at = [], 12
    while at + 12 <= len(node):
        size = _u32(node, at + 8)
        out.append(bytearray(node[at:at + size]))
        at += size
    return out


def _list(flags, children):
    body = b"".join(children)
    return bytearray(struct.pack("<III", flags, len(children), 12 + len(body)) + body)


class Model:
    def __init__(self, data):
        data = bytes(data)
        self.table = nbd.chunks(data)
        self.head = bytearray(data[:128])
        self.bodies = [bytearray(data[o:o + s]) for _, o, s, _ in self.table]
        end = max(o + s for _, o, s, _ in self.table)
        self.tail = bytearray(data[end:])
        names = [c[0] for c in self.table]
        self.i_tid = names.index("TID") if "TID" in names else None
        self.i_amo = names.index("AMO")
        self.i_ahi = names.index("AHI")
        root = self.bodies[self.i_amo]
        self.root_flags = _u32(root, 0)
        self.kids = _children(root)
        self.tid = list(struct.unpack_from(f"<{len(self.bodies[self.i_tid]) // 4}I", self.bodies[self.i_tid])) if self.i_tid is not None else [0]

    # ---- the parts
    def _kid(self, kind):
        for i, k in enumerate(self.kids):
            flags = _u32(k, 0)
            if flags & 0xFFFF == kind and (flags >> 16) & 0xFF == 0:
                return i
        raise ValueError(f"the model has no list of kind {kind}")

    def meshes(self):
        return _children(self.kids[self._kid(2)])

    def materials(self):
        """([kind 9 records], [kind 10 records])"""
        return _children(self.kids[self._kid(9)]), _children(self.kids[self._kid(10)])

    def texture_ref(self, material):
        """The kind 10 record (texture reference) a material uses: the last word of its 272 byte kind 9 record (the two lists are not parallel: measured, the
        number differs from the material's own in 927 of File 1's 973 materials, and read this way the bar's floor gets its wood and its counter its dark wood, as the game shows)"""
        return _u32(self.materials()[0][material], 268)

    def picture_of(self, material):
        n = _u32(self.materials()[1][self.texture_ref(material)], 12)
        return self.tid[1 + n]

    def pictures(self):
        return self.tid[1:1 + self.tid[0]]

    def material_for(self, picture, like9=None, size=(64, 64)):
        """The number of a material showing `picture` (one that has it, or a new one made from `like9`, a 272 byte kind 9 record, and `size`)"""
        nine, ten = self.materials()
        for m, rec in enumerate(nine):
            if self.picture_of(m) == picture and (like9 is None or bytes(rec[:268]) == bytes(like9[:268])):
                return m
        if picture not in self.tid[1:1 + self.tid[0]]:
            self.tid.append(picture)
            self.tid[0] += 1
        n = self.tid.index(picture, 1) - 1
        new10 = bytearray(ten[0])
        struct.pack_into("<3I", new10, 12, n, size[0], size[1])
        ten.append(new10)
        new9 = bytearray(like9 if like9 is not None else nine[0])
        struct.pack_into("<I", new9, 268, len(ten) - 1)      # the new material names the new texture reference
        nine.append(new9)
        self.kids[self._kid(9)] = _list(_u32(self.kids[self._kid(9)], 0), nine)
        self.kids[self._kid(10)] = _list(_u32(self.kids[self._kid(10)], 0), ten)
        return len(nine) - 1

    # ---- the skeleton
    def _bones(self):
        body = self.bodies[self.i_ahi]
        out, at = [], 12
        while at + 12 <= len(body):
            flags, count, size = struct.unpack_from("<III", body, at)
            if size < 12 or at + size > len(body):
                break
            if flags & 0xF0000000 == 0x40000000 and size >= 12 + 64:
                out.append(at + 12)       # where the bone's 256 byte record starts (its node's header is before it)
            at += size
        return out

    def add_object(self, node, offset=None, like_bone=None, identity=False, absolute=False):
        """A mesh node appended as the last object on a new bone (a copy of bone `like_bone`'s, plain when `identity`, moved on by `offset`), linked into the skeleton's chain"""
        meshes = self.meshes()
        number = len(meshes)
        meshes.append(bytearray(node))
        self.kids[self._kid(2)] = _list(_u32(self.kids[self._kid(2)], 0), meshes)
        body = self.bodies[self.i_ahi]
        records = self._bones()
        like = records[(like_bone if like_bone is not None else 2)]
        bone = bytearray(body[like - 12:like + 256])
        new_index = len(records)
        struct.pack_into("<i", bone, 12, new_index)
        struct.pack_into("<i", bone, 12 + 8, -1)
        struct.pack_into("<i", bone, 12 + 12, -1)
        struct.pack_into("<i", bone, 12 + 64, number)
        if identity:
            struct.pack_into("<4f", bone, 12 + 16, 1.0, 1.0, 1.0, 1.0)
            struct.pack_into("<4f", bone, 12 + 32, 0.0, 0.0, 0.0, 1.0)
        if offset is not None:
            old = (0.0, 0.0, 0.0) if absolute else struct.unpack_from("<3f", bone, 12 + 48)
            struct.pack_into("<3f", bone, 12 + 48, *(a + b for a, b in zip(old, offset)))
        parent = struct.unpack_from("<i", bone, 12 + 4)[0]
        last = struct.unpack_from("<i", body, records[parent] + 8)[0]
        while struct.unpack_from("<i", body, records[last] + 12)[0] != -1:
            last = struct.unpack_from("<i", body, records[last] + 12)[0]
        struct.pack_into("<i", body, records[last] + 12, new_index)
        body += bone
        struct.pack_into("<I", body, 4, _u32(body, 4) + 1)
        struct.pack_into("<I", body, 8, len(body))
        return number

    def import_object(self, source, mesh, offset=None, absolute=False):
        """Object `mesh` of another model (`source`, a Model) copied in with the pictures it uses (each found or added here): the new object's number"""
        node = bytearray(source.meshes()[mesh])
        nine, ten = source.materials()
        # the mesh's own material list (its kind 5 array): the numbers are materials of the source model
        at, mapping = 12, {}
        while at + 12 <= len(node):
            flags, count, size = struct.unpack_from("<III", node, at)
            if (flags >> 16) & 0xFF == 5:
                for k in range(count):
                    m = _u32(node, at + 12 + 4 * k)
                    width, height = struct.unpack_from("<2I", ten[source.texture_ref(m)], 16)
                    mapping[m] = self.material_for(source.picture_of(m), nine[m], (width, height))
                    struct.pack_into("<I", node, at + 12 + 4 * k, mapping[m])
            at += size
        return self.add_object(node, offset, identity=True, absolute=absolute)

    # ---- back to bytes
    def to_bytes(self):
        tid = struct.pack(f"<{len(self.tid)}I", *self.tid)
        root = bytearray(struct.pack("<III", self.root_flags, len(self.kids), 12 + sum(len(k) for k in self.kids)) + b"".join(self.kids))
        bodies = list(self.bodies)
        if self.i_tid is not None:
            bodies[self.i_tid] = bytearray(tid)
        bodies[self.i_amo] = root
        out = bytearray(self.head)
        entries = []
        for body in bodies:
            out += bytes(_align(len(out)) - len(out))
            entries.append(len(out))
            out += body
        for i, (name, _, _, count) in enumerate(self.table):
            struct.pack_into("<III", out, 16 * i + 4, entries[i], len(bodies[i]), count)
        return bytes(out + self.tail)
