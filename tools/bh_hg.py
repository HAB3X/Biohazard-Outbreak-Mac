#!/usr/bin/env python3
"""Haunting Ground (Demento) Disc Asset Extractor for Biohazard Outbreak and Studio.

Extracts genuine 3D character meshes (Debilitas, Fiona, Hewie, Daniella, Riccardo,
Lorenzo, Golem) and authentic textures directly from Haunting Ground PS2 ISO (DATA.CVM).
Integrates with decompilation tooling to provide:
1. Outbreak Studio Three.js JSON viewport format
2. Outbreak Game Engine Capcom NBD kind 4 mesh nodes
3. PS2 CLUT unswizzled 8-bit paletted textures (PNG and SLD TIM2)
"""
import base64
import io
import math
import struct
import zlib
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

import bh_nbd as nbd
import bh_sld
import bh_ps2_games as ps2

POS_SCALE = 1.0 / 4096.0
UNIT = 1.0 / 32768.0

HG_ID_MAP = {
    "hg_debilitas": ("O_DB0/DB0_000.PCK", "O_DB0/DB0_000.TEX"),
    "hg_fiona": ("O_FIN/FIN_000.PCK", "O_FIN/FIN_000.TEX"),
    "hg_hewie": ("O_HEW/HEW_000.PCK", "O_HEW/HEW_000.TEX"),
    "hg_daniella": ("O_DNL/DNL_000.PCK", "O_DNL/DNL_000.TEX"),
    "hg_riccardo": ("O_RCG/RCG_000.PCK", "O_RCG/RCG_000.TEX"),
    "hg_lorenzo": ("O_LRO/LRO_000.PCK", "O_LRO/LRO_000.TEX"),
    "hg_golem": ("O_GLM/GLM_000.PCK", "O_GLM/GLM_000.TEX"),
    "hg_crow": ("O_CRW/CRW_000.PCK", "O_CRW/CRW_200.PCK"),
    "hg_magnesia": (None, "ITEM00/ITEM_000.TEX"),
    "hg_antidote": (None, "ITEM00/ITEM_001.TEX"),
    "hg_medallion": (None, "ITEM00/ITEM_002.TEX"),
    "hg_boots": (None, "ITEM00/ITEM_003.TEX"),
    "hg_choker": (None, "ITEM00/ITEM_004.TEX"),
    "hg_refine_stone": (None, "ITEM00/ITEM_005.TEX"),
    "hg_castle_key": (None, "ITEM00/ITEM_006.TEX"),
    "hg_sedative": (None, "ITEM00/ITEM_007.TEX"),
    "hg_smelling_salts": (None, "ITEM00/ITEM_008.TEX"),
    "hg_beef_jerky": (None, "ITEM00/ITEM_009.TEX"),
    "hg_magnesia_slate": (None, "SUBSCR/PLATE.TEX"),
    "hg_silver_plate": (None, "SUBSCR/PLATE.TEX"),
}

_CVM_INDEX_CACHE = None
_MODEL_CACHE = {}


def _index_cvm():
    global _CVM_INDEX_CACHE
    if _CVM_INDEX_CACHE is not None:
        return _CVM_INDEX_CACHE

    iso_path = ps2.GAME_ISOS.get("haunting_ground")
    if not iso_path or not iso_path.exists():
        _CVM_INDEX_CACHE = (0, {})
        return _CVM_INDEX_CACHE

    reader = ps2.ISOReader(iso_path)
    cvm_off, _ = reader.find_file("DATA.CVM")
    if cvm_off < 0:
        _CVM_INDEX_CACHE = (0, {})
        return _CVM_INDEX_CACHE

    inner_iso_offset = cvm_off + 0x1800

    def read_dir(lba, size, prefix=""):
        dir_data = reader.read_bytes(inner_iso_offset + lba * 2048, size)
        pos = 0
        files = {}
        while pos < len(dir_data):
            rec_len = dir_data[pos]
            if rec_len == 0:
                pos = ((pos // 2048) + 1) * 2048
                continue
            flba = struct.unpack_from("<I", dir_data, pos + 2)[0]
            fsz = struct.unpack_from("<I", dir_data, pos + 10)[0]
            flags = dir_data[pos + 25]
            name_len = dir_data[pos + 32]
            name = dir_data[pos + 33 : pos + 33 + name_len].decode("latin-1").split(";")[0]
            if name not in ("\x00", "\x01"):
                full_path = f"{prefix}/{name}" if prefix else name
                if flags & 2:
                    files.update(read_dir(flba, fsz, full_path))
                else:
                    files[full_path] = (flba, fsz)
            pos += rec_len
        return files

    pvd = reader.read_bytes(inner_iso_offset + 16 * 2048, 2048)
    root_record = pvd[156 : 156 + 34]
    root_lba = struct.unpack_from("<I", root_record, 2)[0]
    root_sz = struct.unpack_from("<I", root_record, 10)[0]
    all_files = read_dir(root_lba, root_sz)
    _CVM_INDEX_CACHE = (inner_iso_offset, all_files)
    return _CVM_INDEX_CACHE


def read_file_from_cvm(path_in_cvm):
    """Reads a file directly from Haunting Ground DATA.CVM."""
    inner_iso_offset, files = _index_cvm()
    if path_in_cvm not in files:
        return None
    flba, fsz = files[path_in_cvm]
    iso_path = ps2.GAME_ISOS.get("haunting_ground")
    reader = ps2.ISOReader(iso_path)
    return reader.read_bytes(inner_iso_offset + flba * 2048, fsz)


def read_model(d: bytes):
    """Parses Haunting Ground .PCK model into bones and mesh parts."""
    count = struct.unpack_from("<I", d, 0)[0]
    res = struct.unpack_from(f"<{count}I", d, 4)
    r0 = res[0]
    nbones, mesh_off = struct.unpack_from("<iI", d, r0)
    bones = []
    for i in range(nbones):
        b = r0 + 0x10 + i * 0x70
        parent = struct.unpack_from("<i", d, b)[0]
        rest_rot = struct.unpack_from("<3f", d, b + 0x10)
        rest_pos = struct.unpack_from("<3f", d, b + 0x20)
        ibm = list(struct.unpack_from("<16f", d, b + 0x30))
        bones.append((parent, ibm, rest_rot, rest_pos))

    mt = r0 + mesh_off
    nparts = struct.unpack_from("<I", d, mt)[0]
    parts = []
    for i in range(nparts):
        b = mt + 0x10 + i * 0x30
        n, o_pos, o_uv, o_nrm, o_w, o_bi, o_fl, tex, flags, npal, o_pal, infl = struct.unpack_from("<12I", d, b)
        if n == 0:
            continue
        pal = list(d[b + o_pal : b + o_pal + npal])
        p = list(struct.unpack_from("<3i", d, b + o_pos))
        verts = []
        for k in range(n):
            dp = struct.unpack_from("<3h", d, b + o_pos + 0x10 + k * 6)
            p = [p[j] + dp[j] for j in range(3)]
            uv = struct.unpack_from("<2H", d, b + o_uv + k * 4)
            nrm = struct.unpack_from("<3h", d, b + o_nrm + k * 6)
            w = struct.unpack_from(f"<{infl}H", d, b + o_w + k * 2 * infl)
            bi = d[b + o_bi + k * infl : b + o_bi + (k + 1) * infl]
            fl = d[b + o_fl + k]
            joints = [pal[x // 4] for x in bi]
            weights = [x * UNIT for x in w]
            if sum(weights) == 0.0:
                weights[0] = 1.0
            verts.append(dict(
                pos=[v * POS_SCALE for v in p],
                uv=[uv[0] * UNIT, uv[1] * UNIT],
                nrm=[v * UNIT for v in nrm],
                joints=joints, weights=weights, flag=fl))
        parts.append(dict(tex=tex, flags=flags, verts=verts))

    # Rigid parts (eyeballs, teeth, weapons)
    rt = r0 + struct.unpack_from("<I", d, r0 + 8)[0]
    for i in range(struct.unpack_from("<I", d, rt)[0]):
        b = rt + 0x10 + i * 0x20
        n, o_pos, o_uv, o_nrm, o_fl, tex, _, bone = struct.unpack_from("<8i", d, b)
        p = list(struct.unpack_from("<3i", d, b + o_pos))
        verts = []
        for k in range(n):
            dp = struct.unpack_from("<3h", d, b + o_pos + 0x10 + k * 6)
            p = [p[j] + dp[j] for j in range(3)]
            uv = struct.unpack_from("<2H", d, b + o_uv + k * 4)
            nrm = struct.unpack_from("<3h", d, b + o_nrm + k * 6)
            verts.append(dict(pos=[v * POS_SCALE for v in p], uv=[uv[0] * UNIT, uv[1] * UNIT],
                              nrm=[v * UNIT for v in nrm], joints=[bone], weights=[1.0],
                              flag=d[b + o_fl + k]))
        parts.append(dict(tex=tex, flags=0, verts=verts))

    # Morphing parts (faces, hands)
    if len(res) > 1 and res[1]:
        r1 = res[1]
        for i in range(struct.unpack_from("<I", d, r1)[0]):
            b = r1 + 0x10 + i * 0x40
            nshape, n, o_uv, o_fl, o_shapes, bone, tex, kind, flags = struct.unpack_from("<9i", d, b)
            base = struct.unpack_from("<3i", d, b + 0x30)
            shapes = []
            for k in range(nshape):
                e = b + o_shapes + 8 * k
                po, no = struct.unpack_from("<2i", d, e)
                pos = [[(base[j] + v[j]) * POS_SCALE for j in range(3)]
                       for v in struct.iter_unpack("<3h", d[e + po : e + po + 6 * n])]
                nrm = [[c * UNIT for c in v] for v in struct.iter_unpack("<3h", d[e + no : e + no + 6 * n])]
                shapes.append((pos, nrm))
            verts = []
            for k in range(n):
                uv = struct.unpack_from("<2H", d, b + o_uv + k * 4)
                fl = (1 if struct.unpack_from("<I", d, b + o_fl + k * 4)[0] & 0x8000 else 0) if kind != 0 else (d[b + o_fl + k] & 1)
                verts.append(dict(pos=shapes[0][0][k], uv=[uv[0] * UNIT, uv[1] * UNIT], nrm=shapes[0][1][k],
                                  joints=[bone], weights=[1.0], flag=fl))
            parts.append(dict(tex=tex, flags=flags, verts=verts, shapes=shapes[1:], name=f"morph_{i}"))

    return bones, parts


def strip_triangles(verts):
    """Generates indexed triangles from vertex strip flags."""
    tris = []
    for k in range(2, len(verts)):
        if verts[k]["flag"]:
            continue
        a, b, c = k - 2, k - 1, k
        pa, pb, pc = verts[a]["pos"], verts[b]["pos"], verts[c]["pos"]
        e1 = [pb[j] - pa[j] for j in range(3)]
        e2 = [pc[j] - pa[j] for j in range(3)]
        fn = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        vn = [verts[a]["nrm"][j] + verts[b]["nrm"][j] + verts[c]["nrm"][j] for j in range(3)]
        if sum(fn[j] * vn[j] for j in range(3)) < 0:
            b, c = c, b
        tris.append((a, b, c))
    return tris


def read_textures(d: bytes):
    """Decodes Haunting Ground .TEX into PNG byte buffers."""
    count = struct.unpack_from("<I", d, 0)[0]
    out = []
    for i in range(count):
        r = 0x10 + i * 0x10
        psm, w, h, _, _, off = struct.unpack_from("<IHHHHI", d, r)
        if psm != 0x13:
            continue
        px = d[r + off : r + off + w * h]
        clut = d[r + off + w * h : r + off + w * h + 1024]
        pal = []
        for j in range(256):
            s = (j & ~0x18) | ((j & 0x08) << 1) | ((j & 0x10) >> 1)
            cr, cg, cb, ca = clut[s * 4 : s * 4 + 4]
            pal.append(bytes((cr, cg, cb, min(255, ca * 2))))
        solid = [c[:3] + b"\xff" for c in pal]
        rows_solid = b"".join(b"\0" + b"".join(solid[x] for x in px[y * w : (y + 1) * w]) for y in range(h))
        out.append(_raw_to_png(w, h, rows_solid))
    return out


def _raw_to_png(w, h, raw_rows):
    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw_rows, 9)) + chunk(b"IEND", b""))


def get_asset(item_id):
    """Loads and caches parsed Haunting Ground 3D asset."""
    if item_id in _MODEL_CACHE:
        return _MODEL_CACHE[item_id]

    paths = HG_ID_MAP.get(item_id)
    if not paths:
        return None

    pck_path, tex_path = paths
    pck_bytes = read_file_from_cvm(pck_path) if pck_path else None
    tex_bytes = read_file_from_cvm(tex_path) if tex_path else None

    bones, parts = read_model(pck_bytes) if pck_bytes else ([], [])
    textures = read_textures(tex_bytes) if tex_bytes else []

    asset = {
        "bones": bones,
        "parts": parts,
        "textures": textures,
    }
    _MODEL_CACHE[item_id] = asset
    return asset


def get_model_json(item_id):
    """Returns Outbreak Studio Three.js JSON representation of the real Haunting Ground mesh."""
    asset = get_asset(item_id)
    if not asset or not asset.get("parts"):
        return None

    meshes_out = []
    for mi, p in enumerate(asset["parts"]):
        verts = p["verts"]
        if not verts:
            continue
        tris = strip_triangles(verts)
        if not tris:
            continue

        flat_p = [coord for v in verts for coord in v["pos"]]
        flat_uv = [coord for v in verts for coord in v["uv"]]
        flat_c = [1.0, 1.0, 1.0, 1.0] * len(verts)
        flat_idx = [idx for t in tris for idx in t]

        meshes_out.append({
            "mesh": mi,
            "bone": p.get("verts", [{}])[0].get("joints", [0])[0] if verts else 0,
            "additive": False,
            "c": base64.b64encode(struct.pack(f"<{len(flat_c)}f", *flat_c)).decode(),
            "p": base64.b64encode(struct.pack(f"<{len(flat_p)}f", *flat_p)).decode(),
            "uv": base64.b64encode(struct.pack(f"<{len(flat_uv)}f", *flat_uv)).decode(),
            "groups": [{"m": p.get("tex", 0), "i": base64.b64encode(struct.pack(f"<{len(flat_idx)}I", *flat_idx)).decode()}],
        })

    bones_out = []
    for b in asset.get("bones", []):
        parent, ibm, rest_rot, rest_pos = b
        bones_out.append([parent, [1.0, 1.0, 1.0], list(rest_rot), list(rest_pos)])
    if not bones_out:
        bones_out = [[-1, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]]

    return {
        "bones": bones_out,
        "meshes": meshes_out,
        "tids": [f"haunting_ground:{item_id}:0"],
        "objects": len(meshes_out),
    }


def get_texture_png(item_id):
    """Returns authentic PNG texture bytes for Studio texture rendering."""
    asset = get_asset(item_id)
    if not asset or not asset.get("textures"):
        return None
    return asset["textures"][0]


def get_nbd_mesh_node(item_id, material=0, template=None):
    """Generates Outbreak Capcom NBD kind 4 mesh node from real Haunting Ground geometry."""
    asset = get_asset(item_id)
    if not asset or not asset.get("parts"):
        return None

    all_pos = []
    all_uv = []
    all_tris = []
    v_offset = 0

    for p in asset["parts"]:
        verts = p["verts"]
        if not verts:
            continue
        tris = strip_triangles(verts)
        for v in verts:
            px, py, pz = v["pos"]
            all_pos.append((px * 100.0, py * 100.0, pz * 100.0))
            all_uv.append(tuple(v["uv"]))
        for t in tris:
            all_tris.append((v_offset + t[0], v_offset + t[1], v_offset + t[2]))
        v_offset += len(verts)

    if not all_pos or not all_tris:
        return None

    if template is None:
        p11 = struct.pack("<III", 11 << 16, 1, 28) + struct.pack("<4f", 128.0, 128.0, 128.0, 255.0)
        p15 = struct.pack("<III", 15 << 16, 1, 84) + bytes(72)
        template = struct.pack("<III", 4, 2, 12 + len(p11) + len(p15)) + p11 + p15

    return nbd.make_mesh_node(template, all_pos, all_uv, all_tris, material=material)


def get_sld_texture(item_id):
    """Encodes authentic Haunting Ground texture into Outbreak TIM2 / SLD format."""
    png_bytes = get_texture_png(item_id)
    if not png_bytes:
        return None

    img = Image.open(io.BytesIO(png_bytes)).convert("RGBA")
    w, h = img.size
    q = img.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
    pal_rgb = q.getpalette()[:768]
    raw_pixels = bytes(q.getdata())

    clut = bytearray(1024)
    for i in range(256):
        r = pal_rgb[i * 3]
        g = pal_rgb[i * 3 + 1]
        b = pal_rgb[i * 3 + 2]
        a = 128
        swz = (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)
        struct.pack_into("<4B", clut, 4 * swz, r, g, b, a)

    header_size = 48
    img_size = len(raw_pixels)
    clut_size = len(clut)
    total_size = 16 + header_size + img_size + clut_size

    file_header = struct.pack("<4sBBxxII", b"TIM2", 4, 1, 0, 0)
    pic_header = struct.pack("<IIIHHBBBBHH",
                             total_size - 16, clut_size, img_size, header_size,
                             256, 0, 1, 3, 5, w, h)
    tim2 = file_header + pic_header + bytes(header_size - len(pic_header)) + raw_pixels + clut
    return bh_sld.pack(tim2)
