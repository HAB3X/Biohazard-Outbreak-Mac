"""RE4 (PS2) BIN model reader: skeleton, and per part the vertex batches (VIF V4-16 triplets: position, normal + strip flag,
texture coordinate) laid out as triangle strips"""
import struct
from ps2_vif import vif_codes, dma_chain

def read(b):
    """the same, strips restarted where the flag is set on a vertex (it and the next start a new strip)"""
    njoint_end = struct.unpack_from('<I', b, 0x0C)[0]
    bones = []
    for i in range((njoint_end - 0x50) // 16):
        o = 0x50 + 16 * i
        bones.append((b[o], b[o + 1] if b[o + 1] != 255 else -1, struct.unpack_from('<3f', b, o + 4)))
    offs = []
    k = 0
    while True:
        o = njoint_end + 16 * k
        off = struct.unpack_from('<I', b, o + 12)[0]
        if not off or off >= len(b) or (offs and off <= offs[-1]):
            break
        offs.append(off); k += 1
    parts = []
    for i, start in enumerate(offs):
        end = offs[i + 1] if i + 1 < len(offs) else len(b)
        verts, tris, scale = [], [], 1.0
        for tid, qwc, vw, ps, pe in dma_chain(b, start + 0x20, end):
            for c in vif_codes(b, vw, pe):
                if c[0] != 'UNPACK':
                    continue
                name, n, imm, off, size, cmd = c
                if cmd == 0x6c and (imm & 0x3ff) == 0x20:
                    scale = struct.unpack_from('<f', b, off + 12)[0]
                elif cmd == 0x6d and (imm & 0x3ff) == 0x21:
                    run = 0
                    for j in range(0, n - 2, 3):
                        px, py, pz, pw = struct.unpack_from('<4h', b, off + 8 * j)
                        nx, ny, nz, flag = struct.unpack_from('<4h', b, off + 8 * (j + 1))
                        u, v, q, _ = struct.unpack_from('<4h', b, off + 8 * (j + 2))
                        verts.append(((px * scale, py * scale, pz * scale), (nx / 127, ny / 127, nz / 127), (u, v, q), pw))
                        run = 1 if flag else run + 1
                        if run >= 3:
                            a, bb, cc = len(verts) - 3, len(verts) - 2, len(verts) - 1
                            tris.append((a, bb, cc) if run % 2 else (bb, a, cc))
        parts.append({'offset': start, 'verts': verts, 'tris': tris, 'scale': scale})
    return bones, parts
