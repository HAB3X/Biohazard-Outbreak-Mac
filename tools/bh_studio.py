#!/usr/bin/env python3
"""Outbreak Studio: the level editor for File 1 and File 2. A local web page (tools/studio/web) over these tools' readers and patch builders.

    bh_studio.py [GAME [SCENARIO]] [--port N] [--no-open]

GAME is file1 or file2 (File 2 by default), SCENARIO the scenario's number (40 is File 2's first). Your work is kept in work/studio/ and the page saves it as you go;
Build writes work/studio/GAME-play.patch (the English patch, if the game has one, and your edits, through tools/bh_studio_build.py) and Play starts the game with it.
docs/studio/PLAN.md says what the editor is for and what it can do so far.
"""
import json
import os
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
        self.preview, self.preview_lock = None, threading.Lock()
        self.survey = None

    # ---- what the page asks for
    def scene(self):
        with self.lock:
            key = ("scene", self.game, self.scenario)
            if key not in self.cache:
                rooms = maps.build(self.game, self.scenario, self.names, False)
                self.cache[key] = {"game": self.game, "scenario": self.scenario, "rooms": rooms, "scenarios": scenarios(self.game),
                                   "ops": {str(k): list(v) for k, v in rdt.OPS.items()}, "start_room": start_room(self.game, self.scenario)}
            return self.cache[key]

    def model(self, file):
        with self.lock:
            key = ("model", self.game, self.scenario, file)
            if key not in self.cache:
                self.cache[key] = legacy.model_json(self.game, self.scenario, file)
            return self.cache[key]

    def texture(self, index):
        with self.lock:
            key = ("tex", self.game, self.scenario, index)
            if key not in self.cache:
                self.cache[key] = legacy.texture_png(self.game, self.scenario, index)
            return self.cache[key]

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
        self.stop_preview()

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
        report = studio_build.compose(self.game, scenery_as_models(edits), str(out), base, members={**grown_models(self.game, self.scenario, edits), **repainted(self.game, self.scenario, edits), **moved_start(self.game, self.scenario, edits)})
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

    # ---- the real view: pictures of the game itself
    def preview_prepare(self, edits):
        import bh_preview

        if not bh_autoplay.supported(self.game, self.scenario):
            raise ValueError("the real view needs a scenario whose menus are known (File 1's first, File 2's first)")
        with self.preview_lock:
            if self.preview is None or (self.preview.game, self.preview.scenario) != (self.game, self.scenario):
                if self.preview is not None:
                    self.preview.stop()
                self.preview = bh_preview.Preview(self.game, self.scenario)
        import hashlib

        tag = hashlib.sha1(json.dumps(edits, sort_keys=True).encode()).hexdigest()[:10]
        built = self.build(edits, name=f"preview-{self.game}-{tag}.patch")
        start = edits.get("start_room")
        if start is None:
            start = start_room(self.game, self.scenario) or bh_autoplay.KNOWN[(self.game, self.scenario)]["start_room"]
        return {"room": self.preview.prepare(built["patch"], start), "seconds": built["seconds"]}

    def preview_shot(self, room, hops, at, name):
        if self.preview is None:
            raise ValueError("prepare the real view first")
        path = self.preview.shot(room, [tuple(h) for h in hops], tuple(at), name)
        return {"url": f"/api/shot?name={path.name}", "room": room}

    def survey_start(self):
        import bh_preview

        if not bh_autoplay.supported(self.game, self.scenario):
            return {"supported": False}
        if self.survey is None or self.survey.finished or (self.survey.game, self.survey.scenario) != (self.game, self.scenario):
            if self.preview is not None:        # one game at a time
                self.stop_preview()
            self.survey = bh_preview.Survey(self)
            self.survey.start()
        return self.survey_status()

    def survey_status(self):
        s = self.survey
        have = sorted(p.stem for p in (ROOT / "work" / "studio" / "survey" / self.game).glob("r*c*.jpg")) if (ROOT / "work" / "studio" / "survey" / self.game).exists() else []
        if s is None or (s.game, s.scenario) != (self.game, self.scenario):
            return {"supported": bh_autoplay.supported(self.game, self.scenario), "running": False, "have": have}
        return {"supported": True, "running": not s.finished, "done": s.done, "total": s.total, "current": s.current, "error": s.error, "have": have}

    def set_status(self, text):
        self.status = text

    def stop_preview(self):
        if self.survey is not None and not self.survey.finished:
            self.survey.stop_now = True
        if self.preview is not None:
            self.preview.stop()
            self.preview = None

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
        if words[i] == 0x2000069 and words[i + 1] == words[i + 3]:
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


def scenery_as_models(edits):
    """The page keeps a room's moved objects under the room (`scenery`: object number -> scale, rotation, offset); the patch builder reads them as the room model's bones
    (object i stands on bone i + 2). Rooms that also made copies of objects (`new_objects`) are left out: their models are rebuilt whole (grown_models)"""
    models = {name: dict(spec) for name, spec in edits.get("models", {}).items()}
    for file, spec in edits.get("rooms", {}).items():
        if spec.get("new_objects"):
            continue
        for number, change in spec.get("scenery", {}).items():
            bones = models.setdefault(file.replace(".rdt", ".nbd"), {}).setdefault("bones", {})
            bones[str(int(number) + 2)] = change
    return {**edits, "models": models}


def grown_models(game, scenario, edits):
    """{(pack, member): bytes} of the room models that got copies of objects: each copy appended (bh_nbd.clone_object), then every object's changes written in"""
    import bh_nbd

    out = {}
    disc = archive.Archive(game)
    for file, spec in edits.get("rooms", {}).items():
        if not spec.get("new_objects"):
            continue
        name = file.replace(".rdt", ".nbd")
        node = None
        for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
            try:
                node = disc.open(f"{dat}/r{scenario:03d}.afs/{name}")
                break
            except FileNotFoundError:
                continue
        if node is None:
            raise ValueError(f"{name}: not on the disc")
        data = node.read()
        for made in spec["new_objects"]:
            data, _ = bh_nbd.clone_object(data, made["like"], None, made.get("geometry"))
        data = bytearray(data)
        ahi = next(c for c in bh_nbd.chunks(bytes(data)) if c[0] == "AHI")
        records = bh_nbd.bone_records(bytes(data), ahi[1], ahi[2])
        for number, change in spec.get("scenery", {}).items():
            bh_nbd.set_bone(data, records[int(number) + 2], **change)
        out[(f"r{scenario:03d}.afs", name)] = bytes(data)
    return out


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
                    self.send(200, json.dumps(studio.model(query["file"][0])))
                elif url.path == "/api/tex":
                    png = studio.texture(int(query["index"][0]))
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
                elif url.path == "/api/survey":
                    self.send(200, json.dumps(studio.survey_status()))
                elif url.path == "/api/shot":
                    name = Path(query["name"][0]).name
                    kind = "survey" if query.get("kind", [""])[0] == "survey" else "preview"
                    path = ROOT / "work" / "studio" / kind / studio.game / name
                    if not path.exists():
                        self.send(404, "{}")
                    else:
                        self.send(200, path.read_bytes(), "image/jpeg")
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
                elif path == "/api/survey/start":
                    self.send(200, json.dumps(studio.survey_start()))
                elif path == "/api/preview/prepare":
                    self.send(200, json.dumps(studio.preview_prepare(body.get("edits", {}))))
                elif path == "/api/preview/shot":
                    self.send(200, json.dumps(studio.preview_shot(body["room"], body.get("hops", []), body["at"], body["name"])))
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
