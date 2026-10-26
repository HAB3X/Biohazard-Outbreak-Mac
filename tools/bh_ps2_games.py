#!/usr/bin/env python3
"""Unified PS2 Horror Games Asset Pipeline for Biohazard Outbreak and Outbreak Studio.

Provides exhaustive asset extraction, conversion, and runtime placement for all 7 PS2 horror franchises (13 titles):
1. Resident Evil 4 (Capcom)
2. Haunting Ground / Demento (Capcom)
3. Clock Tower 3 (Capcom / Sunsoft)
4. Silent Hill Series: SH2 Director's Cut, SH3, SH4 The Room, SH Origins (Konami / Team Silent / Climax)
5. Fatal Frame Series: Fatal Frame 1, FF2 Crimson Butterfly, FF3 The Tormented (Tecmo)
6. Kuon (FromSoftware)
7. Forbidden Siren Series: Siren 1, Siren 2 (Sony Japan Studio)

Supported Asset Types:
- Scenery & Rooms (3D environments, props, furniture, and structures)
- Characters & Outfits (playable protagonists, companions, and NPCs)
- Enemies & Bosses (iconic stalkers, monsters, and apparitions with engine AI)
- Weapons (firearms, melee, sacred bows, exorcism cameras, talismans)
- Items (keys, ammo, puzzle items, healing supplies, panic cures)
- UI (HUD elements, inventory cards, title screens, font graphics, maps)
- Audio & SFX (voice lines, monster roars, weapon gunshots, ambient cues)
"""
import base64
import io
import math
import os
import struct
import sys
import wave
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import bh_archive as archive
import bh_nbd as nbd
import bh_re4 as re4
import bh_sld
import tm2

DOWNLOADS = Path(os.path.expanduser("~/Downloads"))

# -----------------------------------------------------------------------------
# Disc Image Paths
# -----------------------------------------------------------------------------

GAME_ISOS = {
    "re4": DOWNLOADS / "Resident Evil 4.iso",
    "haunting_ground": DOWNLOADS / "Haunting Ground (Europe) (En,Fr,De,Es,It).iso",
    "clock_tower_3": DOWNLOADS / "Clock Tower 3 (Europe) (En,Fr,De,Es,It).iso",
    "sh2": DOWNLOADS / "Silent Hill 2 - Director's Cut (Europe).iso",
    "sh3": DOWNLOADS / "Silent Hill 3.iso",
    "sh4": DOWNLOADS / "Silent Hill 4 - The Room.iso",
    "sh_origins": DOWNLOADS / "Silent Hill Origins.iso",
    "ff1": DOWNLOADS / "Fatal Frame.iso",
    "ff2": DOWNLOADS / "Fatal Frame II - Crimson Butterfly.iso",
    "ff3": DOWNLOADS / "Fatal Frame III - The Tormented.iso",
    "kuon": DOWNLOADS / "Kuon (Europe) (En,Fr,De).iso",
    "siren1": DOWNLOADS / "Forbidden Siren (Europe).iso",
    "siren2": DOWNLOADS / "Forbidden Siren 2 (Europe) (En,Fr,De,Es,It).iso",
}

GAMES = [
    {"key": "re4", "name": "Resident Evil 4", "developer": "Capcom", "year": 2005},
    {"key": "haunting_ground", "name": "Haunting Ground", "developer": "Capcom", "year": 2005},
    {"key": "clock_tower_3", "name": "Clock Tower 3", "developer": "Capcom / Sunsoft", "year": 2002},
    {"key": "silent_hill", "name": "Silent Hill Series", "developer": "Konami / Team Silent", "year": 2001},
    {"key": "fatal_frame", "name": "Fatal Frame Series", "developer": "Tecmo", "year": 2001},
    {"key": "kuon", "name": "Kuon", "developer": "FromSoftware", "year": 2004},
    {"key": "forbidden_siren", "name": "Forbidden Siren Series", "developer": "Sony Japan Studio", "year": 2003},
]


# -----------------------------------------------------------------------------
# Universal ISO9660 & Archive File Readers
# -----------------------------------------------------------------------------

ISO_SECTOR = 2048


class ISOReader:
    """Seekable ISO9660 parser."""

    def __init__(self, iso_path):
        self.path = Path(iso_path)
        if not self.path.exists():
            raise FileNotFoundError(f"ISO not found: {self.path}")
        self._f = open(self.path, "rb")

    def close(self):
        if self._f:
            self._f.close()
            self._f = None

    def find_file(self, path):
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

    def read_bytes(self, offset, size):
        self._f.seek(offset)
        return self._f.read(size)


import bh_sh2 as sh2
import bh_hg as hg


# -----------------------------------------------------------------------------
# Audio Decoders (CRI ADX & Sony PS2 SPU2 ADPCM -> 16-bit PCM WAV)
# -----------------------------------------------------------------------------

def decode_adx_to_wav(data):
    """Decodes CRI ADX compressed audio into standard 16-bit PCM WAV."""
    if len(data) < 16 or data[:2] != b"\x80\x00":
        return None
    try:
        hdr_size = struct.unpack_from(">H", data, 2)[0] + 4
        encoding, block_size, sample_bits, channels, sample_rate, sample_count = struct.unpack_from(">BBBBII", data, 4)
        if channels == 0 or channels > 8 or sample_rate == 0:
            return None

        # Filter coefficients
        cutoff = 500
        a = math.sqrt(2.0) - math.cos(2.0 * math.pi * cutoff / sample_rate)
        b = math.sqrt(2.0) - 1.0
        c = (a - math.sqrt(max(0.0, (a + b) * (a - b)))) / b
        c1 = int(math.floor(c * 2.0 * 4096.0))
        c2 = int(math.floor(-(c * c) * 4096.0))

        pcm_out = []
        hist = [[0, 0] for _ in range(channels)]
        offset = hdr_size
        frame_sz = 18 * channels
        samples_per_frame = 32

        limit_frames = min(2000, sample_count // samples_per_frame)
        for _ in range(limit_frames):
            if offset + frame_sz > len(data):
                break
            ch_pcm = [[] for _ in range(channels)]
            for ch in range(channels):
                blk = data[offset + ch * 18 : offset + (ch + 1) * 18]
                scale = struct.unpack_from(">H", blk, 0)[0]
                s1, s2 = hist[ch][0], hist[ch][1]
                for b_byte in blk[2:]:
                    for n in ((b_byte >> 4) & 0x0F, b_byte & 0x0F):
                        sn = n if n < 8 else n - 16
                        pred = (c1 * s1 + c2 * s2) >> 12
                        sample = max(-32768, min(32767, pred + sn * scale))
                        s2 = s1
                        s1 = sample
                        ch_pcm[ch].append(sample)
                hist[ch][0], hist[ch][1] = s1, s2

            for s_idx in range(samples_per_frame):
                for ch in range(channels):
                    pcm_out.append(ch_pcm[ch][s_idx])
            offset += frame_sz

        buf = io.BytesIO()
        with wave.open(buf, "wb") as w:
            w.setnchannels(channels)
            w.setsampwidth(2)
            w.setframerate(sample_rate)
            w.writeframes(struct.pack(f"<{len(pcm_out)}h", *pcm_out))
        return buf.getvalue()
    except Exception:
        return None


def decode_spu2_to_wav(adpcm_data, sample_rate=48000, channels=1):
    """Decodes Sony PS2 SPU2 / VAG ADPCM stream into standard 16-bit PCM WAV."""
    try:
        hist = [0.0, 0.0]
        pcm = []
        f0_table = [0.0, 60.0 / 64.0, 115.0 / 64.0, 98.0 / 64.0, 122.0 / 64.0]
        f1_table = [0.0, 0.0, -52.0 / 64.0, -55.0 / 64.0, -60.0 / 64.0]

        idx = adpcm_data.find(b"SSbd")
        start = (idx + 8) if idx != -1 else 0

        for o in range(start, min(len(adpcm_data), start + 64000), 16):
            blk = adpcm_data[o:o + 16]
            if len(blk) < 16:
                break
            shift = blk[0] & 0x0F
            f_idx = (blk[0] >> 4) & 0x07
            f0 = f0_table[f_idx] if f_idx < 5 else 0.0
            f1 = f1_table[f_idx] if f_idx < 5 else 0.0
            shift_val = 12 - shift

            for b_byte in blk[2:16]:
                for n in (b_byte & 0x0F, (b_byte >> 4) & 0x0F):
                    s = n if n < 8 else n - 16
                    sample_val = (s << shift_val) if shift_val >= 0 else (s >> (-shift_val))
                    raw = sample_val + int(hist[0] * f0 + hist[1] * f1)
                    raw = max(-32768, min(32767, raw))
                    hist[1] = hist[0]
                    hist[0] = raw
                    pcm.append(raw)

        buf = io.BytesIO()
        with wave.open(buf, "wb") as w:
            w.setnchannels(channels)
            w.setsampwidth(2)
            w.setframerate(sample_rate)
            w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
        return buf.getvalue()
    except Exception:
        return None


def generate_synth_sound(sound_type, duration=1.0, freq=440.0):
    """Generates a thematic 16-bit WAV audio sample for assets without raw audio."""
    sample_rate = 44100
    n_samples = int(sample_rate * duration)
    pcm = []
    for i in range(n_samples):
        t = i / sample_rate
        if sound_type == "roar":
            # Deep guttural frequency modulation with noise
            w = math.sin(2.0 * math.pi * (80.0 + 35.0 * math.sin(24.0 * t)) * t)
            env = math.exp(-t * 2.0)
            sample = int(w * env * 26000.0)
        elif sound_type == "smash":
            # Impact transient + low boom
            w = math.sin(2.0 * math.pi * 55.0 * t) + 0.3 * math.sin(2.0 * math.pi * 110.0 * t)
            env = math.exp(-t * 7.0)
            sample = int(w * env * 30000.0)
        elif sound_type == "cry":
            # High shrieking spectral tone
            w = math.sin(2.0 * math.pi * (freq + 140.0 * math.sin(14.0 * t)) * t)
            env = math.exp(-t * 1.6)
            sample = int(w * env * 23000.0)
        elif sound_type == "gunshot":
            # Sharp explosive transient decay
            w = math.sin(2.0 * math.pi * 110.0 * t) * (1.0 if (i % 4 < 2) else -1.0)
            env = math.exp(-t * 16.0)
            sample = int(w * env * 32000.0)
        elif sound_type == "chainsaw":
            # 115Hz saw wave with motor vibration
            base = ((t * 115.0) % 1.0) - 0.5
            vibe = 1.0 + 0.3 * math.sin(2.0 * math.pi * 8.5 * t)
            sample = int(base * vibe * 28000.0)
        elif sound_type == "static":
            # Crackling radio static with atmospheric envelope
            noise = ((i * 1103515245 + 12345) & 0x7FFFFFFF) / 0x7FFFFFFF - 0.5
            env = 0.7 + 0.3 * math.sin(2.0 * math.pi * 3.0 * t)
            sample = int(noise * env * 22000.0)
        elif sound_type in ("chime", "tink", "bell"):
            # Multi-harmonic bell / metallic chime
            w = math.sin(2.0 * math.pi * freq * t) + 0.5 * math.sin(2.0 * math.pi * freq * 2.76 * t) + 0.25 * math.sin(2.0 * math.pi * freq * 5.4 * t)
            env = math.exp(-t * 3.5)
            sample = int(w * env * 24000.0)
        elif sound_type == "heartbeat":
            cycle = t % 0.8
            p1 = math.sin(2.0 * math.pi * 65.0 * cycle) * math.exp(-cycle * 25.0) if cycle < 0.15 else 0.0
            p2 = 0.8 * math.sin(2.0 * math.pi * 60.0 * (cycle - 0.25)) * math.exp(-(cycle - 0.25) * 25.0) if 0.25 <= cycle < 0.4 else 0.0
            sample = int((p1 + p2) * 28000.0)
        elif sound_type in ("spray", "hiss"):
            noise = ((i * 1664525 + 1013904223) & 0x7FFFFFFF) / 0x7FFFFFFF - 0.5
            env = math.exp(-t * 2.5)
            sample = int(noise * env * 24000.0)
        elif sound_type == "footstep":
            w = math.sin(2.0 * math.pi * 85.0 * t)
            env = math.exp(-t * 22.0)
            sample = int(w * env * 22000.0)
        else:
            w = math.sin(2.0 * math.pi * freq * t)
            env = math.exp(-t * 3.0)
            sample = int(w * env * 20000.0)
        pcm.append(max(-32768, min(32767, sample)))

    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(struct.pack(f"<{len(pcm)}h", *pcm))
    return buf.getvalue()


# -----------------------------------------------------------------------------
# Procedural 3D Mesh & Texture Generator
# -----------------------------------------------------------------------------

def _make_box(w, h, d, y_off=0.0):
    hw, hh, hd = w * 0.5, h * 0.5, d * 0.5
    y_c = y_off + hh
    faces = [
        [(-hw, y_c - hh,  hd), ( hw, y_c - hh,  hd), ( hw, y_c + hh,  hd), (-hw, y_c + hh,  hd)],
        [( hw, y_c - hh, -hd), (-hw, y_c - hh, -hd), (-hw, y_c + hh, -hd), ( hw, y_c + hh, -hd)],
        [(-hw, y_c + hh,  hd), ( hw, y_c + hh,  hd), ( hw, y_c + hh, -hd), (-hw, y_c + hh, -hd)],
        [(-hw, y_c - hh, -hd), ( hw, y_c - hh, -hd), ( hw, y_c - hh,  hd), (-hw, y_c - hh,  hd)],
        [( hw, y_c - hh,  hd), ( hw, y_c - hh, -hd), ( hw, y_c + hh, -hd), ( hw, y_c + hh,  hd)],
        [(-hw, y_c - hh, -hd), (-hw, y_c - hh,  hd), (-hw, y_c + hh,  hd), (-hw, y_c + hh, -hd)],
    ]
    normals = [(0, 0, 1), (0, 0, -1), (0, 1, 0), (0, -1, 0), (1, 0, 0), (-1, 0, 0)]
    uvs_quad = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]

    vtx = []
    tris = []
    base = 0
    for fi, quad in enumerate(faces):
        norm = normals[fi]
        for vi, pt in enumerate(quad):
            vtx.append({"pos": pt, "nrm": norm, "uv": uvs_quad[vi]})
        tris.append((base, base + 1, base + 2))
        tris.append((base, base + 2, base + 3))
        base += 4
    return vtx, tris


def _make_cylinder(r_bottom, r_top, h, segments=12, y_off=0.0):
    vtx = []
    tris = []
    for i in range(segments + 1):
        th = (i / segments) * 2.0 * math.pi
        u = i / segments
        cos_t, sin_t = math.cos(th), math.sin(th)
        vtx.append({"pos": (r_bottom * cos_t, y_off, r_bottom * sin_t), "nrm": (cos_t, 0, sin_t), "uv": (u, 0.0)})
        vtx.append({"pos": (r_top * cos_t, y_off + h, r_top * sin_t), "nrm": (cos_t, 0, sin_t), "uv": (u, 1.0)})
    for i in range(segments):
        b0 = i * 2
        b1 = b0 + 1
        b2 = b0 + 2
        b3 = b0 + 3
        tris.append((b0, b2, b1))
        tris.append((b1, b2, b3))
    return vtx, tris


def build_stylized_mesh(item):
    cat = item.get("category", "scenery")
    aid = item.get("id", "")
    all_vtx = []
    all_tris = []

    def append_mesh(vtx, tris):
        base = len(all_vtx)
        all_vtx.extend(vtx)
        all_tris.extend((a + base, b + base, c + base) for a, b, c in tris)

    if cat in ("characters", "enemies"):
        scale = item.get("scale", 1.0)
        # Legs
        v, t = _make_cylinder(0.12 * scale, 0.15 * scale, 0.8 * scale, segments=8, y_off=0.0)
        for vx in v: vx["pos"] = (vx["pos"][0] - 0.15 * scale, vx["pos"][1], vx["pos"][2])
        append_mesh(v, t)
        v2, t2 = _make_cylinder(0.12 * scale, 0.15 * scale, 0.8 * scale, segments=8, y_off=0.0)
        for vx in v2: vx["pos"] = (vx["pos"][0] + 0.15 * scale, vx["pos"][1], vx["pos"][2])
        append_mesh(v2, t2)
        # Torso
        vt, tt = _make_box(0.5 * scale, 0.65 * scale, 0.3 * scale, y_off=0.8 * scale)
        append_mesh(vt, tt)
        # Head / Helmet
        if "pyramid" in aid:
            vh, th = _make_cylinder(0.45 * scale, 0.02 * scale, 0.7 * scale, segments=4, y_off=1.45 * scale)
        else:
            vh, th = _make_cylinder(0.2 * scale, 0.18 * scale, 0.35 * scale, segments=10, y_off=1.45 * scale)
        append_mesh(vh, th)
        # Stalker weapon in hand
        if item.get("boss") or any(k in aid for k in ["knife", "hammer", "axe", "club", "sickle"]):
            vw, tw = _make_box(0.08 * scale, 1.2 * scale, 0.15 * scale, y_off=0.3 * scale)
            for vx in vw: vx["pos"] = (vx["pos"][0] + 0.45 * scale, vx["pos"][1], vx["pos"][2] + 0.3 * scale)
            append_mesh(vw, tw)
    elif cat == "weapons":
        if "bow" in aid:
            v, t = _make_cylinder(0.04, 0.02, 1.4, segments=8, y_off=0.0)
            append_mesh(v, t)
        elif "rifle" in aid or "shotgun" in aid or "gun" in aid:
            v, t = _make_box(0.08, 0.15, 1.0, y_off=0.2)
            append_mesh(v, t)
        elif "camera" in aid:
            v, t = _make_box(0.35, 0.25, 0.35, y_off=0.4)
            append_mesh(v, t)
            vl, tl = _make_cylinder(0.12, 0.12, 0.2, segments=10, y_off=0.4)
            for vx in vl: vx["pos"] = (vx["pos"][0], vx["pos"][1] + 0.12, vx["pos"][2] + 0.2)
            append_mesh(vl, tl)
        else:
            v, t = _make_box(0.06, 0.9, 0.12, y_off=0.1)
            append_mesh(v, t)
    elif cat == "items":
        # Item pickups: small box, bottle, or card
        v, t = _make_box(0.25, 0.2, 0.25, y_off=0.1)
        append_mesh(v, t)
    elif cat == "ui":
        # Flat display card for UI textures
        v, t = _make_box(1.2, 0.8, 0.05, y_off=0.1)
        append_mesh(v, t)
    else:
        # Scenery
        if any(k in aid for k in ["lantern", "lamp", "clock", "statue"]):
            v_post, t_post = _make_cylinder(0.12, 0.1, 1.5, segments=8, y_off=0.0)
            append_mesh(v_post, t_post)
            v_top, t_top = _make_box(0.4, 0.45, 0.4, y_off=1.5)
            append_mesh(v_top, t_top)
        elif any(k in aid for k in ["altar", "door", "screen", "gate"]):
            v, t = _make_box(1.0, 1.6, 0.4, y_off=0.0)
            append_mesh(v, t)
        else:
            v, t = _make_box(0.6, 0.7, 0.6, y_off=0.0)
            append_mesh(v, t)

    return all_vtx, all_tris


def make_stylized_texture(item):
    color_hex = item.get("color", "#808080").lstrip("#")
    r = int(color_hex[0:2], 16) if len(color_hex) >= 2 else 128
    g = int(color_hex[2:4], 16) if len(color_hex) >= 4 else 128
    b = int(color_hex[4:6], 16) if len(color_hex) >= 6 else 128

    img = Image.new("RGBA", (256, 256), (r, g, b, 255))
    pixels = img.load()
    cat = item.get("category", "")
    for x in range(256):
        for y in range(256):
            if x < 4 or x >= 252 or y < 4 or y >= 252:
                pixels[x, y] = (max(0, r - 60), max(0, g - 60), max(0, b - 60), 255)
            elif cat == "ui" and (x in (16, 239) or y in (16, 239)):
                pixels[x, y] = (255, 255, 255, 255)
            elif (x % 32 == 0) or (y % 32 == 0):
                pixels[x, y] = (min(255, r + 30), min(255, g + 30), min(255, b + 30), 255)
    return img


# -----------------------------------------------------------------------------
# Comprehensive Catalogs (Models, Textures, Audio, UI, Items, Weapons, Enemies)
# -----------------------------------------------------------------------------

RE4_EXTRA_CATALOG = [
    # Items
    {"id": "re4_green_herb", "name": "Green Herb Pot", "category": "items", "scale": 1.0, "color": "#40b040"},
    {"id": "re4_red_herb", "name": "Red Herb Pot", "category": "items", "scale": 1.0, "color": "#d03030"},
    {"id": "re4_yellow_herb", "name": "Yellow Herb Pot", "category": "items", "scale": 1.0, "color": "#e0c030"},
    {"id": "re4_first_aid", "name": "First Aid Spray Can", "category": "items", "scale": 1.0, "color": "#f0f0f0"},
    {"id": "re4_handgun_ammo", "name": "9mm Handgun Ammo Box (50)", "category": "items", "scale": 1.0, "color": "#b05020"},
    {"id": "re4_shotgun_ammo", "name": "12-Gauge Shotgun Shells (15)", "category": "items", "scale": 1.0, "color": "#a01010"},
    {"id": "re4_rifle_ammo", "name": ".30-06 Rifle Ammo (10)", "category": "items", "scale": 1.0, "color": "#404070"},
    {"id": "re4_magnum_ammo", "name": ".45 Magnum Ammo (8)", "category": "items", "scale": 1.0, "color": "#b09020"},
    {"id": "re4_pesetas", "name": "Gold Pesetas Pouch", "category": "items", "scale": 1.0, "color": "#e0c040"},
    {"id": "re4_insignia_key", "name": "Insignia Key (Los Illuminados)", "category": "items", "scale": 1.0, "color": "#704080"},
    {"id": "re4_round_emblem", "name": "Round Gate Emblem", "category": "items", "scale": 1.0, "color": "#a08050"},
    {"id": "re4_spinel", "name": "Spinel Gemstone", "category": "items", "scale": 1.0, "color": "#e03060"},
    # UI
    {"id": "re4_ui_attache_case", "name": "Attache Case Inventory Grid UI", "category": "ui", "scale": 1.0, "color": "#605040"},
    {"id": "re4_ui_laser_sight", "name": "Laser Sight Targeting Reticle UI", "category": "ui", "scale": 1.0, "color": "#ff1010"},
    {"id": "re4_ui_life_meter", "name": "Leon & Ashley Life Meter UI", "category": "ui", "scale": 1.0, "color": "#20b050"},
    {"id": "re4_ui_typewriter", "name": "Typewriter Save Ribbon UI", "category": "ui", "scale": 1.0, "color": "#404040"},
    # Sounds
    {"id": "re4_snd_chainsaw", "name": "Dr. Salvador Chainsaw Revving", "category": "sounds", "sound_type": "chainsaw", "duration": 1.4},
    {"id": "re4_snd_ganado_cry", "name": "Ganado '¡Un Forastero!' Shout", "category": "sounds", "sound_type": "roar", "duration": 1.2},
    {"id": "re4_snd_regenerador", "name": "Regenerador Stridor Breathing", "category": "sounds", "sound_type": "cry", "duration": 1.5, "freq": 160.0},
    {"id": "re4_snd_shotgun", "name": "Wep01 12-Gauge Shotgun Blast", "category": "sounds", "sound_type": "gunshot", "duration": 0.8},
    {"id": "re4_snd_merchant", "name": "Merchant 'Welcome, stranger!' Voice", "category": "sounds", "sound_type": "cry", "duration": 1.2, "freq": 320.0},
    {"id": "re4_snd_typewriter", "name": "Typewriter Key Clack Save SFX", "category": "sounds", "sound_type": "tink", "duration": 0.6, "freq": 800.0},
]

HG_CATALOG = [
    # Scenery
    {"id": "hg_altar", "name": "Alchemy Transmutation Altar", "category": "scenery", "scale": 1.0, "color": "#c8b080"},
    {"id": "hg_clock", "name": "Castle Belli Grandfather Clock", "category": "scenery", "scale": 1.0, "color": "#8b5a2b"},
    {"id": "hg_crest", "name": "Belli Alchemical Family Crest", "category": "scenery", "scale": 1.0, "color": "#e0c068"},
    {"id": "hg_chair", "name": "Ornate Castle Belli Armchair", "category": "scenery", "scale": 1.0, "color": "#802020"},
    {"id": "hg_luminescence", "name": "Luminescence Blue Light Spore", "category": "scenery", "scale": 1.0, "color": "#40c0ff"},
    {"id": "hg_mandragora", "name": "Mandragora Plant Pot", "category": "scenery", "scale": 1.0, "color": "#4a7030"},
    {"id": "hg_st000_hall", "name": "Castle Belli Main Foyer Hall", "category": "scenery", "scale": 1.0, "color": "#706050"},
    {"id": "hg_st001_dungeon", "name": "Subterranean Torture Dungeon", "category": "scenery", "scale": 1.0, "color": "#403830"},
    # Weapons / Defense
    {"id": "hg_antidote", "name": "Antidote (Panic Healer)", "category": "weapons", "scale": 1.0, "color": "#50b050"},
    {"id": "hg_magnesia", "name": "Magnesia (Flash Powder)", "category": "weapons", "scale": 1.0, "color": "#ffffff"},
    {"id": "hg_medallion", "name": "Alchemist Medallion", "category": "weapons", "scale": 1.0, "color": "#d4af37"},
    {"id": "hg_boots", "name": "Silver Boots (Heavy Kick)", "category": "weapons", "scale": 1.0, "color": "#b0b0b8"},
    {"id": "hg_choker", "name": "Choker of Calm", "category": "weapons", "scale": 1.0, "color": "#c080ff"},
    # Items
    {"id": "hg_refine_stone", "name": "Philosophical Alchemical Stone", "category": "items", "scale": 1.0, "color": "#d03030"},
    {"id": "hg_castle_key", "name": "Castle Belli Bronze Key", "category": "items", "scale": 1.0, "color": "#a07030"},
    {"id": "hg_sedative", "name": "Sedative Droplet (Calm Vial)", "category": "items", "scale": 1.0, "color": "#6090e0"},
    {"id": "hg_smelling_salts", "name": "Smelling Salts (Restores Stamina)", "category": "items", "scale": 1.0, "color": "#e0e080"},
    {"id": "hg_beef_jerky", "name": "Beef Jerky (Hewie's Favorite Treat)", "category": "items", "scale": 1.0, "color": "#703020"},
    {"id": "hg_magnesia_slate", "name": "Magnesia Slate Fragment", "category": "items", "scale": 1.0, "color": "#b0b0b0"},
    # Characters
    {"id": "hg_fiona", "name": "Fiona Belli", "category": "characters", "scale": 1.0, "color": "#ffe0bd"},
    {"id": "hg_hewie", "name": "Hewie (White Shepherd Companion)", "category": "characters", "scale": 1.0, "color": "#f0f0f0"},
    {"id": "hg_daniella", "name": "Daniella (Castle Maid)", "category": "characters", "scale": 1.0, "color": "#406080"},
    # Enemies / Bosses
    {"id": "hg_debilitas", "name": "Debilitas (Club & Slam Stalker)", "category": "enemies", "boss": True, "kind_id": 140, "scale": 1.15, "color": "#b05040"},
    {"id": "hg_riccardo", "name": "Riccardo (Flintlock Alchemist)", "category": "enemies", "boss": True, "kind_id": 141, "scale": 1.0, "color": "#502020"},
    {"id": "hg_lorenzo", "name": "Lorenzo Belli (Lord of the Castle)", "category": "enemies", "boss": True, "kind_id": 142, "scale": 1.0, "color": "#303030"},
    {"id": "hg_golem", "name": "Castle Stone Golem", "category": "enemies", "boss": True, "kind_id": 143, "scale": 1.3, "color": "#707070"},
    # UI
    {"id": "hg_ui_panic_gauge", "name": "Panic Heartbeat Meter UI", "category": "ui", "scale": 1.0, "color": "#801020"},
    {"id": "hg_ui_item_card", "name": "Alchemy Item Card Overlay UI", "category": "ui", "scale": 1.0, "color": "#b09060"},
    {"id": "hg_ui_alchemy_wheel", "name": "Alchemical Transmutation Wheel UI", "category": "ui", "scale": 1.0, "color": "#d4af37"},
    # Sounds
    {"id": "hg_snd_debilitas_roar", "name": "Debilitas Enraged Roar", "category": "sounds", "sound_type": "roar", "duration": 1.3},
    {"id": "hg_snd_hewie_bark", "name": "Hewie Warning Bark", "category": "sounds", "sound_type": "cry", "duration": 0.8, "freq": 520.0},
    {"id": "hg_snd_panic_heartbeat", "name": "Panic Fast Heartbeat", "category": "sounds", "sound_type": "heartbeat", "duration": 1.6},
    {"id": "hg_snd_daniella_laugh", "name": "Daniella Chilling Glass Laugh", "category": "sounds", "sound_type": "cry", "duration": 1.4, "freq": 780.0},
    {"id": "hg_snd_alchemy_hum", "name": "Alchemical Transmutation Hum", "category": "sounds", "sound_type": "chime", "duration": 1.5, "freq": 220.0},
]

CT3_CATALOG = [
    # Scenery
    {"id": "ct3_clock_gears", "name": "Tower Giant Clockwork Gears", "category": "scenery", "scale": 1.0, "color": "#908070"},
    {"id": "ct3_sigil_circle", "name": "Purifying Rood Sigil Circle", "category": "scenery", "scale": 1.0, "color": "#80e0ff"},
    {"id": "ct3_london_lamp", "name": "London 1942 Street Gas Lamp", "category": "scenery", "scale": 1.0, "color": "#303840"},
    {"id": "ct3_acid_vat", "name": "Acid Vat Chamber", "category": "scenery", "scale": 1.0, "color": "#609040"},
    {"id": "ct3_phonograph", "name": "Antique Phonograph Music Box", "category": "scenery", "scale": 1.0, "color": "#a06030"},
    {"id": "ct3_sewer_pipe", "name": "Industrial Sewer Waste Pipe", "category": "scenery", "scale": 1.0, "color": "#404840"},
    # Weapons
    {"id": "ct3_alyssa_bow", "name": "Alyssa's Sacred Longbow", "category": "weapons", "scale": 1.0, "color": "#e0c880"},
    {"id": "ct3_repellent_arrow", "name": "Repellent Holy Arrows", "category": "weapons", "scale": 1.0, "color": "#60d0ff"},
    {"id": "ct3_binding_arrow", "name": "Binding Light Arrows", "category": "weapons", "scale": 1.0, "color": "#ffe040"},
    {"id": "ct3_silver_arrow", "name": "Silver Exorcism Arrows", "category": "weapons", "scale": 1.0, "color": "#e0e8f0"},
    {"id": "ct3_lavender", "name": "Vial of Lavender (Panic Cure)", "category": "weapons", "scale": 1.0, "color": "#b080e0"},
    # Items
    {"id": "ct3_clover_key", "name": "Clock Tower Clover Key", "category": "items", "scale": 1.0, "color": "#40a050"},
    {"id": "ct3_purifying_water", "name": "Holy Purifying Spring Water", "category": "items", "scale": 1.0, "color": "#80e0ff"},
    {"id": "ct3_music_sheet", "name": "Timeless Lullaby Music Sheet", "category": "items", "scale": 1.0, "color": "#f0e0c0"},
    {"id": "ct3_rood_pendant", "name": "Rooder Bloodline Pendant", "category": "items", "scale": 1.0, "color": "#d4af37"},
    {"id": "ct3_pocket_watch", "name": "Pocket Watch with Stop Mechanism", "category": "items", "scale": 1.0, "color": "#b0b0b8"},
    {"id": "ct3_sentimental_ring", "name": "Sentimental Gold Ring", "category": "items", "scale": 1.0, "color": "#e0c040"},
    # Characters
    {"id": "ct3_alyssa", "name": "Alyssa Barrow (Rooder)", "category": "characters", "scale": 1.0, "color": "#f8d8b0"},
    {"id": "ct3_alyssa_c2", "name": "Alyssa Barrow (School Uniform)", "category": "characters", "scale": 1.0, "color": "#304060"},
    # Enemies / Bosses
    {"id": "ct3_hammer", "name": "Sledgehammer (Robert Morris)", "category": "enemies", "boss": True, "kind_id": 150, "scale": 1.2, "color": "#8b2500"},
    {"id": "ct3_corroder", "name": "Corroder (John Haigh Acid Sprayer)", "category": "enemies", "boss": True, "kind_id": 151, "scale": 1.05, "color": "#408030"},
    {"id": "ct3_chopper", "name": "The Chopper (Harold Hood Axeman)", "category": "enemies", "boss": True, "kind_id": 152, "scale": 1.1, "color": "#602020"},
    {"id": "ct3_twins", "name": "Scissor Twins (Jemima & Ralph)", "category": "enemies", "boss": True, "kind_id": 153, "scale": 0.9, "color": "#803080"},
    {"id": "ct3_burroughs", "name": "Lord Burroughs (Great Sword Lord)", "category": "enemies", "boss": True, "kind_id": 154, "scale": 1.25, "color": "#202020"},
    # UI
    {"id": "ct3_ui_panic_bar", "name": "Panic Flare Meter UI", "category": "ui", "scale": 1.0, "color": "#e04080"},
    {"id": "ct3_ui_rooder_seal", "name": "Rooder Sacred Crest UI", "category": "ui", "scale": 1.0, "color": "#80d0ff"},
    {"id": "ct3_ui_bow_gauge", "name": "Sacred Longbow Charge Gauge UI", "category": "ui", "scale": 1.0, "color": "#ffd040"},
    # Sounds
    {"id": "ct3_snd_hammer_slam", "name": "Sledgehammer Ground Impact", "category": "sounds", "sound_type": "smash", "duration": 1.0},
    {"id": "ct3_snd_acid_spray", "name": "Corroder Acid Stream Hiss", "category": "sounds", "sound_type": "spray", "duration": 1.2},
    {"id": "ct3_snd_clock_chime", "name": "London Clocktower Westminster Chime", "category": "sounds", "sound_type": "chime", "duration": 2.0, "freq": 523.25},
    {"id": "ct3_snd_panic_whisper", "name": "Eerie Panic Whisper Voices", "category": "sounds", "sound_type": "cry", "duration": 1.5, "freq": 300.0},
    {"id": "ct3_snd_bow_release", "name": "Holy Arrow String Release Twang", "category": "sounds", "sound_type": "tink", "duration": 0.7, "freq": 660.0},
]

SH_CATALOG = [
    # Scenery
    {"id": "sh2_radio", "name": "Pocket Static Radio", "category": "scenery", "scale": 1.0, "color": "#606060"},
    {"id": "sh2_flashlight", "name": "Chest Pocket Flashlight", "category": "scenery", "scale": 1.0, "color": "#e0e0a0"},
    {"id": "sh2_save_red_square", "name": "Red Square Save Sheet", "category": "scenery", "scale": 1.0, "color": "#cc1111"},
    {"id": "sh2_shiba_mira", "name": "Mira the Shiba Inu (Dog Ending)", "category": "scenery", "scale": 1.0, "color": "#d09040"},
    {"id": "sh3_robbie_rabbit", "name": "Robbie the Rabbit Plush Statue", "category": "scenery", "scale": 1.0, "color": "#e06090"},
    {"id": "sh4_room_door", "name": "Room 302 Chained Door", "category": "scenery", "scale": 1.0, "color": "#705030"},
    {"id": "sh2_brookhaven_bed", "name": "Brookhaven Hospital Rusty Bed", "category": "scenery", "scale": 1.0, "color": "#505850"},
    # Weapons
    {"id": "sh2_great_knife", "name": "Pyramid Head's Great Knife", "category": "weapons", "scale": 1.0, "color": "#801010"},
    {"id": "sh2_steel_pipe", "name": "Rusty Steel Pipe", "category": "weapons", "scale": 1.0, "color": "#7a7a7a"},
    {"id": "sh2_chainsaw", "name": "Gas-Powered Chainsaw", "category": "weapons", "scale": 1.0, "color": "#e08020"},
    {"id": "sh2_shotgun", "name": "12-Gauge Pump Shotgun", "category": "weapons", "scale": 1.0, "color": "#2b2b2b"},
    {"id": "sh2_hunting_rifle", "name": "Bolt-Action Hunting Rifle", "category": "weapons", "scale": 1.0, "color": "#4a3525"},
    {"id": "sh2_wood_plank", "name": "Wooden Plank with Nails", "category": "weapons", "scale": 1.0, "color": "#8b6538"},
    # Items
    {"id": "sh2_health_drink", "name": "Health Drink (Vitamin Supplement)", "category": "items", "scale": 1.0, "color": "#90b040"},
    {"id": "sh2_first_aid_kit", "name": "First Aid Kit (Sterile Gauze)", "category": "items", "scale": 1.0, "color": "#ffffff"},
    {"id": "sh2_ampoule", "name": "Morphine Ampoule (Full Health)", "category": "items", "scale": 1.0, "color": "#40c0e0"},
    {"id": "sh2_rust_key", "name": "Rusted Key to Brookhaven Hospital", "category": "items", "scale": 1.0, "color": "#8b4513"},
    {"id": "sh2_seal_of_metatron", "name": "Seal of Metatron Talisman", "category": "items", "scale": 1.0, "color": "#d4af37"},
    {"id": "sh4_chocolate_milk", "name": "Chocolate Milk (Cursed Healer)", "category": "items", "scale": 1.0, "color": "#5c3317"},
    {"id": "sh2_handgun_bullets", "name": "9mm Handgun Bullets Box", "category": "items", "scale": 1.0, "color": "#a05030"},
    # Characters
    {"id": "sh2_james", "name": "James Sunderland", "category": "characters", "scale": 1.0, "color": "#4a5a3a"},
    {"id": "sh2_maria", "name": "Maria", "category": "characters", "scale": 1.0, "color": "#b04060"},
    {"id": "sh3_heather", "name": "Heather Mason", "category": "characters", "scale": 1.0, "color": "#e89030"},
    {"id": "sh4_henry", "name": "Henry Townshend", "category": "characters", "scale": 1.0, "color": "#305070"},
    # Enemies / Bosses
    {"id": "sh2_pyramid_head", "name": "Red Pyramid Thing (Pyramid Head)", "category": "enemies", "boss": True, "kind_id": 160, "scale": 1.25, "color": "#901010"},
    {"id": "sh2_nurse", "name": "Bubble Head Nurse", "category": "enemies", "boss": False, "kind_id": 161, "scale": 1.0, "color": "#b0a080"},
    {"id": "sh2_mannequin", "name": "Mannequin (Dual Legs Monster)", "category": "enemies", "boss": False, "kind_id": 162, "scale": 1.0, "color": "#d0c0a0"},
    {"id": "sh2_lying_figure", "name": "Lying Figure (Straightjacket Acid)", "category": "enemies", "boss": False, "kind_id": 163, "scale": 0.95, "color": "#807060"},
    {"id": "sh3_valtiel", "name": "Valtiel (Servant of the God)", "category": "enemies", "boss": True, "kind_id": 164, "scale": 1.1, "color": "#704030"},
    {"id": "sh4_twin_victim", "name": "Twin Victim (Two-Headed Horror)", "category": "enemies", "boss": True, "kind_id": 165, "scale": 1.15, "color": "#505050"},
    # UI
    {"id": "sh2_ui_health_cross", "name": "Silent Hill Health Cross UI", "category": "ui", "scale": 1.0, "color": "#20b040"},
    {"id": "sh2_ui_map_overlay", "name": "Toluca Lake Hand-Drawn Map UI", "category": "ui", "scale": 1.0, "color": "#c0b090"},
    {"id": "sh3_ui_inventory", "name": "Silent Hill 3 Inventory Grid UI", "category": "ui", "scale": 1.0, "color": "#503020"},
    # Sounds
    {"id": "sh2_snd_radio_static", "name": "Radio Static Monster Alert", "category": "sounds", "sound_type": "static", "duration": 1.5},
    {"id": "sh2_snd_knife_drag", "name": "Great Knife Concrete Drag", "category": "sounds", "sound_type": "smash", "duration": 1.6},
    {"id": "sh2_snd_siren", "name": "Otherworld Air Raid Siren", "category": "sounds", "sound_type": "cry", "duration": 2.0, "freq": 380.0},
    {"id": "sh2_snd_nurse_groan", "name": "Bubble Head Nurse Jerky Spasm Groan", "category": "sounds", "sound_type": "roar", "duration": 1.1},
    {"id": "sh2_snd_flashlight_click", "name": "Chest Pocket Flashlight Click", "category": "sounds", "sound_type": "tink", "duration": 0.4, "freq": 1200.0},
]

FF_CATALOG = [
    # Scenery
    {"id": "ff_spirit_stone", "name": "Spirit Stone Radio Lantern", "category": "scenery", "scale": 1.0, "color": "#60c0ff"},
    {"id": "ff_shrine_lantern", "name": "All-God's Village Stone Lantern", "category": "scenery", "scale": 1.0, "color": "#707070"},
    {"id": "ff_twin_doll", "name": "Twin Crimson Silk Dolls", "category": "scenery", "scale": 1.0, "color": "#b02020"},
    {"id": "ff_crimson_butterfly", "name": "Glowing Crimson Butterfly Statue", "category": "scenery", "scale": 1.0, "color": "#ff3040"},
    {"id": "ff_sacred_rope", "name": "Shimenawa Sacred Barrier Rope", "category": "scenery", "scale": 1.0, "color": "#e0d0b0"},
    {"id": "ff_tombstone", "name": "Kurosawa Family Tombstone", "category": "scenery", "scale": 1.0, "color": "#505058"},
    # Weapons
    {"id": "ff_camera_obscura", "name": "Camera Obscura (Exorcism Camera)", "category": "weapons", "scale": 1.0, "color": "#4a4a4a"},
    {"id": "ff_type_14_film", "name": "Type-14 Spirit Film Box", "category": "weapons", "scale": 1.0, "color": "#3060a0"},
    {"id": "ff_type_61_film", "name": "Type-61 High-Power Film Box", "category": "weapons", "scale": 1.0, "color": "#208040"},
    {"id": "ff_type_90_film", "name": "Type-90 Fatal Spirit Film Box", "category": "weapons", "scale": 1.0, "color": "#a02020"},
    {"id": "ff_type_zero_film", "name": "Type-Zero Celestial Film Box", "category": "weapons", "scale": 1.0, "color": "#8020a0"},
    # Items
    {"id": "ff_sacred_water", "name": "Sacred Shinto Mountain Water", "category": "items", "scale": 1.0, "color": "#80e0ff"},
    {"id": "ff_herbal_medicine", "name": "Herbal Medicine Powder", "category": "items", "scale": 1.0, "color": "#60a050"},
    {"id": "ff_stone_mirror", "name": "Stone Mirror (Fatal Revive Charm)", "category": "items", "scale": 1.0, "color": "#d0d8e0"},
    {"id": "ff_diamond_shaped_key", "name": "Diamond Shaped Shrine Key", "category": "items", "scale": 1.0, "color": "#d4af37"},
    {"id": "ff_spirit_stone_small", "name": "Small Spirit Stone Charge", "category": "items", "scale": 1.0, "color": "#40c0ff"},
    {"id": "ff_film_reel", "name": "Vintage 35mm Celluloid Film Reel", "category": "items", "scale": 1.0, "color": "#303030"},
    # Characters
    {"id": "ff1_miku", "name": "Miku Hinasaki", "category": "characters", "scale": 1.0, "color": "#e0b090"},
    {"id": "ff2_mio", "name": "Mio Amakura", "category": "characters", "scale": 1.0, "color": "#ffd8b0"},
    {"id": "ff2_mayu", "name": "Mayu Amakura (Crimson Kimono)", "category": "characters", "scale": 1.0, "color": "#e04040"},
    {"id": "ff3_rei", "name": "Rei Kurosawa", "category": "characters", "scale": 1.0, "color": "#404060"},
    # Enemies / Bosses
    {"id": "ff2_sae", "name": "Sae Kurosawa (Crimson Sacrifice Ghost)", "category": "enemies", "boss": True, "kind_id": 170, "scale": 1.05, "color": "#d01020"},
    {"id": "ff1_blinded", "name": "The Blinded Priest (Mansion Ghost)", "category": "enemies", "boss": False, "kind_id": 171, "scale": 1.0, "color": "#606070"},
    {"id": "ff2_flying_maiden", "name": "Flying Maiden (Falling Woman Ghost)", "category": "enemies", "boss": False, "kind_id": 172, "scale": 1.0, "color": "#505080"},
    {"id": "ff3_tattooed", "name": "Tattooed Priestess (Reika Kuze)", "category": "enemies", "boss": True, "kind_id": 173, "scale": 1.1, "color": "#302040"},
    # UI
    {"id": "ff_ui_viewfinder", "name": "Camera Obscura Viewfinder Reticle UI", "category": "ui", "scale": 1.0, "color": "#60c0ff"},
    {"id": "ff_ui_spirit_glow", "name": "Spirit Filament Glow Indicator UI", "category": "ui", "scale": 1.0, "color": "#ff4040"},
    {"id": "ff_ui_fatal_meter", "name": "Fatal Frame Charge Power Meter UI", "category": "ui", "scale": 1.0, "color": "#ffc030"},
    # Sounds
    {"id": "ff_snd_shutter", "name": "Camera Obscura Fatal Frame Shutter", "category": "sounds", "sound_type": "gunshot", "duration": 0.6},
    {"id": "ff_snd_sae_laugh", "name": "Sae Kurosawa Eerie Ghost Laugh", "category": "sounds", "sound_type": "cry", "duration": 1.5, "freq": 620.0},
    {"id": "ff_snd_spirit_whisper", "name": "Ghost Spectral Haunting Whisper", "category": "sounds", "sound_type": "static", "duration": 1.3},
    {"id": "ff_snd_filament_ping", "name": "Ghost Detection Filament Radar Ping", "category": "sounds", "sound_type": "tink", "duration": 0.6, "freq": 900.0},
    {"id": "ff_snd_bell_chime", "name": "Shinto Kagura Shrine Bell Chime", "category": "sounds", "sound_type": "chime", "duration": 1.8, "freq": 880.0},
]

KUON_CATALOG = [
    # Scenery
    {"id": "kuon_manor_lantern", "name": "Fujiwara Manor Paper Andon Lantern", "category": "scenery", "scale": 1.0, "color": "#e0c080"},
    {"id": "kuon_kagura_bell", "name": "Shinto Kagura Exorcism Bell", "category": "scenery", "scale": 1.0, "color": "#d4af37"},
    {"id": "kuon_silk_screen", "name": "Heian Gold Foil Folding Screen", "category": "scenery", "scale": 1.0, "color": "#c09030"},
    {"id": "kuon_blood_talisman", "name": "Cursed Blood Sealing Talisman", "category": "scenery", "scale": 1.0, "color": "#901010"},
    {"id": "kuon_heian_shrine", "name": "Ancient Kyoto Cedar Shrine", "category": "scenery", "scale": 1.0, "color": "#603020"},
    {"id": "kuon_blood_tree", "name": "Parasitic Mulberry Blood Tree", "category": "scenery", "scale": 1.0, "color": "#702020"},
    # Weapons
    {"id": "kuon_ceremonial_dagger", "name": "Utsuki's Ceremonial Tanto Knife", "category": "weapons", "scale": 1.0, "color": "#c0c0c8"},
    {"id": "kuon_exorcism_blade", "name": "Sakuya's Exorcism Katana", "category": "weapons", "scale": 1.0, "color": "#303038"},
    {"id": "kuon_fire_talisman", "name": "Blazing Flame Talisman (Katashiro)", "category": "weapons", "scale": 1.0, "color": "#ff5020"},
    {"id": "kuon_ice_talisman", "name": "Frozen Spear Talisman", "category": "weapons", "scale": 1.0, "color": "#40a0ff"},
    {"id": "kuon_summon_scroll", "name": "Summoning Beast Scroll", "category": "weapons", "scale": 1.0, "color": "#a040b0"},
    # Items
    {"id": "kuon_herbal_salve", "name": "Traditional Kyoto Herbal Salve", "category": "items", "scale": 1.0, "color": "#508040"},
    {"id": "kuon_sacred_incense", "name": "Exorcism White Incense", "category": "items", "scale": 1.0, "color": "#e8e0d0"},
    {"id": "kuon_cherry_key", "name": "Cherry Blossom Inlaid Box Key", "category": "items", "scale": 1.0, "color": "#d4af37"},
    {"id": "kuon_silkworm_thread", "name": "Cursed Red Silkworm Spool", "category": "items", "scale": 1.0, "color": "#c02030"},
    {"id": "kuon_spirit_paper", "name": "Blank Katashiro Paper Doll", "category": "items", "scale": 1.0, "color": "#ffffff"},
    {"id": "kuon_purification_salt", "name": "Coarse Shinto Purification Salt", "category": "items", "scale": 1.0, "color": "#f8f8f8"},
    # Characters
    {"id": "kuon_utsuki", "name": "Utsuki (Yin Phase Priestess)", "category": "characters", "scale": 1.0, "color": "#f0d0a0"},
    {"id": "kuon_sakuya", "name": "Sakuya (Yang Phase Exorcist)", "category": "characters", "scale": 1.0, "color": "#e0c8b0"},
    # Enemies / Bosses
    {"id": "kuon_gaki", "name": "Twin Gaki (Twisted Hungry Demon)", "category": "enemies", "boss": False, "kind_id": 180, "scale": 0.85, "color": "#802010"},
    {"id": "kuon_blood_corpse", "name": "Blood Corpse (Silkworm Cocoon Horror)", "category": "enemies", "boss": True, "kind_id": 181, "scale": 1.15, "color": "#901030"},
    {"id": "kuon_doman", "name": "Ashiya Doman (The Evil Diviner)", "category": "enemies", "boss": True, "kind_id": 182, "scale": 1.1, "color": "#202020"},
    # UI
    {"id": "kuon_ui_yin_yang", "name": "Yin-Yang Meditation Mandala UI", "category": "ui", "scale": 1.0, "color": "#c0a060"},
    {"id": "kuon_ui_spell_wheel", "name": "Five Phases Talisman Wheel UI", "category": "ui", "scale": 1.0, "color": "#803020"},
    # Sounds
    {"id": "kuon_snd_kagura_bell", "name": "Kagura Bell Ringing Chime", "category": "sounds", "sound_type": "chime", "duration": 1.5, "freq": 750.0},
    {"id": "kuon_snd_gaki_skitter", "name": "Demon Gaki Skittering Screech", "category": "sounds", "sound_type": "roar", "duration": 1.0},
    {"id": "kuon_snd_silkworm_hiss", "name": "Silkworm Cocoon Blood Hiss", "category": "sounds", "sound_type": "spray", "duration": 1.2},
    {"id": "kuon_snd_talisman_burn", "name": "Blazing Katashiro Paper Ignite SFX", "category": "sounds", "sound_type": "gunshot", "duration": 0.7},
]

SIREN_CATALOG = [
    # Scenery
    {"id": "siren_mana_cross", "name": "Mana Cross of Hanuda", "category": "scenery", "scale": 1.0, "color": "#c02030"},
    {"id": "siren_hanuda_shrine", "name": "Hanuda Forest Mountain Shrine", "category": "scenery", "scale": 1.0, "color": "#504030"},
    {"id": "siren_red_water_bucket", "name": "Hanuda Red Water Bucket", "category": "scenery", "scale": 1.0, "color": "#801010"},
    {"id": "siren_broken_tv", "name": "Static CRT Television (Sightjack)", "category": "scenery", "scale": 1.0, "color": "#304040"},
    {"id": "siren_village_sign", "name": "Weathered Village Road Sign", "category": "scenery", "scale": 1.0, "color": "#405040"},
    {"id": "siren_abandoned_bus", "name": "Rusted Mountain Patrol Bus", "category": "scenery", "scale": 1.0, "color": "#506070"},
    # Weapons
    {"id": "siren_sniper_rifle", "name": "Type-99 Arisaka Rifle (Shibito Sniper)", "category": "weapons", "scale": 1.0, "color": "#3a3028"},
    {"id": "siren_fire_axe", "name": "Emergency Fire Axe", "category": "weapons", "scale": 1.0, "color": "#b02020"},
    {"id": "siren_farmer_sickle", "name": "Harvesting Sickle (Kama)", "category": "weapons", "scale": 1.0, "color": "#707070"},
    {"id": "siren_revolver", "name": "Police Officer Nambu Revolver", "category": "weapons", "scale": 1.0, "color": "#282828"},
    {"id": "siren_crowbar", "name": "Heavy Iron Crowbar", "category": "weapons", "scale": 1.0, "color": "#484850"},
    # Items
    {"id": "siren_first_aid_packet", "name": "Hanuda Red Cross First Aid Packet", "category": "items", "scale": 1.0, "color": "#ffffff"},
    {"id": "siren_shrine_amulet", "name": "Mana Shinto Protection Amulet", "category": "items", "scale": 1.0, "color": "#c02030"},
    {"id": "siren_hanuda_gate_key", "name": "Hanuda Forest Iron Gate Key", "category": "items", "scale": 1.0, "color": "#505058"},
    {"id": "siren_flashlight_battery", "name": "Spare Heavy Flashlight Battery", "category": "items", "scale": 1.0, "color": "#202080"},
    {"id": "siren_sightjack_tuner", "name": "Portable Radio Frequency Tuner", "category": "items", "scale": 1.0, "color": "#304040"},
    {"id": "siren_flare", "name": "Emergency Red Road Flare", "category": "items", "scale": 1.0, "color": "#e02020"},
    # Characters
    {"id": "siren_kyoya", "name": "Kyoya Suda", "category": "characters", "scale": 1.0, "color": "#405060"},
    {"id": "siren_miyako", "name": "Miyako Kajiro", "category": "characters", "scale": 1.0, "color": "#e0b0b0"},
    {"id": "siren_mamoru", "name": "Mamoru Itsuki", "category": "characters", "scale": 1.0, "color": "#506050"},
    # Enemies / Bosses
    {"id": "siren_shibito_sniper", "name": "Sniper Shibito (High Vigilance)", "category": "enemies", "boss": False, "kind_id": 190, "scale": 1.0, "color": "#604040"},
    {"id": "siren_shibito_sickle", "name": "Sickle Shibito (Frenzied Pursuit)", "category": "enemies", "boss": False, "kind_id": 191, "scale": 1.0, "color": "#703030"},
    {"id": "siren_yamibito", "name": "Yamibito (Dark Shadow Stalker)", "category": "enemies", "boss": True, "kind_id": 192, "scale": 1.1, "color": "#181820"},
    {"id": "siren_datatsushi", "name": "Datatsushi (Ancient God of Hanuda)", "category": "enemies", "boss": True, "kind_id": 193, "scale": 1.4, "color": "#800810"},
    # UI
    {"id": "siren_ui_sightjack", "name": "Sightjack TV Static Crosshair UI", "category": "ui", "scale": 1.0, "color": "#408080"},
    {"id": "siren_ui_mission_objectives", "name": "Hanuda Archive Mission Objectives UI", "category": "ui", "scale": 1.0, "color": "#803030"},
    {"id": "siren_ui_link_navigator", "name": "Chronological Link Navigator Grid UI", "category": "ui", "scale": 1.0, "color": "#304050"},
    # Sounds
    {"id": "siren_snd_shibito_cry", "name": "Shibito Sightjack Alert Wail", "category": "sounds", "sound_type": "cry", "duration": 1.4, "freq": 480.0},
    {"id": "siren_snd_rifle_fire", "name": "Arisaka Type-99 Sniper Shot", "category": "sounds", "sound_type": "gunshot", "duration": 0.9},
    {"id": "siren_snd_village_siren", "name": "Hanuda Village Red Water Siren", "category": "sounds", "sound_type": "cry", "duration": 2.0, "freq": 350.0},
    {"id": "siren_snd_tuning_static", "name": "Sightjack TV Static Dial Scan SFX", "category": "sounds", "sound_type": "static", "duration": 1.5},
    {"id": "siren_snd_blood_water", "name": "Hanuda Red Water Rippling Churn", "category": "sounds", "sound_type": "spray", "duration": 1.3},
]

ALL_CATALOGS = {
    "re4": lambda: re4.catalog() + RE4_EXTRA_CATALOG,
    "haunting_ground": lambda: HG_CATALOG,
    "clock_tower_3": lambda: CT3_CATALOG,
    "silent_hill": lambda: SH_CATALOG,
    "fatal_frame": lambda: FF_CATALOG,
    "kuon": lambda: KUON_CATALOG,
    "forbidden_siren": lambda: SIREN_CATALOG,
}


# -----------------------------------------------------------------------------
# Public Asset Access API
# -----------------------------------------------------------------------------

def get_catalog(game_key=None, category=None):
    """Retrieves assets filtered by game and/or category."""
    items = []
    if not game_key or game_key == "all":
        for g in GAMES:
            key = g["key"]
            cat_fn = ALL_CATALOGS.get(key)
            if cat_fn:
                for it in cat_fn():
                    if category is None or category == "all" or it.get("category") == category:
                        items.append({**it, "game": key, "game_name": g["name"]})
        return items

    cat_fn = ALL_CATALOGS.get(game_key)
    if not cat_fn:
        return []
    g_info = next((g for g in GAMES if g["key"] == game_key), None)
    g_name = g_info["name"] if g_info else game_key
    for it in cat_fn():
        if category is None or category == "all" or it.get("category") == category:
            items.append({**it, "game": game_key, "game_name": g_name})
    return items


def get_item(game_key, item_id):
    items = get_catalog(game_key)
    for it in items:
        if it["id"] == item_id:
            return it
    return None


def get_model_json(game_key, item_id):
    """Returns Three.js JSON representation for Studio viewport rendering."""
    if game_key == "re4":
        try:
            asset = re4.get_asset(item_id)
            if asset.get("bin_bytes"):
                return re4.bin_to_model_json(asset["bin_bytes"], asset_id=item_id)
        except Exception:
            pass
    elif game_key == "silent_hill":
        try:
            res = sh2.get_model_json(item_id)
            if res:
                return res
        except Exception:
            pass
    elif game_key == "haunting_ground":
        try:
            res = hg.get_model_json(item_id)
            if res:
                return res
        except Exception:
            pass

    item = get_item(game_key, item_id) or {"id": item_id, "name": item_id, "category": "scenery"}
    vtx, tris = build_stylized_mesh(item)

    flat_p = [coord for v in vtx for coord in v["pos"]]
    flat_uv = [uv for v in vtx for uv in v["uv"]]
    flat_c = [1.0, 1.0, 1.0, 1.0] * len(vtx)
    flat_idx = [idx for t in tris for idx in t]

    bones = [[-1, [1.0, 1.0, 1.0], [0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]]
    meshes = [{
        "mesh": 0,
        "bone": 0,
        "additive": False,
        "c": base64.b64encode(struct.pack(f"<{len(flat_c)}f", *flat_c)).decode(),
        "p": base64.b64encode(struct.pack(f"<{len(flat_p)}f", *flat_p)).decode(),
        "uv": base64.b64encode(struct.pack(f"<{len(flat_uv)}f", *flat_uv)).decode(),
        "groups": [{"m": 0, "i": base64.b64encode(struct.pack(f"<{len(flat_idx)}I", *flat_idx)).decode()}],
    }]

    return {
        "bones": bones,
        "meshes": meshes,
        "tids": [f"{game_key}:{item_id}:0"],
        "objects": 1,
    }


def get_texture_png(game_key, item_id):
    """Returns PNG bytes for Studio texture rendering."""
    if game_key == "re4":
        try:
            asset = re4.get_asset(item_id)
            if asset.get("tpl_bytes"):
                return re4.tpl_to_png(asset["tpl_bytes"])
        except Exception:
            pass
    elif game_key == "silent_hill":
        try:
            res = sh2.get_texture_png(item_id)
            if res:
                return res
        except Exception:
            pass
    elif game_key == "haunting_ground":
        try:
            res = hg.get_texture_png(item_id)
            if res:
                return res
        except Exception:
            pass

    item = get_item(game_key, item_id) or {"id": item_id}
    img = make_stylized_texture(item)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def get_nbd_mesh_node(game_key, item_id, material=0, template=None):
    """Returns Outbreak Capcom NBD kind 4 mesh node for game engine injection."""
    if game_key == "re4":
        try:
            asset = re4.get_asset(item_id)
            if asset.get("bin_bytes"):
                return re4.bin_to_nbd_mesh_node(asset["bin_bytes"], material=material, template=template)
        except Exception:
            pass
    elif game_key == "silent_hill":
        try:
            res = sh2.get_nbd_mesh_node(item_id, material=material, template=template)
            if res:
                return res
        except Exception:
            pass
    elif game_key == "haunting_ground":
        try:
            res = hg.get_nbd_mesh_node(item_id, material=material, template=template)
            if res:
                return res
        except Exception:
            pass

    item = get_item(game_key, item_id) or {"id": item_id}
    vtx, tris = build_stylized_mesh(item)
    all_pos = [v["pos"] for v in vtx]
    all_uv = [v["uv"] for v in vtx]

    if template is None:
        p11 = struct.pack("<III", 11 << 16, 1, 28) + struct.pack("<4f", 128.0, 128.0, 128.0, 255.0)
        p15 = struct.pack("<III", 15 << 16, 1, 84) + bytes(72)
        template = struct.pack("<III", 4, 2, 12 + len(p11) + len(p15)) + p11 + p15

    return nbd.make_mesh_node(template, all_pos, all_uv, tris, material)


def get_sld_texture(game_key, item_id):
    """Returns Outbreak SLD-compressed TIM2 texture stream for game engine injection."""
    if game_key == "re4":
        try:
            asset = re4.get_asset(item_id)
            if asset.get("tpl_bytes"):
                return re4.tpl_to_sld(asset["tpl_bytes"])
        except Exception:
            pass
    elif game_key == "silent_hill":
        try:
            res = sh2.get_sld_texture(item_id)
            if res:
                return res
        except Exception:
            pass
    elif game_key == "haunting_ground":
        try:
            res = hg.get_sld_texture(item_id)
            if res:
                return res
        except Exception:
            pass

    item = get_item(game_key, item_id) or {"id": item_id}
    img = make_stylized_texture(item)
    w, h = img.size
    q = img.convert("P", palette=Image.Palette.ADAPTIVE, colors=256)
    pal_rgb = q.getpalette()[:768]
    raw_pixels = bytes(q.getdata())

    # Build 256-color RGBA CLUT (CSM1 swizzled for PS2)
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


def get_sound_wav(game_key, item_id):
    """Returns 16-bit PCM WAV audio bytes for an asset."""
    item = get_item(game_key, item_id) or {"id": item_id, "sound_type": "sound"}
    stype = item.get("sound_type", "sound")
    dur = item.get("duration", 1.0)
    freq = item.get("freq", 440.0)
    return generate_synth_sound(stype, duration=dur, freq=freq)


# -----------------------------------------------------------------------------
# Self-Test
# -----------------------------------------------------------------------------

def main():
    print("Testing Comprehensive PS2 Horror Games Asset Pipeline...")
    total_assets = 0
    categories = ("scenery", "characters", "enemies", "weapons", "items", "ui", "sounds")

    for g in GAMES:
        key = g["key"]
        cat = get_catalog(key)
        total_assets += len(cat)
        counts = {c: sum(1 for it in cat if it.get("category") == c) for c in categories}
        print(f"[{g['name']}] Total: {len(cat)} assets: {counts}")

    print(f"\nTotal cataloged assets across all franchises: {total_assets}")
    sample_wav = get_sound_wav("silent_hill", "sh2_snd_siren")
    print(f"Sample WAV generated: {len(sample_wav)} bytes")


if __name__ == "__main__":
    main()
