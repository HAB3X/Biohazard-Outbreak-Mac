#!/usr/bin/env python3
"""Silent Hill 2 Disc Model and Texture Extractor for Biohazard Outbreak and Studio.

Extracts genuine 3D character, weapon, item, and monster meshes directly from
Silent Hill 2 Director's Cut PS2 ISO (DATA/CHR.MGF via SLES_511.56 ELF table).
Converts authentic Konami PS2 VIF packet geometry into:
1. Outbreak Studio Three.js JSON viewport format
2. Outbreak Game Engine Capcom NBD kind 4 mesh nodes
3. CSM1 unswizzled 512x512/128x128 8-bit paletted textures (PNG and SLD TIM2)
"""
import base64
import io
import struct
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent

import bh_nbd as nbd
import bh_sld
import bh_ps2_games as ps2

# Catalog item ID mapping to internal SH2 .mdl file paths
SH2_ID_MAP = {
    "sh2_pyramid_head": "data/chr/red/red.mdl",
    "sh2_james": "data/chr/jms/hhh_jms.mdl",
    "sh2_nurse": "data/chr/nse/nse.mdl",
    "sh2_maria": "data/chr/mar/hhh_mar.mdl",
    "sh2_angela": "data/chr/agl/agl.mdl",
    "sh2_eddie": "data/chr/edi/hhh_edi.mdl",
    "sh2_laura": "data/chr/lau/lau.mdl",
    "sh2_flesh_lips": "data/chr/bos/bos.mdl",
    "sh2_mary": "data/chr/mry/mry.mdl",
    "sh2_abstract_daddy": "data/chr/oni/oni.mdl",
    "sh2_mannequin": "data/chr/mkn/mkn.mdl",
    "sh2_lying_figure": "data/chr/scu/scu.mdl",
    "sh2_mandarin": "data/chr/pap/pap.mdl",
    "sh2_creeper": "data/chr/tyu/tyu.mdl",
    "sh2_great_knife": "data/chr/wp/wp_nata.mdl",
    "sh2_steel_pipe": "data/chr/wp/wp_pipe.mdl",
    "sh2_chainsaw": "data/chr/wp/wp_csaw.mdl",
    "sh2_handgun": "data/chr/wp/wp_handgun.mdl",
    "sh2_shotgun": "data/chr/wp/wp_shotgun.mdl",
    "sh2_hunting_rifle": "data/chr/wp/wp_riflgun.mdl",
    "sh2_wood_plank": "data/chr/wp/wp_kakuzai.mdl",
    "sh2_first_aid": "data/chr/item/3sk.mdl",
    "sh2_first_aid_kit": "data/chr/item/3sk.mdl",
    "sh2_radio": "data/chr/item/b_ami.mdl",
    "sh2_flashlight": "data/chr/item/b_bol.mdl",
    "sh2_health_drink": "data/chr/item/b_d00.mdl",
    "sh2_ampoule": "data/chr/item/b_d01.mdl",
    "sh2_rust_key": "data/chr/item/b_d03.mdl",
    "sh2_brookhaven_bed": "data/chr/item/b_cha.mdl",
    "sh2_save_red_square": "data/chr2/item/x_houban_red.mdl",
    "sh2_rowboat": "data/chr/boat/boat.mdl",
}

_FILE_TABLE_CACHE = None
_MODEL_CACHE = {}


def _get_file_table():
    global _FILE_TABLE_CACHE
    if _FILE_TABLE_CACHE is not None:
        return _FILE_TABLE_CACHE

    iso_path = ps2.GAME_ISOS.get("sh2")
    if not iso_path or not iso_path.exists():
        _FILE_TABLE_CACHE = {}
        return _FILE_TABLE_CACHE

    reader = ps2.ISOReader(iso_path)
    elf_off, elf_sz = reader.find_file("SLES_511.56")
    if elf_off < 0:
        _FILE_TABLE_CACHE = {}
        return _FILE_TABLE_CACHE

    elf = reader.read_bytes(elf_off, elf_sz)

    def vaddr_to_file(va):
        return va - 0x100000 + 0x800

    def get_entry_info(cur_va):
        total_off = 0
        size = 0
        archive_name = ""
        orig_va = cur_va
        while cur_va:
            foff = vaddr_to_file(cur_va)
            if foff < 0 or foff + 16 > len(elf):
                break
            typ, parent, f_off, f_sz = struct.unpack_from("<4I", elf, foff)
            if cur_va == orig_va:
                size = f_sz
            if typ == 0x23:  # Archive root container
                str_foff = vaddr_to_file(parent)
                end_str = elf.find(b"\0", str_foff)
                archive_name = elf[str_foff:end_str].decode("ascii", errors="ignore")
                break
            total_off += f_off
            if parent == 0 or parent == cur_va:
                break
            cur_va = parent
        return archive_name, total_off, size

    table_start = 0x2CD980
    p = table_start
    table = {}
    while p < len(elf) - 8:
        entry_va, name_va = struct.unpack_from("<2I", elf, p)
        if entry_va < 0x300000 or name_va < 0x300000:
            break
        name_foff = vaddr_to_file(name_va)
        entry_foff = vaddr_to_file(entry_va)
        if name_foff >= len(elf) or entry_foff >= len(elf):
            break
        name_end = elf.find(b"\0", name_foff)
        filename = elf[name_foff:name_end].decode("ascii", errors="ignore")
        if filename.endswith(".mdl"):
            arch, off, sz = get_entry_info(entry_va)
            table[filename] = (arch, off, sz)
        p += 8

    _FILE_TABLE_CACHE = table
    return table


def read_sh2_model_bytes(model_path):
    """Reads raw .mdl file bytes directly from DATA/CHR.MGF in Silent Hill 2 ISO."""
    table = _get_file_table()
    if model_path not in table:
        return None
    arch_name, off, sz = table[model_path]
    iso_path = ps2.GAME_ISOS.get("sh2")
    reader = ps2.ISOReader(iso_path)
    chr_mgf_off, _ = reader.find_file("DATA/CHR.MGF")
    if chr_mgf_off < 0:
        return None
    return reader.read_bytes(chr_mgf_off + off, sz)


def parse_sh2_model(data, item_id=None):
    """Unpacks all VU1 geometry and textures from raw Silent Hill 2 .mdl file bytes,
    applying per-batch skeletal rest matrices and genuine index strip unwrapping so
    articulated parts assemble into the authentic full character silhouette."""
    if not data or len(data) < 0x80:
        return None

    # Base offset of the Model struct is at offset 0x14
    base = struct.unpack_from("<I", data, 0x14)[0]
    if base + 0x30 > len(data):
        return None

    hdr = struct.unpack_from("<28I", data, base)
    n_vu1_parts = hdr[8]
    vu1_parts_off = base + hdr[9]

    # Texture table at 0x40
    n_tex = struct.unpack_from("<I", data, 0x8)[0]
    words = struct.unpack_from("<32I", data, 0)
    textures = []
    if n_tex > 0 and len(words) >= 16 + 2 * n_tex:
        for ti in range(n_tex):
            raster_off = words[16 + ti]
            clut_off = words[16 + n_tex + ti]
            if raster_off > 0 and raster_off + 0x80 < len(data) and clut_off > 0 and clut_off + 1024 <= len(data):
                dim = struct.unpack_from("<I", data, raster_off + 8)[0]
                w = dim & 0xFFFF
                h = (dim >> 16) & 0xFFFF
                if w > 0 and h > 0 and raster_off + 0x80 + w * h <= len(data):
                    raw_pixels = data[raster_off + 0x80 : raster_off + 0x80 + w * h]
                    palette_raw = data[clut_off : clut_off + 1024]
                    # CSM1 unswizzling for PS2 256-color palette
                    palette = [None] * 256
                    for j in range(256):
                        swz = (j & ~0x18) | ((j & 0x08) << 1) | ((j & 0x10) >> 1)
                        r_val, g_val, b_val, a_val = palette_raw[4 * swz : 4 * swz + 4]
                        palette[j] = (r_val, g_val, b_val, 255 if a_val >= 128 else a_val * 2)
                    img = Image.new("RGBA", (w, h))
                    img.putdata([palette[b] for b in raw_pixels])
                    textures.append(img)

    # Bone rest matrices at hdr[2]
    bone_mats = []
    init_mat_off = base + hdr[2]
    n_skel = hdr[3]
    if n_skel > 0 and 0 < init_mat_off < len(data) - n_skel * 64:
        for b in range(n_skel):
            m = struct.unpack_from("<16f", data, init_mat_off + b * 64)
            bone_mats.append(m)

    def xform(x, y, z, m):
        return (
            x * m[0] + y * m[4] + z * m[8] + m[12],
            x * m[1] + y * m[5] + z * m[9] + m[13],
            x * m[2] + y * m[6] + z * m[10] + m[14]
        )

    # Geometry unpacking across all VU1 parts
    p_off = vu1_parts_off
    scale_raw = 1.0 / 16.0
    raw_meshes = []
    all_sh2_y = []

    for pi in range(n_vu1_parts):
        if p_off + 20 > len(data):
            break
        sz, typ, pkt_off, pkt_qwc, xtop = struct.unpack_from("<5I", data, p_off)
        if sz == 0:
            break

        n_b = struct.unpack_from("<I", data, p_off + 0x1c)[0] if p_off + 0x20 <= len(data) else 0
        b_off = struct.unpack_from("<I", data, p_off + 0x20)[0] if p_off + 0x24 <= len(data) else 0
        bones = struct.unpack(f"<{n_b}H", data[p_off + b_off : p_off + b_off + n_b * 2]) if n_b > 0 and b_off > 0 and p_off + b_off + n_b * 2 <= len(data) else (0,)

        if pkt_qwc > 0 and pkt_off > 0 and p_off + pkt_off + pkt_qwc * 16 <= len(data):
            pkt = data[p_off + pkt_off : p_off + pkt_off + pkt_qwc * 16]

            # Read per-batch descriptor table at pkt offset 4
            batch_map = {}
            if len(pkt) > 16:
                n_batches = struct.unpack_from("<I", pkt, 8)[0]
                mat_addrs = []
                batches = []
                for bi in range(n_batches):
                    desc = 4 + (bi + 1) * 36
                    if desc + 24 <= len(pkt):
                        q1 = struct.unpack_from("<4I", pkt, desc + 4)
                        q2 = struct.unpack_from("<4I", pkt, desc + 20)
                        v_count, v_addr = q1[0], q1[2]
                        mat_addr = q2[0]
                        mat_addrs.append(mat_addr)
                        batches.append((v_addr, v_count, mat_addr))

                base_mat = min(mat_addrs) if mat_addrs else 0
                for v_addr, v_count, mat_addr in batches:
                    lb = (mat_addr - base_mat) // 4
                    wb = bones[lb] if lb < len(bones) else bones[0]
                    batch_map[v_addr] = (v_count, wb)

            idx = 0
            pos_part = []
            uv_part = []
            idx_list = []

            while idx < len(pkt) - 4:
                w = struct.unpack_from("<I", pkt, idx)[0]
                cmd = (w >> 24) & 0xFF
                num = (w >> 16) & 0xFF
                imm = w & 0xFFFF
                if 0x60 <= cmd <= 0x7F:
                    fmt = cmd & 0x0F
                    if fmt == 9 and (not batch_map or imm in batch_map):  # V3-16 vertex coordinates
                        wb = batch_map[imm][1] if imm in batch_map else bones[0]
                        m = bone_mats[wb] if bone_mats and wb < len(bone_mats) else (bone_mats[0] if bone_mats else None)
                        for vi in range(num):
                            x, y, z = struct.unpack_from("<3h", pkt, idx + 4 + vi * 6)
                            if m:
                                wx, wy, wz = xform(x * scale_raw, y * scale_raw, z * scale_raw, m)
                                pos_part.append((wx, wy, wz))
                                all_sh2_y.append(wy)
                            else:
                                pos_part.append((x / 4096.0, y / 4096.0, z / 4096.0))
                        sz_bytes = num * 6
                        if sz_bytes % 4 != 0:
                            sz_bytes += 4 - (sz_bytes % 4)
                        idx += 4 + sz_bytes
                        continue
                    elif fmt == 5 and num > 0:  # V2-16 UVs
                        for vi in range(num):
                            u, v = struct.unpack_from("<2h", pkt, idx + 4 + vi * 4)
                            uv_part.append((u / 4096.0, v / 4096.0))
                        idx += 4 + num * 4
                        continue
                    elif fmt == 13 and num > 0:  # V4-16 strip indices
                        shorts = struct.unpack(f"<{num * 4}H", pkt[idx + 4 : idx + 4 + num * 8])
                        idx_list.extend(shorts)
                        idx += 4 + num * 8
                        continue
                    else:
                        elem_sz = {0: 4, 1: 2, 2: 1, 4: 8, 5: 4, 6: 2, 8: 12, 9: 6, 12: 16, 13: 8, 14: 4}.get(fmt, 4)
                        sz_bytes = num * elem_sz
                        if sz_bytes % 4 != 0:
                            sz_bytes += 4 - (sz_bytes % 4)
                        idx += 4 + sz_bytes
                        continue
                idx += 4

            real_nv = len(pos_part)
            if real_nv > 0:
                if len(uv_part) < real_nv:
                    uv_part.extend([(0.0, 0.0)] * (real_nv - len(uv_part)))
                else:
                    uv_part = uv_part[:real_nv]

                tris = []
                if idx_list:
                    raws = [s & 0x7FFF for s in idx_list]
                    div4 = [r // 4 for r in raws]
                    base_idx = min(div4) if div4 else 0
                    strip = []
                    wind = 0
                    for i, s in enumerate(idx_list):
                        adc = (s & 0x8000) != 0
                        v_idx = (s & 0x7FFF) // 4 - base_idx
                        if v_idx < 0 or v_idx >= real_nv:
                            strip = []
                            wind = 0
                            continue
                        if adc:
                            if i > 0 and not (idx_list[i - 1] & 0x8000 != 0):
                                strip = [v_idx]
                            else:
                                strip.append(v_idx)
                            wind = 0
                        else:
                            strip.append(v_idx)
                            wind += 1
                            if len(strip) >= 3:
                                a, b, c = strip[-3], strip[-2], strip[-1]
                                if a != b and b != c and a != c:
                                    tris.append((a, b, c) if wind % 2 == 1 else (b, a, c))
                else:
                    for vi in range(real_nv - 2):
                        tris.append((vi, vi + 1, vi + 2) if vi % 2 == 0 else (vi + 1, vi, vi + 2))

                raw_meshes.append({
                    "part": pi,
                    "w_pts": pos_part,
                    "uv": uv_part,
                    "tris": tris,
                })
        p_off += sz

    meshes = []
    if bone_mats and all_sh2_y:
        y_max = max(all_sh2_y)
        y_min = min(all_sh2_y)
        h_raw = max(1.0, y_max - y_min)

        target_h = 180.0
        if item_id:
            low_id = item_id.lower()
            if "pyramid" in low_id or "red" in low_id:
                target_h = 210.0
            elif any(k in low_id for k in ["nurse", "maria", "angela", "mary"]):
                target_h = 168.0
            elif "laura" in low_id:
                target_h = 130.0

        scale_out = target_h / h_raw
        for rm in raw_meshes:
            out_pos = [(p[0] * scale_out, (y_max - p[1]) * scale_out, p[2] * scale_out) for p in rm["w_pts"]]
            meshes.append({
                "part": rm["part"],
                "pos": out_pos,
                "uv": rm["uv"],
                "tris": rm["tris"],
            })
    else:
        for rm in raw_meshes:
            meshes.append({
                "part": rm["part"],
                "pos": rm["w_pts"],
                "uv": rm["uv"],
                "tris": rm["tris"],
            })

    return {
        "n_parts": n_vu1_parts,
        "meshes": meshes,
        "textures": textures,
    }


def get_asset(item_id):
    """Loads and caches parsed Silent Hill 2 asset."""
    if item_id in _MODEL_CACHE:
        return _MODEL_CACHE[item_id]

    mdl_path = SH2_ID_MAP.get(item_id)
    if not mdl_path:
        return None

    raw_bytes = read_sh2_model_bytes(mdl_path)
    if not raw_bytes:
        return None

    parsed = parse_sh2_model(raw_bytes, item_id=item_id)
    if not parsed:
        return None

    _MODEL_CACHE[item_id] = parsed
    return parsed


def get_model_json(item_id):
    """Returns Outbreak Studio Three.js JSON representation of the real SH2 3D mesh."""
    asset = get_asset(item_id)
    if not asset or not asset.get("meshes"):
        return None

    meshes_out = []
    for mi, m in enumerate(asset["meshes"]):
        pos = m["pos"]
        uv = m["uv"]
        tris = m["tris"]
        if not pos or not tris:
            continue

        flat_p = [coord for pt in pos for coord in pt]
        flat_uv = [coord for pt in uv for coord in pt]
        flat_c = [1.0, 1.0, 1.0, 1.0] * len(pos)
        flat_idx = [idx for t in tris for idx in t]

        meshes_out.append({
            "mesh": mi,
            "bone": 0,
            "additive": False,
            "c": base64.b64encode(struct.pack(f"<{len(flat_c)}f", *flat_c)).decode(),
            "p": base64.b64encode(struct.pack(f"<{len(flat_p)}f", *flat_p)).decode(),
            "uv": base64.b64encode(struct.pack(f"<{len(flat_uv)}f", *flat_uv)).decode(),
            "groups": [{"m": 0, "i": base64.b64encode(struct.pack(f"<{len(flat_idx)}I", *flat_idx)).decode()}],
        })

    bones = [[-1, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]]
    return {
        "bones": bones,
        "meshes": meshes_out,
        "tids": [f"silent_hill:{item_id}:0"],
        "objects": len(meshes_out),
    }


def get_texture_png(item_id):
    """Returns authentic unswizzled PNG texture bytes for Studio texture rendering."""
    asset = get_asset(item_id)
    if not asset or not asset.get("textures"):
        return None
    img = asset["textures"][0]
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def get_nbd_mesh_node(item_id, material=0, template=None):
    """Generates Outbreak Capcom NBD kind 4 mesh node from real SH2 geometry for game injection."""
    asset = get_asset(item_id)
    if not asset or not asset.get("meshes"):
        return None

    all_pos = []
    all_uv = []
    all_tris = []
    v_offset = 0

    for m in asset["meshes"]:
        pos = m["pos"]
        uv = m["uv"]
        tris = m["tris"]
        for p in pos:
            all_pos.append((p[0], p[1], p[2]))
        all_uv.extend(uv)
        for t in tris:
            all_tris.append((v_offset + t[0], v_offset + t[1], v_offset + t[2]))
        v_offset += len(pos)

    if not all_pos or not all_tris:
        return None

    if template is None:
        p11 = struct.pack("<III", 11 << 16, 1, 28) + struct.pack("<4f", 128.0, 128.0, 128.0, 255.0)
        p15 = struct.pack("<III", 15 << 16, 1, 84) + bytes(72)
        template = struct.pack("<III", 4, 2, 12 + len(p11) + len(p15)) + p11 + p15

    return nbd.make_mesh_node(template, all_pos, all_uv, all_tris, material=material)


def get_sld_texture(item_id):
    """Encodes authentic SH2 texture into Outbreak TIM2 / SLD format for in-game injection."""
    asset = get_asset(item_id)
    if not asset or not asset.get("textures"):
        return None

    img = asset["textures"][0]
    w, h = img.size
    q = img.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
    pal_rgb = q.getpalette()[:768]
    raw_pixels = bytes(q.getdata())

    # Build 256-color RGBA CLUT (CSM1 swizzled for PS2 GS)
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
