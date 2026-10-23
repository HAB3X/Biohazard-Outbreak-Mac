#!/usr/bin/env python3
"""Reads the meshes of the games' .nbd model files (characters, objects and rooms): the positions and the triangle strips.

    bh_nbd.py GAME ARCHIVE_PATH      the chunk table and each AMO chunk's meshes (counts and bounds), e.g. file2 /NETBIO00.DAT/r040.afs/r0400300.nbd

The file starts with a table of 16 byte entries (four-character name, offset, size, count) up to the first empty one; the AMO
chunks hold the meshes. An AMO chunk is a tree of nodes of 12 bytes [flags, count, size (header included)] then the contents: a
node whose flags' bits 16..23 are 0 holds `count` child nodes (its kind is flags & 0xFFFF), otherwise it is an array of that type.
A mesh is a kind 4 node. Its arrays: type 7 positions (x y z floats), 8 normals, 10 uv; the triangle strips are in the type 3 and 4
arrays as runs of [n, then n vertex indices]. This is the same reading as launcher/Sources/Launcher/CharacterModel.swift (written
from the files and checked against reo's names for the arrays: reference only); here it is for rooms, whose positions are
checked against the room file's coordinates (tools/bh_rdt.py) in docs/research/rdt-format.md.
"""
import math
import struct
import sys

import bh_archive as archive


def chunks(data):
    """[(name, offset, size, count)] of the table at the file's start"""
    out = []
    for at in range(0, 0x80, 16):
        name = data[at:at + 4].split(b"\0")[0].decode("latin-1")
        offset, size, count = struct.unpack_from("<III", data, at + 4)
        if not name:
            break
        out.append((name, offset, size, count))
    return out


class Mesh:
    def __init__(self, positions, strips, palette, uvs=None, strip_materials=None, positions_at=0):
        self.positions = positions
        self.positions_at = positions_at  # where the positions' floats start in the file
        self.strips = strips
        self.palette = palette  # the skeleton's bones the mesh's vertices name (their weights' bone numbers index it)
        self.uvs = uvs or []
        self.strip_materials = strip_materials or [0] * len(strips)  # the model's material of each strip (an index into the .nbd's textures)

    def textured_triangles(self):
        """[(a, b, c, material)]: the strips as triangles, the winding alternated as a strip's is, each with its strip's material"""
        out = []
        for strip, material in zip(self.strips, self.strip_materials):
            for i in range(len(strip) - 2):
                a, b, c = strip[i], strip[i + 1], strip[i + 2]
                if a != b and b != c and a != c:
                    out.append(((a, b, c) if i % 2 == 0 else (b, a, c)) + (material,))
        return out

    def triangles(self):
        """The strips as triangles of vertex indices, the winding alternated as a strip's is"""
        out = []
        for strip in self.strips:
            for i in range(len(strip) - 2):
                a, b, c = strip[i], strip[i + 1], strip[i + 2]
                if a != b and b != c and a != c:
                    out.append((a, b, c) if i % 2 == 0 else (b, a, c))
        return out


def _u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def _walk(data, at, end, visit):
    pos = at
    while pos + 12 <= end:
        flags, count, size = struct.unpack_from("<III", data, pos)
        if size < 12 or pos + size > end:
            return
        if (flags >> 16) & 0xFF == 0 and count > 0:
            visit(flags & 0xFFFF, pos, pos + size)
            _walk(data, pos + 12, pos + size, visit)
        pos += size


def _arrays(data, at, end):
    """{type: [(start, count, bytes)]} directly in a mesh node and one level under it"""
    out = {}

    def scan(start, stop, depth):
        pos = start
        while pos + 12 <= stop:
            flags, count, size = struct.unpack_from("<III", data, pos)
            if size < 12 or pos + size > stop:
                return
            kind = (flags >> 16) & 0xFF
            if kind == 0:
                if depth == 0:
                    scan(pos + 12, pos + size, 1)
            else:
                out.setdefault(kind, []).append((pos + 12, count, size - 12))
            pos += size

    scan(at + 12, end, 0)
    return out


def meshes(data, offset, size):
    """The meshes of the AMO chunk at data[offset:offset + size]"""
    found = []

    def visit(kind, at, end):
        if kind != 4:
            return
        arrays = _arrays(data, at, end)
        if 7 not in arrays:
            return
        start, count, _ = arrays[7][0]
        positions = [struct.unpack_from("<3f", data, start + 12 * i) for i in range(count)]
        strips = []
        for kind_of_strips in (3, 4):
            for s, n, nbytes in arrays.get(kind_of_strips, []):
                pos, stop = s, s + nbytes
                for _ in range(n):
                    if pos + 4 > stop:
                        break
                    length = _u32(data, pos)
                    pos += 4
                    if length > 100000 or pos + 4 * length > stop:
                        break
                    if length >= 3:  # (the launcher's reader skips the shorter ones too: the per-strip materials line up)
                        strips.append(list(struct.unpack_from(f"<{length}I", data, pos)))
                    pos += 4 * length
        palette = []
        if 16 in arrays:
            start16, count16, _ = arrays[16][0]
            palette = list(struct.unpack_from(f"<{count16}I", data, start16))
        uvs = []
        if 10 in arrays:
            start10, count10, _ = arrays[10][0]
            uvs = [struct.unpack_from("<2f", data, start10 + 8 * i) for i in range(min(count10, len(positions)))]
        mesh_materials, per_strip = [0], []
        if 5 in arrays:
            s5, c5, _ = arrays[5][0]
            mesh_materials = list(struct.unpack_from(f"<{c5}I", data, s5))
        if 6 in arrays:
            s6, c6, _ = arrays[6][0]
            per_strip = list(struct.unpack_from(f"<{c6}I", data, s6))
        strip_materials = [mesh_materials[per_strip[i]] if i < len(per_strip) and per_strip[i] < len(mesh_materials) else 0 for i in range(len(strips))]
        found.append(Mesh(positions, strips, palette, uvs, strip_materials, start))

    _walk(data, offset + 0, offset + size, visit)
    return found


def set_vertex(data, mesh, index, position):
    """Writes `position` (x, y, z) over the mesh's vertex `index` in the file's bytes (the same size: a vertex is three floats)"""
    if not 0 <= index < len(mesh.positions):
        raise IndexError(f"vertex {index}: the mesh has {len(mesh.positions)}")
    struct.pack_into("<3f", data, mesh.positions_at + 12 * index, *map(float, position))


def bone_records(data, offset, size):
    """[the file offset of each bone's 256 byte record] of the AHI chunk (skeleton() reads the same nodes)"""
    out = []
    pos = offset + 12
    while pos + 12 <= offset + size:
        flags, count, node_size = struct.unpack_from("<III", data, pos)
        if node_size < 12 or pos + node_size > offset + size:
            break
        if flags & 0xF0000000 == 0x40000000 and node_size >= 12 + 64:
            out.append(pos + 12)
        pos += node_size
    return out


def set_bone(data, record, scale=None, rotation=None, offset=None):
    """The bone record at file offset `record` overwritten in a bytearray `data`: scale (x, y, z), rotation (x, y, z radians),
    offset from its parent (x, y, z); the 4th number of each is left (1)"""
    if scale is not None:
        struct.pack_into("<3f", data, record + 16, *map(float, scale))
    if rotation is not None:
        struct.pack_into("<3f", data, record + 32, *map(float, rotation))
    if offset is not None:
        struct.pack_into("<3f", data, record + 48, *map(float, offset))


def skeleton(data, offset, size):
    """[(parent, scale, rotation, offset)] of the AHI chunk's bones: a 12 byte header, then nodes of 12 bytes and 256 more, those
    flagged 0x4000000n the bones; in a bone's 256: index, parent, first child, next sibling, scale (x y z w), rotation (x y z
    Euler angles in radians, w 1), offset from the parent (x y z w)"""
    bones = []
    pos = offset + 12
    while pos + 12 <= offset + size:
        flags, count, node_size = struct.unpack_from("<III", data, pos)
        if node_size < 12 or pos + node_size > offset + size:
            break
        if flags & 0xF0000000 == 0x40000000 and node_size >= 12 + 64:
            at = pos + 12
            parent = struct.unpack_from("<i", data, at + 4)[0]
            scale = struct.unpack_from("<4f", data, at + 16)
            rotation = struct.unpack_from("<4f", data, at + 32)
            where = struct.unpack_from("<4f", data, at + 48)
            bones.append((parent, scale, rotation, where))
        pos += node_size
    return bones


def _rotate(angles, v):
    """v turned about x, then y, then z (the order isn't established: the angles in the rooms looked at turn about one axis)"""
    x, y, z = angles[0], angles[1], angles[2]
    v = (v[0], v[1] * math.cos(x) - v[2] * math.sin(x), v[1] * math.sin(x) + v[2] * math.cos(x))
    v = (v[0] * math.cos(y) + v[2] * math.sin(y), v[1], -v[0] * math.sin(y) + v[2] * math.cos(y))
    return (v[0] * math.cos(z) - v[1] * math.sin(z), v[0] * math.sin(z) + v[1] * math.cos(z), v[2])


def world(bones):
    """each bone's place as a function of a point in its own space: the chain of scale, rotation and offset up to the root"""
    chains = []
    for index, (parent, scale, rotation, where) in enumerate(bones):
        chain = []
        at = index
        guard = 0
        while 0 <= at < len(bones) and guard < 64:
            chain.append(bones[at])
            at = bones[at][0] if bones[at][0] != at else -1
            guard += 1
        chains.append(chain)

    def place(bone, point):
        for _, scale, rotation, where in chains[bone]:
            point = (point[0] * scale[0], point[1] * scale[1], point[2] * scale[2])
            point = _rotate(rotation, point)
            point = (point[0] + where[0], point[1] + where[1], point[2] + where[2])
        return point

    return place


def room_triangles(data):
    """The triangles of a room .nbd's stage in world coordinates [((x, y, z) x3)]: the first AMO's meshes each placed by a bone
    of the first AHI, mesh i by bone i + 2 (the root and one more come first). Checked on File 2's r0400300: 99.3% of the
    vertices land in the room's floor region from its camera boxes (rooms.md's rooms share one world with the room file)."""
    table = chunks(data)
    amo = next((c for c in table if c[0] == "AMO"), None)
    ahi = next((c for c in table if c[0] == "AHI"), None)
    if not amo or not ahi:
        return []
    parts = meshes(data, amo[1], amo[2])
    place = world(skeleton(data, ahi[1], ahi[2]))
    out = []
    for index, mesh in enumerate(parts):
        bone = index + 2
        if not mesh.positions:
            continue
        try:
            points = [place(bone, p) for p in mesh.positions]
        except IndexError:
            continue
        for a, b, c in mesh.triangles():
            if a < len(points) and b < len(points) and c < len(points):
                out.append((points[a], points[b], points[c]))
    return out


def floors_and_walls(triangles, band=120.0):
    """(floor triangles, wall triangles) of a room's world triangles, seen as a plan: the floor's height is the lowest of the
    heights holding most flat surface (flat: the normal near vertical, either way, as the strips' winding isn't reliable), the
    floors the triangles whose highest corner is within `band` of it, the walls those standing from it to over a band higher;
    ceilings and the rest are left out, and so are triangles with an edge over 3000 (backdrops and skies)"""
    flat = {}
    for t in triangles:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
        ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        length = math.sqrt(nx * nx + ny * ny + nz * nz)
        if length > 1e-6 and abs(ny) / length > 0.8:
            key = round((ay + by + cy) / 3 / 20)
            flat[key] = flat.get(key, 0.0) + length / 2
    if not flat:
        return [], []
    peak = max(flat.values())
    floor = min(key for key, area in flat.items() if area >= 0.4 * peak) * 20.0
    floors, walls = [], []
    for t in triangles:
        if max(math.dist(t[i], t[(i + 1) % 3]) for i in range(3)) > 3000:
            continue  # the long ones are backdrops and skies, not the room
        low, high = min(p[1] for p in t), max(p[1] for p in t)
        if high <= floor + band:
            floors.append(t)
        elif low <= floor + band and high >= floor + 1.25 * band:
            walls.append(t)
    return floors, walls


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 1
    node = archive.Archive(argv[1]).open(argv[2])
    data = node.read()
    for name, offset, size, count in chunks(data):
        print(f"{name} offset {offset} size {size} count {count}")
        if name == "AMO":
            ms = meshes(data, offset, size)
            vertices = sum(len(m.positions) for m in ms)
            triangles = sum(len(m.triangles()) for m in ms)
            xs = [p[0] for m in ms for p in m.positions]
            ys = [p[1] for m in ms for p in m.positions]
            zs = [p[2] for m in ms for p in m.positions]
            print(f"  {len(ms)} meshes, {vertices} vertices, {triangles} triangles"
                  + (f", x {min(xs):.0f}..{max(xs):.0f} y {min(ys):.0f}..{max(ys):.0f} z {min(zs):.0f}..{max(zs):.0f}" if xs else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))


def make_mesh_node(template, positions, uvs, triangles, material, colour=None):
    """A mesh node (a kind 4 node as the file holds them) for the given geometry, laid out like `template`'s: positions [(x, y, z)] in the object's own space, uvs [(u, v)] (v 0 at the picture's top),
    triangles [(a, b, c)] of vertex numbers (each a strip of its own), `material` the picture's number among the model's materials, `colour` (r, g, b, a) per vertex as the game stores them
    (floats up to 255; the template's own average when none). Normals are the triangles' averages at each vertex."""
    n = len(positions)
    if len(uvs) != n:
        raise ValueError("a position needs a uv each")
    normals = [[0.0, 0.0, 0.0] for _ in positions]
    for a, b, c in triangles:
        pa, pb, pc = positions[a], positions[b], positions[c]
        u = [pb[k] - pa[k] for k in range(3)]
        v = [pc[k] - pa[k] for k in range(3)]
        cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
        for i in (a, b, c):
            for k in range(3):
                normals[i][k] += cross[k]
    for nv in normals:
        length = math.sqrt(sum(x * x for x in nv)) or 1.0
        nv[:] = [x / length for x in nv]
    # the template's pieces by kind
    pieces, at = {}, 12
    while at < len(template):
        flags, count, size = struct.unpack_from("<III", template, at)
        pieces[(flags >> 16) & 0xFF] = template[at:at + size]
        at += size
    if colour is None:
        old = pieces[11]
        rows = struct.unpack_from(f"<{(len(old) - 12) // 4}f", old, 12)
        count = len(rows) // 4
        colour = tuple(sum(rows[i * 4 + k] for i in range(count)) / count for k in range(4)) if count else (128.0, 128.0, 128.0, 255.0)

    def node(kind, count, payload):
        return struct.pack("<III", kind << 16, count, 12 + len(payload)) + payload

    strips = b"".join(struct.pack("<4I", 3, a, b, c) for a, b, c in triangles)
    strip_node = struct.pack("<III", 3 << 16, len(triangles), 12 + len(strips)) + strips
    holder = struct.pack("<III", 5, 1, 12 + len(strip_node)) + strip_node
    parts = [holder,
             node(5, 1, struct.pack("<I", material)),
             node(6, len(triangles), bytes(4 * len(triangles))),
             node(7, n, b"".join(struct.pack("<3f", *p) for p in positions)),
             node(8, n, b"".join(struct.pack("<3f", *v) for v in normals)),
             node(10, n, b"".join(struct.pack("<2f", *uv) for uv in uvs)),
             node(11, n, struct.pack("<4f", *colour) * n),
             pieces[15]]
    body = b"".join(parts)
    return struct.pack("<III", 4, 8, 12 + len(body)) + body


def clone_object(data, number, offset=None, geometry=None):
    """The model's bytes with object (mesh) `number` copied as a new last object on a new last bone: a copy of its bone, moved by `offset` (a distance in the parent's space,
    added to the bone's own), linked in as the last of the root's children (the game walks the skeleton by those links). Returns (bytes, the new object's number). The file
    grows: the AMO tree's sizes, the AHI's bone count and size, and the chunk table's later offsets (each chunk on a 64 byte boundary)."""
    data = bytearray(data)
    table = chunks(bytes(data))
    first_amo = next(i for i, c in enumerate(table) if c[0] == "AMO")
    first_ahi = next(i for i, c in enumerate(table) if c[0] == "AHI")
    amo, ahi = table[first_amo], table[first_ahi]
    root = amo[1]
    pos, holder = root + 12, None
    while pos + 12 <= root + amo[2]:
        flags, count, size = struct.unpack_from("<III", data, pos)
        if flags & 0xFFFF == 2 and (flags >> 16) & 0xFF == 0:
            holder = pos
            break
        pos += size
    if holder is None:
        raise ValueError("no mesh list in the model")
    hflags, hcount, hsize = struct.unpack_from("<III", data, holder)
    nodes, pos = [], holder + 12
    for _ in range(hcount):
        nodes.append((pos, struct.unpack_from("<I", data, pos + 8)[0]))
        pos += nodes[-1][1]
    if not 0 <= number < len(nodes):
        raise IndexError(f"object {number}: the model has {len(nodes)}")
    copy = bytes(data[nodes[number][0]:nodes[number][0] + nodes[number][1]])
    if geometry is not None:     # a model of your own in the copy's place: the copy's bone is made plain (no scale or turn) so the model stands as it was made
        copy = make_mesh_node(copy, geometry["positions"], geometry["uvs"], geometry["triangles"], geometry["material"], geometry.get("colour"))
    end_of_list = holder + hsize
    records = bone_records(bytes(data), ahi[1], ahi[2])
    bone_at = records[number + 2] - 12
    bone = bytearray(data[bone_at:bone_at + 12 + 256])
    new_index = len(records)
    struct.pack_into("<i", bone, 12, new_index)
    struct.pack_into("<i", bone, 12 + 8, -1)
    struct.pack_into("<i", bone, 12 + 12, -1)
    struct.pack_into("<i", bone, 12 + 64, hcount)       # the number of the object the bone carries (its own copy's: with the original's number the original lost its picture)
    if geometry is not None:
        struct.pack_into("<4f", bone, 12 + 16, 1.0, 1.0, 1.0, 1.0)
        struct.pack_into("<4f", bone, 12 + 32, 0.0, 0.0, 0.0, 1.0)
    if offset is not None:
        old = struct.unpack_from("<3f", bone, 12 + 48)
        struct.pack_into("<3f", bone, 12 + 48, *(a + b for a, b in zip(old, offset)))
    # the root's last child points on to the new bone
    parent = struct.unpack_from("<i", bone, 12 + 4)[0]
    last = struct.unpack_from("<i", data, records[parent] + 8)[0]
    while struct.unpack_from("<i", data, records[last] + 12)[0] != -1:
        last = struct.unpack_from("<i", data, records[last] + 12)[0]
    struct.pack_into("<i", data, records[last] + 12, new_index)
    align = lambda n: (n + 63) & ~63
    out = bytearray(data[:end_of_list]) + copy + data[end_of_list:amo[1] + amo[2]]
    sizes, shifts, cursor = {first_amo: amo[2] + len(copy)}, {}, amo[1] + amo[2]
    for i, c in enumerate(table):
        if i <= first_amo:
            continue
        out += bytes(align(len(out)) - len(out))
        body = bytearray(data[c[1]:c[1] + c[2]])
        if i == first_ahi:
            body += bone
            struct.pack_into("<I", body, 4, struct.unpack_from("<I", body, 4)[0] + 1)
            struct.pack_into("<I", body, 8, struct.unpack_from("<I", body, 8)[0] + len(bone))
        shifts[i], sizes[i] = len(out), len(body)
        out += body
        cursor = c[1] + c[2]
    out += data[cursor:]
    struct.pack_into("<I", out, holder + 4, hcount + 1)
    struct.pack_into("<I", out, holder + 8, hsize + len(copy))
    struct.pack_into("<I", out, root + 8, struct.unpack_from("<I", out, root + 8)[0] + len(copy))
    for i in range(first_amo, len(table)):
        if i in shifts:
            struct.pack_into("<I", out, 16 * i + 4, shifts[i])
        if i in sizes:
            struct.pack_into("<I", out, 16 * i + 8, sizes[i])
    return bytes(out), hcount
