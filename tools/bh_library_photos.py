#!/usr/bin/env python3
"""Photographs the Library's objects in the game itself, so their pictures are exactly what the player sees (the game's own lighting, pictures, see-through leaves).

For one room: every other object of the room's model is hidden (its bone scaled to nothing), the ones to photograph stay where they stand, and the game, started in that
room, is told by the Studio camera mailbox (src/modern_camera.cpp) to look at each in turn. The pictures are cut to squares and kept under work/studio/library/GAME/ named by the
object's shape (`triangles,x,y,z`, the Library's key), which web/js/library.js shows in place of its own drawing when there is one.

    photograph_room(studio_url, game, scenario, room_file, start_room, wanted)   -> {key: path}
"""
import base64
import json
import math
import struct
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FOLDER = ROOT / "work" / "studio" / "library"
HIDDEN = [0.0001, 0.0001, 0.0001]


def _get(url):
    with urllib.request.urlopen(url) as r:
        return json.load(r)


def _post(url, body):
    request = urllib.request.Request(url, json.dumps(body).encode(), {"Content-Type": "application/json"})
    with urllib.request.urlopen(request) as r:
        return json.load(r)


def key_of(mesh):
    """The Library's key of a mesh of the model JSON (bh_rdt_edit.model_json): its triangles and the size of its box, as web/js/library.js makes them"""
    p = struct.unpack(f"<{len(base64.b64decode(mesh['p'])) // 4}f", base64.b64decode(mesh["p"]))
    pts = [p[i:i + 3] for i in range(0, len(p), 3)]
    lo = [min(q[k] for q in pts) for k in range(3)]
    hi = [max(q[k] for q in pts) for k in range(3)]
    triangles = sum(len(base64.b64decode(g["i"])) // 12 for g in mesh["groups"])
    return ",".join(str(v) for v in (triangles, *[round(hi[k] - lo[k]) for k in range(3)])), lo, hi


def _pure_translations(bones):
    """Every bone that has children moves them only (no turn, no scale), so a change of an object's offset is a change of its place in the world"""
    parents = {b[0] for b in bones}
    for i in parents:
        if i >= 0 and (any(abs(v - 1) > 1e-6 for v in bones[i][1]) or any(abs(v) > 1e-6 for v in bones[i][2])):
            return False
    return True


def _world_offset(bones, i):
    x = y = z = 0.0
    while i >= 0:
        o = bones[i][3]
        x, y, z = x + o[0], y + o[1], z + o[2]
        i = bones[i][0]
    return x, y, z


def _turn(rot, v):
    """v turned by a bone's rotation: about x, then y, then z (the order the model viewer and the game share)"""
    x, y, z = v
    a, b, c = rot
    y, z = y * math.cos(a) - z * math.sin(a), y * math.sin(a) + z * math.cos(a)
    x, z = x * math.cos(b) + z * math.sin(b), -x * math.sin(b) + z * math.cos(b)
    x, y = x * math.cos(c) - y * math.sin(c), x * math.sin(c) + y * math.cos(c)
    return x, y, z


def lineup(model, scene_room, wanted):
    """(edits for the room's scenery, [(key, centre, size)]): the objects wanted stay where they are and every other one is hidden, except what stands at a door or exit (hiding those crashed the game)"""
    bones = model["bones"]
    if not _pure_translations(bones):
        raise ValueError("a bone with children turns or scales: world places can't be had by adding offsets")
    ways = [a["quad"] for a in scene_room["areas"] if a["tag"] in (0, 1, 2)]
    near = lambda lo, hi: any(min(p[0] for p in q) - 250 < hi[0] and max(p[0] for p in q) + 250 > lo[0] and min(p[1] for p in q) - 250 < hi[2] and max(p[1] for p in q) + 250 > lo[2] for q in ways)
    edits, shots = {}, []
    for m in model["meshes"]:
        k, lo, hi = key_of(m)
        b = bones[m["bone"]]
        above = _world_offset(bones, b[0]) if b[0] >= 0 else (0.0, 0.0, 0.0)
        if m["mesh"] in wanted:
            mid = _turn(b[2], [((lo[j] + hi[j]) / 2) * b[1][j] for j in range(3)])
            centre = [above[j] + b[3][j] + mid[j] for j in range(3)]
            shots.append((k, centre, max(hi[j] - lo[j] for j in range(3)) * max(abs(v) for v in b[1])))
        else:
            world = [above[j] + b[3][j] for j in range(3)]
            small = max((hi[j] - lo[j]) * abs(b[1][j]) for j in range(3)) < 400        # only door-sized objects stay: the room's floor and walls overlap its doors too
            if not (small and near([world[j] + lo[j] for j in range(3)], [world[j] + hi[j] for j in range(3)])):
                edits[str(m["mesh"])] = {"scale": HIDDEN, "rotation": b[2], "offset": b[3]}
    return edits, shots


def camera_for(centre, size):
    """An eye in front, to the right and above the object, looking at its middle, far enough to have all of it in a 40 degree view"""
    dist = max(70.0, size * 1.25)
    d = (0.55, 0.40, 0.73)
    n = math.sqrt(sum(v * v for v in d))
    return {"eye": [centre[j] + dist * d[j] / n for j in range(3)], "target": list(centre), "fov": 32}


def photograph_room(studio, game, scenario, room_file, start_room, wanted, batch=40):
    """Photographs the objects `wanted` ({mesh number: key}) of a room, `batch` to a game start; the pictures kept; {key: path}"""
    import bh_autoplay
    import bh_preview

    model = _get(f"{studio}/api/model?file={room_file.replace('.rdt', '.nbd')}")
    scene = _get(f"{studio}/api/scene")
    room = next(r for r in scene["rooms"] if r["file"] == room_file)
    out_dir = FOLDER / game
    out_dir.mkdir(parents=True, exist_ok=True)
    made = {}
    items = sorted(wanted.items())
    for at in range(0, len(items), batch):
        part = dict(items[at:at + batch])
        edits, shots = lineup(model, room, part)
        built = _post(f"{studio}/api/build", {"edits": {"rooms": {room_file: {"scenery": edits}}, **({"start_room": start_room} if start_room is not None else {})}, "english": True})
        preview = bh_preview.Preview(game, scenario, kind="library")
        try:
            preview.prepare(built["patch"], start_room if start_room is not None else bh_autoplay.KNOWN[(game, scenario)]["start_room"])
            for k, centre, size in shots:
                jpg = preview.photo_from(camera_for(centre, size), "lib")
                made[k] = _square(jpg, out_dir / (k.replace(",", "_") + ".jpg"))
        finally:
            preview.stop()
    return made


def _square(jpg, target):
    from PIL import Image

    image = Image.open(jpg).convert("RGB")
    image = _lift(image)
    side = min(image.size)
    left, top = (image.width - side) // 2, (image.height - side) // 2
    image.crop((left, top, left + side, top + side)).resize((256, 256), Image.LANCZOS).save(target, "JPEG", quality=90)
    return target


def _lift(image):
    """The game's rooms are dark: the picture is brightened so that its brightest parts reach about four fifths (the look of the colours is kept, the light is not)"""
    from PIL import ImageOps

    levels = sorted(image.convert("L").getdata())
    top = levels[int(len(levels) * 0.995)]
    if top < 8:
        return image
    k = min(8.0, 205.0 / top)
    return image.point(lambda v: min(255, int(v * k)))


def context_camera(centre, size, middle):
    """An eye on the room's middle side of the object, a little above it, far enough to have it and its surroundings in view (the game's own picture of it, in its place)"""
    dx, dz = middle[0] - centre[0], middle[1] - centre[1]
    n = math.hypot(dx, dz) or 1.0
    dist = max(260.0, size * 3.2)
    return {"eye": [centre[0] + dx / n * dist, centre[1] + dist * 0.45, centre[2] + dz / n * dist], "target": list(centre), "fov": 45}


def photograph_in_context(studio, game, scenario, rooms, wanted_by_room, hops_to, at_in):
    """Pictures of the objects as they stand in the game (nothing hidden): `rooms` {room file: room number} in the order to visit, `wanted_by_room` {room file: {mesh: key}},
    `hops_to(room number)` the doors' standing points to take from the room the game starts in, `at_in(room number)` where to stand there. {key: path}"""
    import bh_preview

    out_dir = FOLDER / game
    out_dir.mkdir(parents=True, exist_ok=True)
    built = _post(f"{studio}/api/build", {"edits": {"rooms": {}}, "english": True})
    preview = bh_preview.Preview(game, scenario, kind="library")
    made = {}
    try:
        import bh_autoplay

        start = bh_autoplay.KNOWN[(game, scenario)]["start_room"]
        preview.prepare(built["patch"], start)
        here = start
        for file, number in rooms.items():
            wanted = wanted_by_room.get(file)
            if not wanted:
                continue
            model = _get(f"{studio}/api/model?file={file.replace('.rdt', '.nbd')}")
            bones = model["bones"]
            xs = [b[3][0] for b in bones[1:] if abs(b[3][0]) < 1e7]
            zs = [b[3][2] for b in bones[1:] if abs(b[3][2]) < 1e7]
            middle = (sum(xs) / len(xs), sum(zs) / len(zs))
            _, shots = lineup(model, {"areas": []}, wanted)          # (the places of the objects; the hides it also works out are not used)
            hops = hops_to(here, number)
            for n, (k, centre, size) in enumerate(shots):
                jpg = preview.shot(number, hops if n == 0 else [], at_in(number), "ctx", context_camera(centre, size, middle))
                made[k] = _square(jpg, out_dir / (k.replace(",", "_") + ".jpg"))
            here = number
    finally:
        preview.stop()
    return made
