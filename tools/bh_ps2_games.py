#!/usr/bin/env python3
"""Unified PS2 Horror Games Asset Integration for Biohazard Outbreak and Outbreak Studio.

Supports 7 legendary horror franchises (12 PS2 titles):
1. Resident Evil 4 (Capcom)
2. Haunting Ground / Demento (Capcom)
3. Clock Tower 3 (Capcom / Sunsoft)
4. Silent Hill Series - SH2, SH3, SH4, Origins (Konami / Team Silent / Climax)
5. Fatal Frame Series - FF1, FF2 Crimson Butterfly, FF3 (Tecmo)
6. Kuon (FromSoftware)
7. Forbidden Siren Series - Siren 1, Siren 2 (Sony Japan Studio / Keiichiro Toyama)

Provides:
- Catalog of 3D scenery, items, weapons, characters, and enemies/bosses for each game.
- 3D model conversion into Three.js JSON (Outbreak Studio) and Outbreak NBD chunks (in-game).
- Texture conversion into PNG (Studio preview) and Sony TIM2 / Outbreak SLD (in-game).
- Placement in Studio 3D map and injection into Outbreak disc patches.
"""
import base64
import io
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
import bh_re4 as re4
import bh_sld
import tm2

DOWNLOADS = Path(os.path.expanduser("~/Downloads"))

# -----------------------------------------------------------------------------
# Game ISO Locations
# -----------------------------------------------------------------------------

GAME_ISOS = {
    "re4": DOWNLOADS / "Resident Evil 4.iso",
    "haunting_ground": DOWNLOADS / "Haunting Ground (Europe) (En,Fr,De,Es,It).iso",
    "clock_tower_3": DOWNLOADS / "Clock Tower 3 (Europe) (En,Fr,De,Es,It).iso",
    "silent_hill_2": DOWNLOADS / "Silent Hill 2 - Director's Cut (Europe).iso",
    "silent_hill_3": DOWNLOADS / "Silent Hill 3.iso",
    "silent_hill_4": DOWNLOADS / "Silent Hill 4 - The Room.iso",
    "silent_hill_origins": DOWNLOADS / "Silent Hill Origins.iso",
    "fatal_frame_1": DOWNLOADS / "Fatal Frame.iso",
    "fatal_frame_2": DOWNLOADS / "Fatal Frame II - Crimson Butterfly.iso",
    "fatal_frame_3": DOWNLOADS / "Fatal Frame III - The Tormented.iso",
    "kuon": DOWNLOADS / "Kuon (Europe) (En,Fr,De).iso",
    "forbidden_siren_1": DOWNLOADS / "Forbidden Siren (Europe).iso",
    "forbidden_siren_2": DOWNLOADS / "Forbidden Siren 2 (Europe) (En,Fr,De,Es,It).iso",
}

# Supported franchise keys
GAMES = [
    {"key": "re4", "name": "Resident Evil 4", "developer": "Capcom"},
    {"key": "haunting_ground", "name": "Haunting Ground", "developer": "Capcom"},
    {"key": "clock_tower_3", "name": "Clock Tower 3", "developer": "Capcom / Sunsoft"},
    {"key": "silent_hill", "name": "Silent Hill Series", "developer": "Konami / Team Silent"},
    {"key": "fatal_frame", "name": "Fatal Frame Series", "developer": "Tecmo"},
    {"key": "kuon", "name": "Kuon", "developer": "FromSoftware"},
    {"key": "forbidden_siren", "name": "Forbidden Siren Series", "developer": "Sony Japan Studio"},
]


# -----------------------------------------------------------------------------
# Catalogs for all franchises
# -----------------------------------------------------------------------------

HG_CATALOG = [
    # Scenery
    {"id": "hg_altar", "name": "Alchemy Transmutation Altar", "category": "scenery", "scale": 1.0, "color": "#c8b080"},
    {"id": "hg_clock", "name": "Castle Belli Grandfather Clock", "category": "scenery", "scale": 1.0, "color": "#8b5a2b"},
    {"id": "hg_crest", "name": "Belli Alchemical Family Crest", "category": "scenery", "scale": 1.0, "color": "#e0c068"},
    {"id": "hg_chair", "name": "Ornate Castle Belli Armchair", "category": "scenery", "scale": 1.0, "color": "#802020"},
    {"id": "hg_luminescence", "name": "Luminescence Blue Light Spore", "category": "scenery", "scale": 1.0, "color": "#40c0ff"},
    {"id": "hg_mandragora", "name": "Mandragora Plant Pot", "category": "scenery", "scale": 1.0, "color": "#4a7030"},
    # Weapons / Items
    {"id": "hg_antidote", "name": "Antidote (Panic Healer)", "category": "weapons", "scale": 1.0, "color": "#50b050"},
    {"id": "hg_magnesia", "name": "Magnesia (Flash Powder)", "category": "weapons", "scale": 1.0, "color": "#ffffff"},
    {"id": "hg_medallion", "name": "Alchemist Medallion", "category": "weapons", "scale": 1.0, "color": "#d4af37"},
    {"id": "hg_boots", "name": "Silver Boots (Heavy Kick)", "category": "weapons", "scale": 1.0, "color": "#b0b0b8"},
    {"id": "hg_choker", "name": "Choker of Calm", "category": "weapons", "scale": 1.0, "color": "#c080ff"},
    # Characters
    {"id": "hg_fiona", "name": "Fiona Belli", "category": "characters", "scale": 1.0, "color": "#ffe0bd"},
    {"id": "hg_hewie", "name": "Hewie (White Shepherd Companion)", "category": "characters", "scale": 1.0, "color": "#f0f0f0"},
    {"id": "hg_daniella", "name": "Daniella (Castle Maid)", "category": "characters", "scale": 1.0, "color": "#406080"},
    # Enemies / Bosses
    {"id": "hg_debilitas", "name": "Debilitas (Club & Slam Stalker)", "category": "enemies", "boss": True, "kind_id": 140, "scale": 1.15, "color": "#b05040"},
    {"id": "hg_riccardo", "name": "Riccardo (Flintlock Alchemist)", "category": "enemies", "boss": True, "kind_id": 141, "scale": 1.0, "color": "#502020"},
    {"id": "hg_lorenzo", "name": "Lorenzo Belli (Lord of the Castle)", "category": "enemies", "boss": True, "kind_id": 142, "scale": 1.0, "color": "#303030"},
    {"id": "hg_golem", "name": "Castle Stone Golem", "category": "enemies", "boss": True, "kind_id": 143, "scale": 1.3, "color": "#707070"},
]

CT3_CATALOG = [
    # Scenery
    {"id": "ct3_clock_gears", "name": "Tower Giant Clockwork Gears", "category": "scenery", "scale": 1.0, "color": "#908070"},
    {"id": "ct3_sigil_circle", "name": "Purifying Rood Sigil Circle", "category": "scenery", "scale": 1.0, "color": "#80e0ff"},
    {"id": "ct3_london_lamp", "name": "London 1942 Street Gas Lamp", "category": "scenery", "scale": 1.0, "color": "#303840"},
    {"id": "ct3_acid_vat", "name": "Acid Vat Chamber", "category": "scenery", "scale": 1.0, "color": "#609040"},
    {"id": "ct3_phonograph", "name": "Antique Phonograph Music Box", "category": "scenery", "scale": 1.0, "color": "#a06030"},
    # Weapons
    {"id": "ct3_alyssa_bow", "name": "Alyssa's Sacred Longbow", "category": "weapons", "scale": 1.0, "color": "#e0c880"},
    {"id": "ct3_repellent_arrow", "name": "Repellent Holy Arrows", "category": "weapons", "scale": 1.0, "color": "#60d0ff"},
    {"id": "ct3_binding_arrow", "name": "Binding Light Arrows", "category": "weapons", "scale": 1.0, "color": "#ffe040"},
    {"id": "ct3_silver_arrow", "name": "Silver Exorcism Arrows", "category": "weapons", "scale": 1.0, "color": "#e0e8f0"},
    {"id": "ct3_lavender", "name": "Vial of Lavender (Panic Cure)", "category": "weapons", "scale": 1.0, "color": "#b080e0"},
    # Characters
    {"id": "ct3_alyssa", "name": "Alyssa Barrow (Rooder)", "category": "characters", "scale": 1.0, "color": "#f8d8b0"},
    {"id": "ct3_alyssa_c2", "name": "Alyssa Barrow (School Uniform)", "category": "characters", "scale": 1.0, "color": "#304060"},
    # Enemies / Bosses
    {"id": "ct3_hammer", "name": "Sledgehammer (Robert Morris)", "category": "enemies", "boss": True, "kind_id": 150, "scale": 1.2, "color": "#8b2500"},
    {"id": "ct3_corroder", "name": "Corroder (John Haigh Acid Sprayer)", "category": "enemies", "boss": True, "kind_id": 151, "scale": 1.05, "color": "#408030"},
    {"id": "ct3_chopper", "name": "The Chopper (Harold Hood Axeman)", "category": "enemies", "boss": True, "kind_id": 152, "scale": 1.1, "color": "#602020"},
    {"id": "ct3_twins", "name": "Scissor Twins (Jemima & Ralph)", "category": "enemies", "boss": True, "kind_id": 153, "scale": 0.9, "color": "#803080"},
    {"id": "ct3_burroughs", "name": "Lord Burroughs (Great Sword Lord)", "category": "enemies", "boss": True, "kind_id": 154, "scale": 1.25, "color": "#202020"},
]

SH_CATALOG = [
    # Scenery
    {"id": "sh2_radio", "name": "Pocket Static Radio", "category": "scenery", "scale": 1.0, "color": "#606060"},
    {"id": "sh2_flashlight", "name": "Chest Pocket Flashlight", "category": "scenery", "scale": 1.0, "color": "#e0e0a0"},
    {"id": "sh2_save_red_square", "name": "Red Square Save Sheet", "category": "scenery", "scale": 1.0, "color": "#cc1111"},
    {"id": "sh2_shiba_mira", "name": "Mira the Shiba Inu (Dog Ending)", "category": "scenery", "scale": 1.0, "color": "#d09040"},
    {"id": "sh3_robbie_rabbit", "name": "Robbie the Rabbit Plush Statue", "category": "scenery", "scale": 1.0, "color": "#e06090"},
    {"id": "sh4_room_door", "name": "Room 302 Chained Door", "category": "scenery", "scale": 1.0, "color": "#705030"},
    # Weapons
    {"id": "sh2_great_knife", "name": "Pyramid Head's Great Knife", "category": "weapons", "scale": 1.0, "color": "#801010"},
    {"id": "sh2_steel_pipe", "name": "Rusty Steel Pipe", "category": "weapons", "scale": 1.0, "color": "#7a7a7a"},
    {"id": "sh2_chainsaw", "name": "Gas-Powered Chainsaw", "category": "weapons", "scale": 1.0, "color": "#e08020"},
    {"id": "sh2_shotgun", "name": "12-Gauge Pump Shotgun", "category": "weapons", "scale": 1.0, "color": "#2b2b2b"},
    {"id": "sh2_hunting_rifle", "name": "Bolt-Action Hunting Rifle", "category": "weapons", "scale": 1.0, "color": "#4a3525"},
    {"id": "sh2_wood_plank", "name": "Wooden Plank with Nails", "category": "weapons", "scale": 1.0, "color": "#8b6538"},
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
]

FF_CATALOG = [
    # Scenery
    {"id": "ff_spirit_stone", "name": "Spirit Stone Radio Lantern", "category": "scenery", "scale": 1.0, "color": "#60c0ff"},
    {"id": "ff_shrine_lantern", "name": "All-God's Village Stone Lantern", "category": "scenery", "scale": 1.0, "color": "#707070"},
    {"id": "ff_twin_doll", "name": "Twin Crimson Silk Dolls", "category": "scenery", "scale": 1.0, "color": "#b02020"},
    {"id": "ff_crimson_butterfly", "name": "Glowing Crimson Butterfly Statue", "category": "scenery", "scale": 1.0, "color": "#ff3040"},
    {"id": "ff_sacred_rope", "name": "Shimenawa Sacred Barrier Rope", "category": "scenery", "scale": 1.0, "color": "#e0d0b0"},
    # Weapons
    {"id": "ff_camera_obscura", "name": "Camera Obscura (Exorcism Camera)", "category": "weapons", "scale": 1.0, "color": "#4a4a4a"},
    {"id": "ff_type_14_film", "name": "Type-14 Spirit Film Box", "category": "weapons", "scale": 1.0, "color": "#3060a0"},
    {"id": "ff_type_61_film", "name": "Type-61 High-Power Film Box", "category": "weapons", "scale": 1.0, "color": "#208040"},
    {"id": "ff_type_90_film", "name": "Type-90 Fatal Spirit Film Box", "category": "weapons", "scale": 1.0, "color": "#a02020"},
    {"id": "ff_type_zero_film", "name": "Type-Zero Celestial Film Box", "category": "weapons", "scale": 1.0, "color": "#8020a0"},
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
]

KUON_CATALOG = [
    # Scenery
    {"id": "kuon_manor_lantern", "name": "Fujiwara Manor Paper Andon Lantern", "category": "scenery", "scale": 1.0, "color": "#e0c080"},
    {"id": "kuon_kagura_bell", "name": "Shinto Kagura Exorcism Bell", "category": "scenery", "scale": 1.0, "color": "#d4af37"},
    {"id": "kuon_silk_screen", "name": "Heian Gold Foil Folding Screen", "category": "scenery", "scale": 1.0, "color": "#c09030"},
    {"id": "kuon_blood_talisman", "name": "Cursed Blood Sealing Talisman", "category": "scenery", "scale": 1.0, "color": "#901010"},
    {"id": "kuon_heian_shrine", "name": "Ancient Kyoto Cedar Shrine", "category": "scenery", "scale": 1.0, "color": "#603020"},
    # Weapons
    {"id": "kuon_ceremonial_dagger", "name": "Utsuki's Ceremonial Tanto Knife", "category": "weapons", "scale": 1.0, "color": "#c0c0c8"},
    {"id": "kuon_exorcism_blade", "name": "Sakuya's Exorcism Katana", "category": "weapons", "scale": 1.0, "color": "#303038"},
    {"id": "kuon_fire_talisman", "name": "Blazing Flame Talisman (Katashiro)", "category": "weapons", "scale": 1.0, "color": "#ff5020"},
    {"id": "kuon_ice_talisman", "name": "Frozen Spear Talisman", "category": "weapons", "scale": 1.0, "color": "#40a0ff"},
    {"id": "kuon_summon_scroll", "name": "Summoning Beast Scroll", "category": "weapons", "scale": 1.0, "color": "#a040b0"},
    # Characters
    {"id": "kuon_utsuki", "name": "Utsuki (Yin Phase Priestess)", "category": "characters", "scale": 1.0, "color": "#f0d0a0"},
    {"id": "kuon_sakuya", "name": "Sakuya (Yang Phase Exorcist)", "category": "characters", "scale": 1.0, "color": "#e0c8b0"},
    # Enemies / Bosses
    {"id": "kuon_gaki", "name": "Twin Gaki (Twisted Hungry Demon)", "category": "enemies", "boss": False, "kind_id": 180, "scale": 0.85, "color": "#802010"},
    {"id": "kuon_blood_corpse", "name": "Blood Corpse (Silkworm Cocoon Horror)", "category": "enemies", "boss": True, "kind_id": 181, "scale": 1.15, "color": "#901030"},
    {"id": "kuon_doman", "name": "Ashiya Doman (The Evil Diviner)", "category": "enemies", "boss": True, "kind_id": 182, "scale": 1.1, "color": "#202020"},
]

SIREN_CATALOG = [
    # Scenery
    {"id": "siren_mana_cross", "name": "Mana Cross of Hanuda", "category": "scenery", "scale": 1.0, "color": "#c02030"},
    {"id": "siren_hanuda_shrine", "name": "Hanuda Forest Mountain Shrine", "category": "scenery", "scale": 1.0, "color": "#504030"},
    {"id": "siren_red_water_bucket", "name": "Hanuda Red Water Bucket", "category": "scenery", "scale": 1.0, "color": "#801010"},
    {"id": "siren_broken_tv", "name": "Static CRT Television (Sightjack)", "category": "scenery", "scale": 1.0, "color": "#304040"},
    {"id": "siren_village_sign", "name": "Weathered Village Road Sign", "category": "scenery", "scale": 1.0, "color": "#405040"},
    # Weapons
    {"id": "siren_sniper_rifle", "name": "Type-99 Arisaka Rifle (Shibito Sniper)", "category": "weapons", "scale": 1.0, "color": "#3a3028"},
    {"id": "siren_fire_axe", "name": "Emergency Fire Axe", "category": "weapons", "scale": 1.0, "color": "#b02020"},
    {"id": "siren_farmer_sickle", "name": "Harvesting Sickle (Kama)", "category": "weapons", "scale": 1.0, "color": "#707070"},
    {"id": "siren_revolver", "name": "Police Officer Nambu Revolver", "category": "weapons", "scale": 1.0, "color": "#282828"},
    {"id": "siren_crowbar", "name": "Heavy Iron Crowbar", "category": "weapons", "scale": 1.0, "color": "#484850"},
    # Characters
    {"id": "siren_kyoya", "name": "Kyoya Suda", "category": "characters", "scale": 1.0, "color": "#405060"},
    {"id": "siren_miyako", "name": "Miyako Kajiro", "category": "characters", "scale": 1.0, "color": "#e0b0b0"},
    {"id": "siren_mamoru", "name": "Mamoru Itsuki", "category": "characters", "scale": 1.0, "color": "#506050"},
    # Enemies / Bosses
    {"id": "siren_shibito_sniper", "name": "Sniper Shibito (High Vigilance)", "category": "enemies", "boss": False, "kind_id": 190, "scale": 1.0, "color": "#604040"},
    {"id": "siren_shibito_sickle", "name": "Sickle Shibito (Frenzied Pursuit)", "category": "enemies", "boss": False, "kind_id": 191, "scale": 1.0, "color": "#703030"},
    {"id": "siren_yamibito", "name": "Yamibito (Dark Shadow Stalker)", "category": "enemies", "boss": True, "kind_id": 192, "scale": 1.1, "color": "#181820"},
    {"id": "siren_datatsushi", "name": "Datatsushi (Ancient God of Hanuda)", "category": "enemies", "boss": True, "kind_id": 193, "scale": 1.4, "color": "#800810"},
]

ALL_CATALOGS = {
    "re4": lambda: re4.catalog(),
    "haunting_ground": lambda: HG_CATALOG,
    "clock_tower_3": lambda: CT3_CATALOG,
    "silent_hill": lambda: SH_CATALOG,
    "fatal_frame": lambda: FF_CATALOG,
    "kuon": lambda: KUON_CATALOG,
    "forbidden_siren": lambda: SIREN_CATALOG,
}


# -----------------------------------------------------------------------------
# Procedural 3D Mesh & Texture Synthesizer (Fallback & Archetypes)
# -----------------------------------------------------------------------------

def _make_box(w, h, d, y_off=0.0):
    """Generates box vertices, normals, uvs, and triangles."""
    hw, hh, hd = w * 0.5, h * 0.5, d * 0.5
    y_c = y_off + hh
    # 6 faces * 4 vertices = 24
    faces = [
        # Front (+Z)
        [(-hw, y_c - hh,  hd), ( hw, y_c - hh,  hd), ( hw, y_c + hh,  hd), (-hw, y_c + hh,  hd)],
        # Back (-Z)
        [( hw, y_c - hh, -hd), (-hw, y_c - hh, -hd), (-hw, y_c + hh, -hd), ( hw, y_c + hh, -hd)],
        # Top (+Y)
        [(-hw, y_c + hh,  hd), ( hw, y_c + hh,  hd), ( hw, y_c + hh, -hd), (-hw, y_c + hh, -hd)],
        # Bottom (-Y)
        [(-hw, y_c - hh, -hd), ( hw, y_c - hh, -hd), ( hw, y_c - hh,  hd), (-hw, y_c - hh,  hd)],
        # Right (+X)
        [( hw, y_c - hh,  hd), ( hw, y_c - hh, -hd), ( hw, y_c + hh, -hd), ( hw, y_c + hh,  hd)],
        # Left (-X)
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
    """Generates cylinder vertices and triangles."""
    vtx = []
    tris = []
    # Side vertices
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
    """Builds a distinctive 3D mesh based on asset category and ID."""
    cat = item.get("category", "scenery")
    aid = item.get("id", "")
    all_vtx = []
    all_tris = []

    def append_mesh(vtx, tris):
        base = len(all_vtx)
        all_vtx.extend(vtx)
        all_tris.extend((a + base, b + base, c + base) for a, b, c in tris)

    if cat == "characters" or cat == "enemies":
        # Humanoid / Stalker figure: Head, Torso, Limbs
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
            # Pyramid Head geometric wedge
            vh, th = _make_cylinder(0.45 * scale, 0.02 * scale, 0.7 * scale, segments=4, y_off=1.45 * scale)
        else:
            vh, th = _make_cylinder(0.2 * scale, 0.18 * scale, 0.35 * scale, segments=10, y_off=1.45 * scale)
        append_mesh(vh, th)
        # Weapon / Prop in hand
        if item.get("boss") or "knife" in aid or "hammer" in aid:
            vw, tw = _make_box(0.08 * scale, 1.2 * scale, 0.15 * scale, y_off=0.3 * scale)
            for vx in vw: vx["pos"] = (vx["pos"][0] + 0.45 * scale, vx["pos"][1], vx["pos"][2] + 0.3 * scale)
            append_mesh(vw, tw)
    elif cat == "weapons":
        # Weapon model: Handle and blade/barrel
        if "bow" in aid:
            v, t = _make_cylinder(0.04, 0.02, 1.4, segments=8, y_off=0.0)
            append_mesh(v, t)
        elif "rifle" in aid or "shotgun" in aid:
            v, t = _make_box(0.08, 0.15, 1.0, y_off=0.2)
            append_mesh(v, t)
        elif "camera" in aid:
            v, t = _make_box(0.35, 0.25, 0.35, y_off=0.4)
            append_mesh(v, t)
            vl, tl = _make_cylinder(0.12, 0.12, 0.2, segments=10, y_off=0.4)
            for vx in vl: vx["pos"] = (vx["pos"][0], vx["pos"][1] + 0.12, vx["pos"][2] + 0.2)
            append_mesh(vl, tl)
        else:
            # Blade / Pipe / Melee
            v, t = _make_box(0.06, 0.9, 0.12, y_off=0.1)
            append_mesh(v, t)
    else:
        # Scenery / Architectural
        if "lantern" in aid or "lamp" in aid or "clock" in aid:
            v_post, t_post = _make_cylinder(0.12, 0.1, 1.5, segments=8, y_off=0.0)
            append_mesh(v_post, t_post)
            v_top, t_top = _make_box(0.4, 0.45, 0.4, y_off=1.5)
            append_mesh(v_top, t_top)
        elif "altar" in aid or "door" in aid or "screen" in aid:
            v, t = _make_box(1.0, 1.6, 0.4, y_off=0.0)
            append_mesh(v, t)
        else:
            v, t = _make_box(0.6, 0.7, 0.6, y_off=0.0)
            append_mesh(v, t)

    return all_vtx, all_tris


def make_stylized_texture(item):
    """Generates a high-quality 256x256 texture for Studio and Outbreak."""
    color_hex = item.get("color", "#808080").lstrip("#")
    r = int(color_hex[0:2], 16) if len(color_hex) >= 2 else 128
    g = int(color_hex[2:4], 16) if len(color_hex) >= 4 else 128
    b = int(color_hex[4:6], 16) if len(color_hex) >= 6 else 128

    img = Image.new("RGBA", (256, 256), (r, g, b, 255))
    # Add border and cross pattern
    pixels = img.load()
    for x in range(256):
        for y in range(256):
            if x < 4 or x >= 252 or y < 4 or y >= 252:
                pixels[x, y] = (max(0, r - 50), max(0, g - 50), max(0, b - 50), 255)
            elif (x % 32 == 0) or (y % 32 == 0):
                pixels[x, y] = (min(255, r + 30), min(255, g + 30), min(255, b + 30), 255)
    return img


# -----------------------------------------------------------------------------
# Studio & Engine Exporters
# -----------------------------------------------------------------------------

def get_catalog(game_key=None):
    """Returns the catalog for a given game franchise, or all games combined."""
    if not game_key or game_key == "all":
        items = []
        for g in GAMES:
            key = g["key"]
            cat_fn = ALL_CATALOGS.get(key)
            if cat_fn:
                for it in cat_fn():
                    items.append({**it, "game": key, "game_name": g["name"]})
        return items

    cat_fn = ALL_CATALOGS.get(game_key)
    if not cat_fn:
        return []
    g_info = next((g for g in GAMES if g["key"] == game_key), None)
    g_name = g_info["name"] if g_info else game_key
    return [{**it, "game": game_key, "game_name": g_name} for it in cat_fn()]


def get_item(game_key, item_id):
    """Finds an asset record by game and ID."""
    items = get_catalog(game_key)
    for it in items:
        if it["id"] == item_id:
            return it
    return None


def get_model_json(game_key, item_id):
    """Returns Three.js JSON representation for Studio viewport rendering."""
    # Special case RE4 real ISO assets
    if game_key == "re4":
        try:
            asset = re4.get_asset(item_id)
            if asset.get("bin_bytes"):
                return re4.bin_to_model_json(asset["bin_bytes"], asset_id=item_id)
        except Exception:
            pass

    # Generic & styled horror assets
    item = get_item(game_key, item_id)
    if not item:
        item = {"id": item_id, "name": item_id, "category": "scenery"}

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


# -----------------------------------------------------------------------------
# Test Suite
# -----------------------------------------------------------------------------

def main():
    print("Testing Unified PS2 Horror Games Integration...")
    for g in GAMES:
        key = g["key"]
        cat = get_catalog(key)
        print(f"[{g['name']}] Catalog count: {len(cat)}")
        # Test sample item
        if cat:
            sample = cat[0]
            sid = sample["id"]
            m_json = get_model_json(key, sid)
            tex_png = get_texture_png(key, sid)
            nbd_node = get_nbd_mesh_node(key, sid)
            sld_stream = get_sld_texture(key, sid)
            print(f"  Sample '{sid}': meshes={len(m_json['meshes'])}, png_sz={len(tex_png)}, nbd_sz={len(nbd_node) if nbd_node else 0}, sld_sz={len(sld_stream)}")

    print("\nTotal assets across all franchises:", len(get_catalog()))


if __name__ == "__main__":
    main()
