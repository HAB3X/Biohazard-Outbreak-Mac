#!/usr/bin/env python3
"""Resident Evil 4 asset extraction and conversion for Biohazard Outbreak and Outbreak Studio.

Extracts 3D scenery, items, weapons, characters, and enemies from the PS2 RE4 ISO (DATA/BIO4DAT.AFS)
and converts their models and textures into:
1. Outbreak Studio 3D format (JSON with base64 buffers for Three.js rendering).
2. Outbreak Game format (Capcom NBD chunks AMO/AHI/TID and Sony TIM2 / SLD textures).

Usage:
    tools/bh_re4.py list [scenery|weapons|enemies|characters]
    tools/bh_re4.py dump ID OUT_DIR
    tools/bh_re4.py test
"""
import base64
import io
import json
import math
import os
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import bh_archive as archive
import bh_nbd as nbd
import bh_sld
import tm2

DEFAULT_ISO = Path("/Users/marcuschandler/Downloads/Resident Evil 4.iso")
RE4_MASTER = Path("/Users/marcuschandler/Downloads/re4-master")

# -----------------------------------------------------------------------------
# Asset Catalog Definitions
# -----------------------------------------------------------------------------

RE4_SCENERY = {
    "cap01": {"name": "Leon (Rocket Launcher Figurine)", "bin": "cap01.bin", "tpl": "cap01.tpl", "scale": 1.0, "category": "scenery"},
    "cap02": {"name": "Ashley Graham (Figurine)", "bin": "cap02.bin", "tpl": "cap02.tpl", "scale": 1.0, "category": "scenery"},
    "cap03": {"name": "Ada Wong (Figurine)", "bin": "cap03.bin", "tpl": "cap03.tpl", "scale": 1.0, "category": "scenery"},
    "cap04": {"name": "Luis Sera (Figurine)", "bin": "cap04.bin", "tpl": "cap04.tpl", "scale": 1.0, "category": "scenery"},
    "cap05": {"name": "Don Jose (Ganado w/ Axe)", "bin": "cap05.bin", "tpl": "cap05.tpl", "scale": 1.0, "category": "scenery"},
    "cap06": {"name": "Don Diego (Ganado w/ Scythe)", "bin": "cap06.bin", "tpl": "cap06.tpl", "scale": 1.0, "category": "scenery"},
    "cap07": {"name": "Don Esteban (Ganado w/ Sickle)", "bin": "cap07.bin", "tpl": "cap07.tpl", "scale": 1.0, "category": "scenery"},
    "cap08": {"name": "Don Manuel (Ganado w/ Pitchfork)", "bin": "cap08.bin", "tpl": "cap08.tpl", "scale": 1.0, "category": "scenery"},
    "cap09": {"name": "Dr. Salvador (Chainsaw Man Figurine)", "bin": "cap09.bin", "tpl": "cap09.tpl", "scale": 1.0, "category": "scenery"},
    "cap10": {"name": "Weapons Merchant (Figurine)", "bin": "cap10.bin", "tpl": "cap10.tpl", "scale": 1.0, "category": "scenery"},
    "cap11": {"name": "Zealot (Castle Cultist Figurine)", "bin": "cap11.bin", "tpl": "cap11.tpl", "scale": 1.0, "category": "scenery"},
    "cap12": {"name": "Leader Zealot (Goat Skull Figurine)", "bin": "cap12.bin", "tpl": "cap12.tpl", "scale": 1.0, "category": "scenery"},
    "cap13": {"name": "Soldier (Island Ganado Figurine)", "bin": "cap13.bin", "tpl": "cap13.tpl", "scale": 1.0, "category": "scenery"},
    "cap14": {"name": "J.J. (Gatling Gun Soldier Figurine)", "bin": "cap14.bin", "tpl": "cap14.tpl", "scale": 1.0, "category": "scenery"},
    "cap15": {"name": "Bella Sisters (Dual Chainsaw Figurine)", "bin": "cap15.bin", "tpl": "cap15.tpl", "scale": 1.0, "category": "scenery"},
    "cap16": {"name": "Garrador (Clawed Gladiator Figurine)", "bin": "cap16.bin", "tpl": "cap16.tpl", "scale": 1.0, "category": "scenery"},
    "cap17": {"name": "Armadura (Knight Armor Figurine)", "bin": "cap17.bin", "tpl": "cap17.tpl", "scale": 1.0, "category": "scenery"},
    "cap18": {"name": "El Gigante (Giant Figurine)", "bin": "cap18.bin", "tpl": "cap18.tpl", "scale": 1.0, "category": "scenery"},
    "cap19": {"name": "Bitores Mendez (Chief Figurine)", "bin": "cap19.bin", "tpl": "cap19.tpl", "scale": 1.0, "category": "scenery"},
    "cap20": {"name": "Ramon Salazar (Figurine)", "bin": "cap20.bin", "tpl": "cap20.tpl", "scale": 1.0, "category": "scenery"},
    "cap21": {"name": "Jack Krauser (Mutated Figurine)", "bin": "cap21.bin", "tpl": "cap21.tpl", "scale": 1.0, "category": "scenery"},
    "cap22": {"name": "Osmund Saddler (Figurine)", "bin": "cap22.bin", "tpl": "cap22.tpl", "scale": 1.0, "category": "scenery"},
    "cap23": {"name": "Del Lago (Lake Monster Figurine)", "bin": "cap23.bin", "tpl": "cap23.tpl", "scale": 1.0, "category": "scenery"},
    "cap24": {"name": "Regenerador (Figurine)", "bin": "cap24.bin", "tpl": "cap24.tpl", "scale": 1.0, "category": "scenery"},
    "itm00": {"name": "Handgun Ammo Box", "bin": "itm00.bin", "tpl": "itm00.tpl", "scale": 1.0, "category": "scenery"},
    "itm01": {"name": "Shotgun Shells Box", "bin": "itm01.bin", "tpl": "itm01.tpl", "scale": 1.0, "category": "scenery"},
    "itm02": {"name": "Rifle Ammo Box", "bin": "itm02.bin", "tpl": "itm02.tpl", "scale": 1.0, "category": "scenery"},
    "itm04": {"name": "Green Herb Pot", "bin": "itm04.bin", "tpl": "itm04.tpl", "scale": 1.0, "category": "scenery"},
    "itm05": {"name": "First Aid Spray", "bin": "itm05.bin", "tpl": "itm05.tpl", "scale": 1.0, "category": "scenery"},
}

RE4_WEAPONS = {
    "wep00": {"name": "Silver Ghost (Leon's Handgun)", "dat": "ss_wep00.dat", "category": "weapons"},
    "wep01": {"name": "Shotgun (Pump Action)", "dat": "ss_wep01.dat", "category": "weapons"},
    "wep02": {"name": "Broken Butterfly (.45 Magnum)", "dat": "ss_wep02.dat", "category": "weapons"},
    "wep03": {"name": "Rifle (Bolt Action)", "dat": "ss_wep03.dat", "category": "weapons"},
    "wep04": {"name": "Punisher (Handgun)", "dat": "ss_wep04.dat", "category": "weapons"},
    "wep05": {"name": "Red9 (Mauser C96 Broomhandle)", "dat": "ss_wep05.dat", "category": "weapons"},
    "wep06": {"name": "Blacktail (Handgun)", "dat": "ss_wep06.dat", "category": "weapons"},
    "wep07": {"name": "Riot Gun (Shotgun)", "dat": "ss_wep07.dat", "category": "weapons"},
    "wep08": {"name": "Semi-Auto Rifle", "dat": "ss_wep08.dat", "category": "weapons"},
    "wep09": {"name": "TMP (Submachine Gun)", "dat": "ss_wep09.dat", "category": "weapons"},
    "wep10": {"name": "Striker (Combat Shotgun)", "dat": "ss_wep10.dat", "category": "weapons"},
    "wep11": {"name": "Killer7 (.45 Magnum)", "dat": "ss_wep11.dat", "category": "weapons"},
    "wep12": {"name": "Handcannon (.500 Magnum)", "dat": "ss_wep12.dat", "category": "weapons"},
    "wep13": {"name": "Chicago Typewriter (.45 SMG)", "dat": "ss_wep13.dat", "category": "weapons"},
    "wep14": {"name": "Rocket Launcher (RPG-7)", "dat": "ss_wep14.dat", "category": "weapons"},
    "wep17": {"name": "Matilda (3-Burst Handgun)", "dat": "ss_wep17.dat", "category": "weapons"},
}

RE4_ENEMIES = {
    "em10": {"name": "Ganado (Villager)", "dat": "em10.dat", "kind_id": 100, "category": "enemies", "boss": False},
    "em11": {"name": "Zealot (Castle Cultist)", "dat": "em11.dat", "kind_id": 101, "category": "enemies", "boss": False},
    "em12": {"name": "Soldier (Island Military)", "dat": "em12.dat", "kind_id": 102, "category": "enemies", "boss": False},
    "em14": {"name": "Dr. Salvador (Chainsaw Man)", "dat": "em14.dat", "kind_id": 104, "category": "enemies", "boss": True},
    "em20": {"name": "El Gigante (Giant Boss)", "dat": "em20.dat", "kind_id": 120, "category": "enemies", "boss": True},
    "em22": {"name": "Garrador (Claw Berserker)", "dat": "em22.dat", "kind_id": 122, "category": "enemies", "boss": True},
    "em24": {"name": "Del Lago (Lake Leviathan)", "dat": "em24.dat", "kind_id": 124, "category": "enemies", "boss": True},
    "em2b": {"name": "Regenerador (Iron Maiden)", "dat": "em2b.dat", "kind_id": 125, "category": "enemies", "boss": True},
    "em31": {"name": "Jack Krauser (Boss)", "dat": "em31.dat", "kind_id": 131, "category": "enemies", "boss": True},
    "em32": {"name": "Ramon Salazar (Boss)", "dat": "em32.dat", "kind_id": 132, "category": "enemies", "boss": True},
    "em35": {"name": "U3 ('It' Chimera Boss)", "dat": "em35.dat", "kind_id": 135, "category": "enemies", "boss": True},
    "em39": {"name": "Osmund Saddler (Final Boss)", "dat": "em39.dat", "kind_id": 139, "category": "enemies", "boss": True},
}

RE4_CHARACTERS = {
    "pl00": {"name": "Leon S. Kennedy", "dat": "pl00.dat", "category": "characters"},
    "pl01": {"name": "Ashley Graham", "bin": "pl0100.bin", "tpl": "pl0100.tpl", "category": "characters"},
    "pl05": {"name": "Ada Wong", "bin": "pl0500.bin", "tpl": "pl0500.tpl", "category": "characters"},
}

# -----------------------------------------------------------------------------
# ISO & AFS Reader
# -----------------------------------------------------------------------------

ISO_SECTOR = 2048


class RE4Disc:
    _instance = None

    def __init__(self, iso_path=None):
        self.path = Path(iso_path or os.environ.get("RE4_ISO") or DEFAULT_ISO)
        if not self.path.exists():
            raise FileNotFoundError(f"RE4 ISO not found at {self.path}")
        self._f = open(self.path, "rb")
        self.afs_base, self.afs_size = self._find_iso("DATA/BIO4DAT.AFS")
        self.files = self._read_afs()

    @classmethod
    def get(cls):
        if cls._instance is None:
            cls._instance = cls()
        return cls._instance

    def _find_iso(self, path):
        self._f.seek(16 * ISO_SECTOR)
        root = self._f.read(ISO_SECTOR)[156:156 + 34]
        lba = struct.unpack_from("<I", root, 2)[0]
        size = struct.unpack_from("<I", root, 10)[0]
        for part in path.split("/"):
            self._f.seek(lba * ISO_SECTOR)
            data = self._f.read(size)
            o = 0
            found = None
            while o < len(data):
                n = data[o]
                if n == 0:
                    o = (o // ISO_SECTOR + 1) * ISO_SECTOR
                    continue
                rec = data[o:o + n]
                name = rec[33:33 + rec[32]].decode("ascii", errors="ignore").split(";")[0].upper()
                if name == part.upper():
                    found = struct.unpack_from("<I", rec, 2)[0], struct.unpack_from("<I", rec, 10)[0]
                    break
                o += n
            if found is None:
                raise ValueError(f"{path}: {part} not on disc")
            lba, size = found
        return lba * ISO_SECTOR, size

    def _read_afs(self):
        self._f.seek(self.afs_base)
        magic, count = struct.unpack("<4sI", self._f.read(8))
        if magic != b"AFS\0":
            raise ValueError(f"AFS magic error: {magic!r}")
        tab = struct.unpack(f"<{2 * count}I", self._f.read(8 * count))
        names_ofs, names_size = struct.unpack("<2I", self._f.read(8))
        self._f.seek(self.afs_base + names_ofs)
        names_buf = self._f.read(names_size)
        files = {}
        for i in range(count):
            name = names_buf[0x30 * i:0x30 * i + 0x20].split(b"\0")[0].decode("ascii", errors="ignore")
            offset = self.afs_base + tab[2 * i]
            size = tab[2 * i + 1]
            if size > 0:
                files[name.lower()] = (i, name, offset, size)
        return files

    def read_file(self, name):
        key = name.lower()
        if key not in self.files:
            raise FileNotFoundError(f"{name} not in BIO4DAT.AFS")
        _, _, offset, size = self.files[key]
        self._f.seek(offset)
        return self._f.read(size)

    def unpack_dat(self, name):
        """Unpacks a bare body .dat container: returns list of (tag, bytes)."""
        data = self.read_file(name)
        if len(data) < 16:
            return []
        count = struct.unpack_from("<I", data, 0)[0]
        if count == 0 or count > 2000 or data[4:16] != b"\0" * 12:
            return []
        offsets = struct.unpack_from(f"<{count}I", data, 16)
        tags_start = 16 + 4 * count
        tags = [data[tags_start + 4 * j:tags_start + 4 * j + 4].decode("ascii", errors="ignore").strip() for j in range(count)]
        entries = []
        for j in range(count):
            end = offsets[j + 1] if j + 1 < count else len(data)
            tag = tags[j]
            payload = data[offsets[j]:end]
            entries.append((tag, payload))
        return entries


# -----------------------------------------------------------------------------
# Texture Decoder: PS2 TPL -> PNG / TIM2
# -----------------------------------------------------------------------------

def decode_ps2_tpl(tpl_bytes):
    """Decodes a PS2 TPL texture into a PIL RGBA Image."""
    if len(tpl_bytes) < 32:
        return None
    ver, n, ofs = struct.unpack_from("<III", tpl_bytes, 0)
    w, h, fmt, p_clut, p_data = struct.unpack_from("<HHIII", tpl_bytes, ofs)
    if w == 0 or h == 0 or w > 2048 or h > 2048:
        return None

    # 4-bit indexed (16 colors) or 8-bit indexed (256 colors)
    # Check data and clut offsets
    pixel_data = tpl_bytes[p_data:]
    clut_raw = tpl_bytes[p_clut:] if p_clut < len(tpl_bytes) else None

    # If it's a 4-bit texture: 16 colors
    if len(tpl_bytes) <= (p_data + (w * h // 2) + 256):
        # 16-color CLUT layout
        # In PS2 TPL, 16-color palette is often stored after the pixel data (e.g. at 0x1040)
        # or at p_clut
        clut_offset = 0x40 + (w * h // 2)
        if len(tpl_bytes) >= clut_offset + 64:
            clut_raw = tpl_bytes[clut_offset:]
        palette = []
        if clut_raw and len(clut_raw) >= 64:
            # First 8 colors
            for j in range(8):
                r, g, b, a = clut_raw[4 * j:4 * j + 4]
                alpha = 255 if a >= 128 else a * 2
                palette.append((r, g, b, alpha))
            # Second 8 colors at +0x40 stride
            second_half = clut_raw[0x40:] if len(clut_raw) >= 0x40 + 32 else clut_raw[32:]
            for j in range(8):
                r, g, b, a = second_half[4 * j:4 * j + 4]
                alpha = 255 if a >= 128 else a * 2
                palette.append((r, g, b, alpha))
        else:
            palette = [(i * 16, i * 16, i * 16, 255) for i in range(16)]

        pixels = []
        for b in pixel_data[:w * h // 2]:
            p0 = b & 0x0F
            p1 = (b >> 4) & 0x0F
            pixels.append(palette[p0] if p0 < len(palette) else (255, 0, 255, 255))
            pixels.append(palette[p1] if p1 < len(palette) else (255, 0, 255, 255))
        img = Image.new("RGBA", (w, h))
        img.putdata(pixels[:w * h])
        return img
    else:
        # 8-bit indexed (256 colors)
        clut_offset = p_data + (w * h)
        if len(tpl_bytes) >= clut_offset + 1024:
            clut_raw = tpl_bytes[clut_offset:]
        palette = []
        if clut_raw and len(clut_raw) >= 1024:
            for j in range(256):
                # CSM1 palette unswizzling: blocks 8-15 and 16-23 swapped every 32
                swz = (j & ~0x18) | ((j & 0x08) << 1) | ((j & 0x10) >> 1)
                r, g, b, a = clut_raw[4 * swz:4 * swz + 4]
                palette.append((r, g, b, 255 if a >= 128 else a * 2))
        else:
            palette = [(i, i, i, 255) for i in range(256)]
        pixels = [palette[b] if b < len(palette) else (255, 0, 255, 255) for b in pixel_data[:w * h]]
        img = Image.new("RGBA", (w, h))
        img.putdata(pixels[:w * h])
        return img


def tpl_to_png(tpl_bytes):
    """Converts PS2 TPL texture bytes to PNG bytes."""
    img = decode_ps2_tpl(tpl_bytes)
    if not img:
        img = Image.new("RGBA", (64, 64), (128, 128, 128, 255))
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def tpl_to_tim2(tpl_bytes):
    """Converts PS2 TPL texture bytes to Sony TIM2 bytes for in-game use."""
    img = decode_ps2_tpl(tpl_bytes)
    if not img:
        img = Image.new("RGBA", (64, 64), (128, 128, 128, 255))
    w, h = img.size
    # Quantize to 256 colors
    q = img.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
    pal_rgb = q.getpalette()[:768]
    raw_pixels = bytes(q.getdata())

    # Build 256-color RGBA CLUT (CSM1 swizzled for PS2)
    clut = bytearray(1024)
    for i in range(256):
        r = pal_rgb[i * 3]
        g = pal_rgb[i * 3 + 1]
        b = pal_rgb[i * 3 + 2]
        a = 128  # PS2 GS full opacity
        swz = (i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)
        struct.pack_into("<4B", clut, 4 * swz, r, g, b, a)

    # TIM2 Picture Header (48 bytes)
    header_size = 48
    img_size = len(raw_pixels)
    clut_size = len(clut)
    total_size = 16 + header_size + img_size + clut_size

    file_header = struct.pack("<4sBBxxII", b"TIM2", 4, 1, 0, 0)
    pic_header = struct.pack("<IIIHHBBBBHH",
                             total_size - 16, clut_size, img_size, header_size,
                             256, 0, 1, 3, 5, w, h)
    return file_header + pic_header + bytes(header_size - len(pic_header)) + raw_pixels + clut


def tpl_to_sld(tpl_bytes):
    """Converts PS2 TPL texture bytes to Outbreak SLD-compressed TIM2 stream."""
    return bh_sld.pack(tpl_to_tim2(tpl_bytes))


# -----------------------------------------------------------------------------
# Geometry Decoder: PS2 BIN -> Triangles, Three.js JSON & NBD
# -----------------------------------------------------------------------------

def parse_ps2_bin(data):
    """Parses PS2 BIN mesh data into parts, vertices, normals, UVs, and triangles."""
    if len(data) < 0x50:
        return [], []
    n_parts = data[2]
    p_head = struct.unpack_from("<I", data, 4)[0]
    parts = []
    if p_head + 16 * n_parts <= len(data):
        for i in range(n_parts):
            r = data[p_head + 16 * i:p_head + 16 * i + 16]
            no, parent, attach = r[0], r[1], r[2]
            pos = struct.unpack_from("<3f", r, 4)
            parts.append({"no": no, "parent": parent if parent < 0xFE else -1, "pos": pos})
    if not parts:
        parts = [{"no": 0, "parent": -1, "pos": (0.0, 0.0, 0.0)}]

    meshes = []
    idx = 0x60
    while idx < len(data) - 16:
        cmd = data[idx + 3]
        num = data[idx + 2]
        if cmd == 0x6D and num > 0 and num % 3 == 0:
            v_count = num // 3
            pkt_start = idx + 4
            vtx = []
            for k in range(v_count):
                base = pkt_start + k * 24
                if base + 24 > len(data):
                    break
                px, py, pz, _ = struct.unpack_from("<4h", data, base)
                nx, ny, nz, _ = struct.unpack_from("<4h", data, base + 8)
                u, v, flg, adc = struct.unpack_from("<4h", data, base + 16)
                # Scale from PS2 fixed-point (x1000/x512) into millimetres
                vtx.append({
                    "pos": (px * 0.02, py * 0.02, pz * 0.02),
                    "nrm": (nx / 128.0, ny / 128.0, nz / 128.0),
                    "uv": (u / 256.0, v / 256.0),
                    "adc": adc,
                })

            triangles = []
            strip = []
            for vi, v in enumerate(vtx):
                strip.append(vi)
                if v["adc"] == -1 or v["adc"] == 0x7FFF or vi == len(vtx) - 1:
                    for s in range(len(strip) - 2):
                        a, b, c = strip[s], strip[s + 1], strip[s + 2]
                        if a != b and b != c and a != c:
                            triangles.append((a, b, c) if s % 2 == 0 else (b, a, c))
                    strip = []

            if triangles:
                meshes.append({"vertices": vtx, "triangles": triangles})
            idx += 4 + num * 8
        else:
            idx += 4

    return parts, meshes


def bin_to_model_json(bin_bytes, asset_id="re4"):
    """Converts a PS2 BIN model into Outbreak Studio Three.js JSON format."""
    parts, raw_meshes = parse_ps2_bin(bin_bytes)
    bones = []
    for p in parts:
        # [parent, scale xyz, rotation xyz, offset xyz]
        bones.append([p["parent"], [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], list(p["pos"])])
    if not bones:
        bones = [[-1, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]]

    meshes = []
    for mi, m in enumerate(raw_meshes):
        vtx = m["vertices"]
        tris = m["triangles"]
        if not vtx or not tris:
            continue
        flat_p = [coord for v in vtx for coord in v["pos"]]
        flat_uv = [uv for v in vtx for uv in v["uv"]]
        flat_c = [1.0, 1.0, 1.0, 1.0] * len(vtx)
        flat_idx = [idx for t in tris for idx in t]

        meshes.append({
            "mesh": mi,
            "bone": 0,
            "additive": False,
            "c": base64.b64encode(struct.pack(f"<{len(flat_c)}f", *flat_c)).decode(),
            "p": base64.b64encode(struct.pack(f"<{len(flat_p)}f", *flat_p)).decode(),
            "uv": base64.b64encode(struct.pack(f"<{len(flat_uv)}f", *flat_uv)).decode(),
            "groups": [{"m": 0, "i": base64.b64encode(struct.pack(f"<{len(flat_idx)}I", *flat_idx)).decode()}],
        })

    return {
        "bones": bones,
        "meshes": meshes,
        "tids": [f"re4:{asset_id}:0"],
        "objects": len(meshes),
    }


def bin_to_nbd_mesh_node(bin_bytes, material=0, template=None):
    """Converts a PS2 BIN model into an Outbreak Capcom NBD kind 4 mesh node."""
    _, raw_meshes = parse_ps2_bin(bin_bytes)
    if not raw_meshes:
        return None
    # Combine submeshes into a single unified mesh node
    all_pos, all_uv, all_tris = [], [], []
    v_base = 0
    for m in raw_meshes:
        vtx = m["vertices"]
        tris = m["triangles"]
        all_pos.extend(v["pos"] for v in vtx)
        all_uv.extend(v["uv"] for v in vtx)
        all_tris.extend((a + v_base, b + v_base, c + v_base) for a, b, c in tris)
        v_base += len(vtx)

    # Kind 4 template piece
    if template is None:
        p11 = struct.pack("<III", 11 << 16, 1, 28) + struct.pack("<4f", 128.0, 128.0, 128.0, 255.0)
        p15 = struct.pack("<III", 15 << 16, 1, 84) + bytes(72)
        template = struct.pack("<III", 4, 2, 12 + len(p11) + len(p15)) + p11 + p15
    return nbd.make_mesh_node(template, all_pos, all_uv, all_tris, material)


# -----------------------------------------------------------------------------
# Asset Exporter & Public API
# -----------------------------------------------------------------------------

def get_asset(asset_id):
    """Retrieves metadata and raw binary contents for an RE4 asset by ID."""
    info = (
        RE4_SCENERY.get(asset_id)
        or RE4_WEAPONS.get(asset_id)
        or RE4_ENEMIES.get(asset_id)
        or RE4_CHARACTERS.get(asset_id)
    )
    if not info:
        raise KeyError(f"Unknown RE4 asset '{asset_id}'")

    disc = RE4Disc.get()
    bin_data = None
    tpl_data = None

    if "bin" in info:
        bin_data = disc.read_file(info["bin"])
    if "tpl" in info:
        tpl_data = disc.read_file(info["tpl"])

    if "dat" in info:
        entries = disc.unpack_dat(info["dat"])
        for tag, data in entries:
            if "BIN" in tag and bin_data is None:
                bin_data = data
            elif "TPL" in tag and tpl_data is None:
                tpl_data = data
            if bin_data and tpl_data:
                break

    return {**info, "id": asset_id, "bin_bytes": bin_data, "tpl_bytes": tpl_data}


def catalog():
    """Returns the full catalog of all supported RE4 assets."""
    all_items = []
    for d in (RE4_SCENERY, RE4_WEAPONS, RE4_ENEMIES, RE4_CHARACTERS):
        for k, v in d.items():
            all_items.append({**v, "id": k})
    return all_items


# -----------------------------------------------------------------------------
# CLI Entry Point
# -----------------------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 0

    cmd = sys.argv[1]
    if cmd == "list":
        category = sys.argv[2] if len(sys.argv) > 2 else "all"
        for item in catalog():
            if category == "all" or item.get("category") == category:
                print(f"[{item['category']:<10}] {item['id']:<8}: {item['name']}")
    elif cmd == "test":
        print("Testing RE4 extraction on cap01, itm00, wep00, em10...")
        for aid in ("cap01", "itm00", "wep00", "em10"):
            asset = get_asset(aid)
            has_bin = asset["bin_bytes"] is not None
            has_tpl = asset["tpl_bytes"] is not None
            b_len = len(asset["bin_bytes"]) if has_bin else 0
            t_len = len(asset["tpl_bytes"]) if has_tpl else 0
            m_json = bin_to_model_json(asset["bin_bytes"], aid) if has_bin else {}
            num_meshes = len(m_json.get("meshes", []))
            print(f"  {aid:<6} ({asset['name']}): BIN={b_len}B, TPL={t_len}B, Studio Meshes={num_meshes}")
        print("All tests passed!")
    elif cmd == "dump" and len(sys.argv) >= 4:
        aid = sys.argv[2]
        out_dir = Path(sys.argv[3])
        out_dir.mkdir(parents=True, exist_ok=True)
        asset = get_asset(aid)
        if asset["bin_bytes"]:
            (out_dir / f"{aid}.bin").write_bytes(asset["bin_bytes"])
            model = bin_to_model_json(asset["bin_bytes"], aid)
            (out_dir / f"{aid}.json").write_text(json.dumps(model, indent=2))
        if asset["tpl_bytes"]:
            (out_dir / f"{aid}.tpl").write_bytes(asset["tpl_bytes"])
            png = tpl_to_png(asset["tpl_bytes"])
            (out_dir / f"{aid}.png").write_bytes(png)
        print(f"Dumped {aid} to {out_dir}")


if __name__ == "__main__":
    sys.exit(main() or 0)
