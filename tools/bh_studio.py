#!/usr/bin/env python3
"""Outbreak Studio: the level editor for File 1 and File 2. A local web page (tools/studio/web) over these tools' readers and patch builders.

    bh_studio.py [GAME [SCENARIO]] [--port N] [--no-open]

GAME is file1 or file2 (File 2 by default), SCENARIO the scenario's number (40 is File 2's first). Your work is kept in work/studio/ and the page saves it as you go;
Build writes work/studio/GAME-play.patch (the English patch, if the game has one, and your edits, through tools/bh_studio_build.py) and Play starts the game with it.
docs/studio/PLAN.md says what the editor is for and what it can do so far.
"""
import io
import json
import os
import struct
import subprocess
import sys
import threading
import time
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import bh_archive as archive
import bh_rdt as rdt
import bh_rdt_edit as legacy
import bh_rdt_map as maps
import bh_autoplay
import bh_studio_build as studio_build

ROOT = Path(__file__).resolve().parent.parent
WEB = Path(__file__).resolve().parent / "studio" / "web"
WIKI = Path.home() / "Documents" / "outbreak-research" / "Documentation" / "docs" / "game data" / "rooms.md"
TYPES = {".html": "text/html; charset=utf-8", ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".svg": "image/svg+xml",
         ".png": "image/png", ".json": "application/json", ".woff2": "font/woff2"}


def scenarios(game):
    return sorted({int(pack[1:4]) for pack, _, _ in rdt.rooms(game)})


class Studio:
    def __init__(self, game, scenario):
        self.lock = threading.Lock()
        self.game, self.scenario = game, scenario
        self.names = maps.room_names(str(WIKI)) if WIKI.exists() else {}
        self.cache = {}
        self.process = None

    # ---- what the page asks for
    def scene(self):
        with self.lock:
            key = ("scene", self.game, self.scenario)
            if key not in self.cache:
                rooms = maps.build(self.game, self.scenario, self.names, False)
                self.cache[key] = {"game": self.game, "scenario": self.scenario, "rooms": rooms, "scenarios": scenarios(self.game),
                                   "ops": {str(k): list(v) for k, v in rdt.OPS.items()}, "start_room": start_room(self.game, self.scenario)}
            return self.cache[key]

    def model(self, file, game=None, scenario=None):
        """A room's model as JSON (bh_rdt_edit.model_json), of this scenario or (for the Library) of any scenario of either game: then its pictures are
        named "game:scenario:number", which the page asks /api/tex for with that scenario"""
        game, scenario = game or self.game, int(scenario or self.scenario)
        with self.lock:
            key = ("model", game, scenario, file)
            if key not in self.cache:
                model = legacy.model_json(game, scenario, file)
                if (game, scenario) != (self.game, self.scenario) and "tids" in model:
                    model = {**model, "tids": [f"{game}:{scenario}:{n}" for n in model["tids"]]}
                self.cache[key] = model
            return self.cache[key]

    def texture(self, index, game=None, scenario=None):
        game, scenario = game or self.game, int(scenario or self.scenario)
        with self.lock:
            key = ("tex", game, scenario, index)
            if key not in self.cache:
                self.cache[key] = legacy.texture_png(game, scenario, index)
            return self.cache[key]

    def levels(self):
        """Every scenario of both games (the Library brings objects from any of them): [{"game", "scenario", "rooms": [room file of each room's first variant]}]"""
        with self.lock:
            if ("levels",) not in self.cache:
                out = []
                for game in ("file1", "file2"):
                    try:
                        rooms = rdt.rooms(game)
                    except (FileNotFoundError, KeyError):
                        continue           # (that game's disc isn't set up here)
                    for scenario in sorted({int(pack[1:4]) for pack, _, _ in rooms}):
                        files = sorted(m for pack, m, _ in rooms if int(pack[1:4]) == scenario and m.lower().endswith("00.rdt"))
                        out.append({"game": game, "scenario": scenario, "rooms": files})
                self.cache[("levels",)] = out
            return self.cache[("levels",)]

    def character(self, kind, cls):
        """The model an enemy of `kind` and class `cls` is drawn with ({"file", "model"}), or {"file": None, "reason"} when it has none (bh_characters)"""
        import bh_characters

        with self.lock:
            key = ("character", self.game, kind, cls)
            if key not in self.cache:
                file = bh_characters.model_file(self.game, kind, cls)
                self.cache[key] = ({"file": file, "model": bh_characters.character_json(self.game, file)} if file else
                                   {"file": None, "reason": f"no E{kind:02d}_{cls:02d}.NBD on the disc for this kind and class"})
            return self.cache[key]

    def character_picture(self, key):
        import bh_characters

        with self.lock:
            cache_key = ("character picture", self.game, key)
            if cache_key not in self.cache:
                self.cache[cache_key] = bh_characters.picture_png(self.game, key)
            return self.cache[cache_key]

    def messages(self):
        with self.lock:
            key = ("msg", self.game, self.scenario)
            if key not in self.cache:
                self.cache[key] = legacy.messages_json(self.game, self.scenario)
            return self.cache[key]

    # ---- the project: the edits, as tools/bh_room_patch.py reads them
    def project_path(self):
        return ROOT / "work" / "studio" / f"{self.game}-r{self.scenario:03d}.json"

    def project(self):
        path = self.project_path()
        return json.loads(path.read_text()) if path.exists() else {"edits": {"rooms": {}, "models": {}}, "meta": {}}

    def save_project(self, body):
        path = self.project_path()
        path.parent.mkdir(parents=True, exist_ok=True)
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(body, indent=1))
        tmp.replace(path)

    def switch(self, game, scenario):
        with self.lock:
            self.game, self.scenario = game, scenario

    # ---- build and play
    def english_patch(self):
        path = ROOT / "build" / f"english-{self.game}.patch"
        if not path.exists():
            subprocess.run([sys.executable, str(ROOT / "tools" / "bh_build_patch.py"), self.game, str(path)], check=False)
        return path if path.exists() else None

    def build(self, edits, english=True, name=None):
        out = ROOT / "work" / "studio" / (name or f"{self.game}-play.patch")
        out.parent.mkdir(parents=True, exist_ok=True)
        base = [str(self.english_patch())] if english and self.english_patch() else []
        started = time.time()
        models, pictures = grown_models(self.game, self.scenario, edits)
        report = studio_build.compose(self.game, scenery_as_models(self.game, self.scenario, edits), str(out), base,
                                      members={**models, **repainted(self.game, self.scenario, edits), **moved_start(self.game, self.scenario, edits)},
                                      pictures={f"r{self.scenario:03d}.afs": pictures} if pictures else None)
        return {"patch": str(out), "report": report, "seconds": round(time.time() - started, 2), "bytes": out.stat().st_size}

    def play(self, patch, hops=None, at=None):
        self.stop()
        self.status = ""
        disc = archive.disc_path(self.game)
        if hops is not None and bh_autoplay.supported(self.game, self.scenario):
            self.process = bh_autoplay.start(self.game, self.scenario, patch, [tuple(h) for h in hops], tuple(at) if at else None, self.set_status).process
            return {"pid": self.process.pid, "automated": True}
        command = [str(ROOT / "build" / f"biohazard-{self.game}"), "--disc", disc]
        if patch:
            command += ["--patch", patch]
        self.process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        return {"pid": self.process.pid, "automated": False}

    def set_status(self, text):
        self.status = text

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
        self.process = None


def start_records(game, scenario):
    """(the scenario's event file bytes, [word index of the room in each opening `SetCharacterStart slot, room, slot` record]): the first run of op 0x69 records, one a character
    (measured, file 2 zoo: the rooms in them are where each of the four starts; the start spot is that room's own). ([], None) when the scenario has none"""
    import struct

    disc = archive.Archive(game)
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            data = disc.open(f"{dat}/r{scenario:03d}.afs/r{scenario:03d}.evb").read()
            break
        except FileNotFoundError:
            continue
    else:
        return None, []
    words = struct.unpack(f"<{len(data) // 4}I", data)
    found, i = [], 0
    while i + 3 < min(len(words), 400):
        if words[i] == 0x2000069 and words[i + 1] == len(found):       # one for each slot in turn: 0, 1, 2, 3
            found.append(i + 2)
            i += 4
        elif found:
            break
        else:
            i += 1
    return data, found


def start_room(game, scenario):
    data, at = start_records(game, scenario)
    if not at:
        return None
    import struct

    return struct.unpack_from("<I", data, 4 * at[0])[0]


def moved_start(game, scenario, edits):
    """{(pack, member): bytes}: the scenario's event file with its characters starting in the room edits["start_room"] names"""
    import struct

    if edits.get("start_room") is None:
        return {}
    data, at = start_records(game, scenario)
    if not at:
        raise ValueError("this scenario's start room isn't known")
    out = bytearray(data)
    for w in at:
        struct.pack_into("<I", out, 4 * w, int(edits["start_room"]))
    return {(f"r{scenario:03d}.afs", f"r{scenario:03d}.evb"): bytes(out)}


def _room_model(disc, scenario, name):
    """The bytes of a room model (`name`, an .nbd) of the scenario's pack, from whichever of the disc's two archives holds it"""
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            return disc.open(f"{dat}/r{scenario:03d}.afs/{name}").read()
        except FileNotFoundError:
            continue
    raise ValueError(f"{name}: not on the disc")


def scenery_as_models(game, scenario, edits):
    """The page keeps a room's moved objects under the room (`scenery`: object number -> scale, rotation, offset); the patch builder reads them as the room model's bones
    (each object's own, bh_nbd.mesh_bones). Rooms that also made copies of objects (`new_objects`) are left out: their models are rebuilt whole (grown_models)"""
    import bh_nbd

    models = {name: dict(spec) for name, spec in edits.get("models", {}).items()}
    disc = archive.Archive(game)
    for file, spec in edits.get("rooms", {}).items():
        if spec.get("new_objects") or not spec.get("scenery"):
            continue
        name = file.replace(".rdt", ".nbd")
        data = _room_model(disc, scenario, name)
        ahi = next(c for c in bh_nbd.chunks(data) if c[0] == "AHI")
        carriers = bh_nbd.mesh_bones(data, ahi[1], ahi[2])
        bones = models.setdefault(name, {}).setdefault("bones", {})
        for number, change in spec["scenery"].items():
            bones[str(carriers[int(number)])] = change
    return {**edits, "models": models}


def grown_models(game, scenario, edits):
    """({(pack, member): bytes} of the room models that got copies of objects: each copy appended (bh_nbd.clone_object), then every object's changes
    written in; [the .sld bytes of the pictures brought from other scenarios, in the order they take the numbers after the scenario's own]).
    A Library object of another scenario or game (`library` with "game" and "scenario") brings its pictures: each is laid as a new picture after the
    scenario's last (bh_studio_build.compose: the pack's member and the _h.bin's count) and the object's materials name it"""
    import bh_model
    import bh_nbd

    out, new_pictures, brought = {}, [], {}
    disc = archive.Archive(game)
    own_count = None
    for file, spec in edits.get("rooms", {}).items():
        if not spec.get("new_objects"):
            continue
        name = file.replace(".rdt", ".nbd")
        data = _room_model(disc, scenario, name)
        sources = {}
        for made in spec["new_objects"]:
            if made.get("library"):          # an object of another room, of this scenario or another's (of either game), with the pictures it needs
                lib = made["library"]
                where = (lib.get("game", game), int(lib.get("scenario", scenario)))
                src = lib["room"].replace(".rdt", ".nbd")
                if (where, src) not in sources:
                    sources[(where, src)] = bh_model.Model(_room_model(archive.Archive(where[0]), where[1], src))
                source, mesh = sources[(where, src)], int(lib["mesh"])
                pictures = None
                if where != (game, scenario):
                    if own_count is None:
                        own_count = picture_count(disc, scenario)
                    pictures = {}
                    for picture in source.mesh_pictures(mesh):
                        if (where, picture) not in brought:
                            brought[(where, picture)] = own_count + len(new_pictures)
                            new_pictures.append(_pack_member(archive.Archive(where[0]), where[1], picture))
                        pictures[picture] = brought[(where, picture)]
                model = bh_model.Model(data)
                model.import_object(source, mesh, pictures=pictures)
                data = model.to_bytes()
            else:
                data, _ = bh_nbd.clone_object(data, made["like"], None, made.get("geometry"))
        data = bytearray(data)
        ahi = next(c for c in bh_nbd.chunks(bytes(data)) if c[0] == "AHI")
        records = bh_nbd.bone_records(bytes(data), ahi[1], ahi[2])
        carriers = bh_nbd.mesh_bones(bytes(data), ahi[1], ahi[2])      # (the copies too: each copy's new bone names it)
        for number, change in spec.get("scenery", {}).items():
            bh_nbd.set_bone(data, records[carriers[int(number)]], **change)
        out[(f"r{scenario:03d}.afs", name)] = bytes(data)
    return out, new_pictures


def picture_count(disc, scenario):
    """How many pictures the scenario's pack begins with (the first word of its r<NNN>_h.bin, which then names the pack's other members in order)"""
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            head = dict(disc.open(dat).members())[f"r{scenario:03d}_h.bin"].read()
            return struct.unpack_from("<I", head, 0)[0]
        except (FileNotFoundError, KeyError):
            continue
    raise FileNotFoundError(f"r{scenario:03d}_h.bin: not on the disc")


def _pack_member(disc, scenario, index):
    """The bytes of member number `index` of the scenario's pack (a picture's .sld for the numbers below picture_count)"""
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            return disc.open(f"{dat}/r{scenario:03d}.afs").members()[index][1].read()
        except FileNotFoundError:
            continue
    raise FileNotFoundError(f"r{scenario:03d}.afs: not on the disc")


def repainted(game, scenario, edits):
    """{(pack, member): bytes} of the room pictures painted over with your images (edits["textures"]: the picture's number in the pack -> a data: URL)"""
    import base64
    import io

    from PIL import Image

    import bh_sld
    import tm2

    out = {}
    if not edits.get("textures"):
        return out
    disc = archive.Archive(game)
    pack = f"r{scenario:03d}.afs"
    members = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            members = disc.open(f"{dat}/{pack}").members()
            break
        except FileNotFoundError:
            continue
    for index, url in edits["textures"].items():
        name, node = members[int(index)]
        image = Image.open(io.BytesIO(base64.b64decode(url.split(",", 1)[1])))
        out[(pack, name)] = bh_sld.pack(tm2.restyle(bh_sld.unpack(node.read()), image))
    return out


def make_handler(studio):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, code, body, kind="application/json"):
            payload = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)

        def body(self):
            return json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")

        def do_GET(self):
            url = urlparse(self.path)
            query = parse_qs(url.query)
            try:
                if url.path == "/api/scene":
                    self.send(200, json.dumps(studio.scene()))
                elif url.path == "/api/project":
                    self.send(200, json.dumps(studio.project()))
                elif url.path == "/api/model":
                    self.send(200, json.dumps(studio.model(query["file"][0], query.get("game", [None])[0], query.get("scenario", [None])[0])))
                elif url.path == "/api/levels":
                    self.send(200, json.dumps(studio.levels()))
                elif url.path == "/api/tex":
                    png = studio.texture(int(query["index"][0]), query.get("game", [None])[0], query.get("scenario", [None])[0])
                    self.send(200 if png else 404, png or b"", "image/png")
                elif url.path == "/api/status":
                    running = bool(studio.process and studio.process.poll() is None)
                    self.send(200, json.dumps({"running": running, "status": getattr(studio, "status", ""), "automatable": bh_autoplay.supported(studio.game, studio.scenario), "start_room": bh_autoplay.KNOWN.get((studio.game, studio.scenario), {}).get("start_room")}))
                elif url.path == "/api/download":
                    patch = ROOT / "work" / "studio" / f"{studio.game}-play.patch"
                    if not patch.exists():
                        self.send(404, "{}")
                    else:   # the last build, to keep: the runtime takes it with --patch
                        data = patch.read_bytes()
                        self.send_response(200)
                        self.send_header("Content-Type", "application/octet-stream")
                        self.send_header("Content-Length", str(len(data)))
                        self.send_header("Content-Disposition", f'attachment; filename="my-{studio.game}-level.patch"')
                        self.end_headers()
                        self.wfile.write(data)
                elif url.path == "/api/character":
                    self.send(200, json.dumps(studio.character(int(query["kind"][0]), int(query["cls"][0]))))
                elif url.path == "/api/characters":
                    import bh_characters
                    self.send(200, json.dumps(bh_characters.enemy_models(studio.game)))
                elif url.path == "/api/character/picture":
                    self.send(200, studio.character_picture(query["key"][0]), "image/png")
                elif url.path == "/api/messages":
                    self.send(200, json.dumps(studio.messages()))
                elif url.path.startswith("/api/"):
                    self.send(404, "{}")
                else:
                    name = "index.html" if url.path in ("", "/") else url.path.lstrip("/")
                    target = (WEB / name).resolve()
                    if WEB.resolve() not in target.parents and target != WEB.resolve() or not target.is_file():
                        self.send(404, "not found", "text/plain")
                    else:
                        self.send(200, target.read_bytes(), TYPES.get(target.suffix, "application/octet-stream"))
            except Exception as error:  # told to the page, not a crash
                traceback.print_exc()
                self.send(500, json.dumps({"error": str(error)}))

        def do_PUT(self):
            try:
                if urlparse(self.path).path == "/api/project":
                    studio.save_project(self.body())
                    self.send(200, "{}")
                else:
                    self.send(404, "{}")
            except Exception as error:
                self.send(500, json.dumps({"error": str(error)}))

        def do_POST(self):
            path = urlparse(self.path).path
            try:
                body = self.body()
                if path == "/api/build":
                    self.send(200, json.dumps(studio.build(body.get("edits", {}), body.get("english", True))))
                elif path == "/api/play":
                    self.send(200, json.dumps(studio.play(body.get("patch"), body.get("hops"), body.get("at"))))
                elif path == "/api/stop":
                    studio.stop()
                    self.send(200, "{}")
                elif path == "/api/switch":
                    studio.switch(body["game"], int(body["scenario"]))
                    self.send(200, "{}")
                else:
                    self.send(404, "{}")
            except (SystemExit, Exception) as error:
                traceback.print_exc()
                self.send(400, json.dumps({"error": str(error)}))

    return Handler


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    port = 8770
    if "--port" in argv:
        port = int(argv[argv.index("--port") + 1])
        args = [a for a in args if a != str(port)]
    game = args[0] if args else "file2"
    scenario = int(args[1]) if len(args) > 1 else scenarios(game)[0]
    studio = Studio(game, scenario)
    server = ThreadingHTTPServer(("127.0.0.1", port), make_handler(studio))
    url = f"http://127.0.0.1:{port}/"
    print(f"Outbreak Studio: {url}   ({game} r{scenario:03d}; Ctrl+C stops)")
    if "--no-open" not in argv and sys.platform == "darwin":
        subprocess.Popen(["open", url])
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        studio.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
