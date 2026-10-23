#!/usr/bin/env python3
"""A room's collision: the walls (w0NNMM00.bin) and the ground (g0NNMM00.bin), read and written.

    bh_collision.py GAME SCENARIO ROOMFILE     a summary of the room's two collision files

Both files are the same: triangles in a grid over the room's ground (File 2's first scenario, measured in the game and read from the data).
  head, 40 bytes: the magic 'HITS' (0x53544948), the file's size, the cell's width and depth (500 or 600), the cells across (nx) and down (nz), the grid's corner x and z,
                  32 (a flag word), and where the triangles begin (`data`)
  then nx * nz words: for each cell (column by column: the cell's index is x * nz + z) the offset of its list, from 8 bytes before the end of this table
  the lists: 4 byte offsets of triangles (from the first triangle) ending in -1; a cell's list may be the tail of another's (the files share them)
  at `data`: -1, -1 and then the triangles, 56 bytes each: a flag word (walls 0x2F0000, ground 0), three corners (x, y, z floats), the plane's normal (3 floats) and its offset d (n . p = d)
The walls are standing triangles (a wall segment is two, from y -100 up to 800 in the zoo's room 4); the ground is flat or sloped triangles.
"""
import math
import struct
import sys

MAGIC = 0x53544948   # 'HITS'
TRIANGLE = 56


class Triangle:
    __slots__ = ("flags", "p", "normal", "d")

    def __init__(self, flags, p, normal=None, d=None):
        self.flags = flags
        self.p = [tuple(map(float, v)) for v in p]
        if normal is None:
            normal, d = plane(self.p)
        self.normal, self.d = tuple(normal), d

    def box(self):
        xs = [v[0] for v in self.p]
        zs = [v[2] for v in self.p]
        return min(xs), min(zs), max(xs), max(zs)


def _overlaps(tri, x0, z0, x1, z1):
    """Whether the triangle (its x and z) and the box share ground: the triangle's box overlaps and no side of either separates them"""
    pts = [(v[0], v[2]) for v in tri]
    if max(p[0] for p in pts) < x0 or min(p[0] for p in pts) > x1 or max(p[1] for p in pts) < z0 or min(p[1] for p in pts) > z1:
        return False
    corners = [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]
    for i in range(3):
        a, b = pts[i], pts[(i + 1) % 3]
        edge = (b[0] - a[0], b[1] - a[1])
        axis = (-edge[1], edge[0])
        tri_side = [axis[0] * (p[0] - a[0]) + axis[1] * (p[1] - a[1]) for p in pts]
        box_side = [axis[0] * (c[0] - a[0]) + axis[1] * (c[1] - a[1]) for c in corners]
        if max(box_side) < min(tri_side) or min(box_side) > max(tri_side):
            return False
    return True


def plane(p):
    a, b, c = p
    u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
    v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    length = math.sqrt(sum(x * x for x in n)) or 1.0
    n = tuple(x / length for x in n)
    return n, n[0] * a[0] + n[1] * a[1] + n[2] * a[2]


class Collision:
    def __init__(self, data=None):
        self.cell = (500, 500)
        self.size = (1, 1)
        self.origin = (0, 0)
        self.flag_word = 32
        self.triangles = []
        if data:
            self.parse(bytes(data))

    def parse(self, data):
        magic, _, cw, cd, nx, nz, ox, oz, flag_word, start = struct.unpack_from("<10i", data, 0)
        if magic != MAGIC:
            raise ValueError("not a collision file")
        self.cell, self.size, self.origin, self.flag_word = (cw, cd), (nx, nz), (ox, oz), flag_word
        first = start + 8
        seen = {}
        triangles = []
        index = {}
        for c in range(nx * nz):
            at = struct.unpack_from("<i", data, 40 + 4 * c)[0] + 8
            while True:
                off = struct.unpack_from("<i", data, at)[0]
                at += 4
                if off == -1:
                    break
                if off not in index:
                    rec = struct.unpack_from("<I13f", data, first + off)
                    index[off] = len(triangles)
                    triangles.append(Triangle(rec[0], [rec[1:4], rec[4:7], rec[7:10]], rec[10:13], rec[13]))
        self.triangles = triangles

    def tobytes(self):
        """The file for the triangles, the grid (cell size, corner, counts) as set; triangles outside the grid are left out of the lists but kept in the file"""
        nx, nz = self.size
        cw, cd = self.cell
        ox, oz = self.origin
        cells = [[] for _ in range(nx * nz)]
        for t_index, t in enumerate(self.triangles):
            x0, z0, x1, z1 = t.box()
            for cz in range(max(0, int((z0 - oz) // cd)), min(nz - 1, int((z1 - oz) // cd)) + 1):
                for cx in range(max(0, int((x0 - ox) // cw)), min(nx - 1, int((x1 - ox) // cw)) + 1):
                    if _overlaps(t.p, ox + cx * cw, oz + cz * cd, ox + (cx + 1) * cw, oz + (cz + 1) * cd):
                        cells[cx * nz + cz].append(t_index * TRIANGLE)
        table, lists, where = [], bytearray(), {}
        for lst in cells:   # every cell has a list of its own, laid one after another (the game's files do; sharing them crashed it)
            table.append(len(lists))
            lists += struct.pack(f"<{len(lst) + 1}i", *lst, -1)
        table_end = 40 + 4 * nx * nz
        table = [x + table_end - 8 for x in table]
        start = table_end + len(lists)
        body = struct.pack("<2i", -1, -1) + b"".join(struct.pack("<I13f", t.flags, *[c for v in t.p for c in v], *t.normal, t.d) for t in self.triangles)
        size = start + len(body)
        head = struct.pack("<10i", MAGIC, size, cw, cd, nx, nz, ox, oz, self.flag_word, start)
        return head + struct.pack(f"<{nx * nz}i", *table) + bytes(lists) + body

    def bounds(self):
        xs = [v[0] for t in self.triangles for v in t.p]
        zs = [v[2] for t in self.triangles for v in t.p]
        return (min(xs), min(zs), max(xs), max(zs)) if xs else None


def segments(collision):
    """([{"a": (x, z), "b": (x, z), "y": [low, high], "flags": n, "n": (nx, nz)}], the other triangles): the standing triangles of the walls as wall segments (two triangles make one),
    the rest (slopes, ceilings) left as they are"""
    segs, others = {}, []
    for t in collision.triangles:
        pts = {(round(v[0], 2), round(v[2], 2)) for v in t.p}
        if len(pts) == 2 and abs(t.normal[1]) < 0.2:
            a, b = sorted(pts)
            y0, y1 = min(v[1] for v in t.p), max(v[1] for v in t.p)
            s = segs.setdefault((a, b), {"a": a, "b": b, "y": [y0, y1], "flags": t.flags, "n": (t.normal[0], t.normal[2])})
            s["y"] = [min(s["y"][0], y0), max(s["y"][1], y1)]
        else:
            others.append(t)
    return list(segs.values()), others


def wall_triangles(seg):
    """The two standing triangles of a wall segment, wound so the plane's normal is the segment's "n" (the side that is open)"""
    (ax, az), (bx, bz) = seg["a"], seg["b"]
    y0, y1 = seg["y"]
    one = [(ax, y0, az), (ax, y1, az), (bx, y0, bz)]
    two = [(ax, y1, az), (bx, y1, bz), (bx, y0, bz)]
    out = []
    for tri in (one, two):
        n, _ = plane(tri)
        if n[0] * seg["n"][0] + n[2] * seg["n"][1] < 0:
            tri = [tri[0], tri[2], tri[1]]
        out.append(Triangle(int(seg.get("flags", 0x2F0000)), tri))
    return out


def main(argv):
    if len(argv) != 4:
        print(__doc__)
        return 1
    import bh_archive as archive

    pack = archive.Archive(argv[1]).open(f"/NETBIO00.DAT/r{int(argv[2]):03d}.afs")
    members = dict(pack.members())
    room = argv[3]
    for prefix in ("w", "g"):
        name = f"{prefix}{room[1:]}.bin" if room.startswith("r") else room
        if name not in members:
            continue
        c = Collision(members[name].read())
        print(f"{name}: {len(c.triangles)} triangles, grid {c.size[0]}x{c.size[1]} cells of {c.cell}, corner {c.origin}, bounds {c.bounds()}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
