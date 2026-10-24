#!/usr/bin/env python3
"""Pictures of the game itself for the editor's real view: a headless game kept running on the last built patch, taken to a room and a place and photographed (the runtime's
--control `snap`), so the editor can show exactly what the player sees, characters, light and all. Only the first scenarios of both games have their menus known (bh_autoplay.KNOWN).

    Preview(game, scenario).prepare(patch, start_room)       starts the game (again, when the patch changed) and takes it into play
    Preview.shot(hops, at, name) -> path of a JPEG           puts the player through the doors `hops` (each a (x, z) to stand at), at `at` = (x, z, facing), and photographs
"""
import struct
import threading
import time
from pathlib import Path

import bh_autoplay as auto
import ai_scenario as S

ROOT = Path(__file__).resolve().parent.parent
WIDTH = 1100          # the pictures are sent this wide (the game's own are 2048)


class Preview:
    def __init__(self, game, scenario, kind="preview"):
        self.game, self.scenario = game, scenario
        self.info = auto.KNOWN[(game, scenario)]
        self.folder = ROOT / "work" / "studio" / kind / f"{game}-{scenario}"
        self.run = None
        self.patch = None
        self.room = None
        self.free_camera = False
        self.lock = threading.Lock()

    def alive(self):
        return self.run is not None and self.run.alive()

    def stop(self):
        if self.run is not None:
            self.run.quit()
        self.run = None
        self.patch = None

    def prepare(self, patch, start_room):
        """The game running on `patch` and in play: the room it starts in. Starts it again when the patch is a different one or the game has ended"""
        with self.lock:
            if self.alive() and self.patch == patch:
                return self.room
            self.stop()
            self.free_camera = False
            self.run = auto.AutoPlay(self.game, patch, headless=True, folder=self.folder)
            S.through_menus(self.run, auto.keys_for(self.info["menus"]))
            time.sleep(4)
            self.patch, self.room = patch, start_room
            return self.room

    def room_now(self):
        """The room number the game is in (its low half: the high half is the variant)"""
        return self.run.peek(self.info["room_at"], 1)[0] & 0xFFFF

    def _poke(self, x, z, facing=None):
        base = self.info["player"]
        self.run.send(f"poke {base + 0x38:x} {auto.float_hex(x)}")
        self.run.send(f"poke {base + 0x40:x} {auto.float_hex(z)}")
        if facing is not None:
            self.run.send(f"poke {base + 0x90:x} {int(facing) << 16:08x}")

    def live_camera(self):
        """(eye, target) of the camera the game is using now, from its memory"""
        words = self.run.peek(self.info["camera"], 6)
        f = [struct.unpack("<f", struct.pack("<I", w))[0] for w in words]
        return tuple(f[:3]), tuple(f[3:6])

    def walk(self, hops):
        with self.lock:
            for x, z in hops:
                self._poke(x, z)
                time.sleep(6)

    def stand(self, x, z):
        """Puts the player at (x, z) and says the room and the camera the game picked: (room number, eye, target)"""
        with self.lock:
            self._poke(x, z)
            time.sleep(3.2)
            return (self.room_now(), *self.live_camera())

    def photo(self, name):
        """The picture of what is on the screen now, as a JPEG path"""
        from PIL import Image

        with self.lock:
            shots = self.folder / "shots"
            before = set(shots.glob("*"))
            self.run.send(f"snap {name}")
            for _ in range(60):
                time.sleep(0.25)
                new = [p for p in shots.glob(f"{name}*") if p not in before]
                if new:
                    time.sleep(0.4)
                    break
            else:
                raise RuntimeError("the game made no picture")
            out = self.folder / f"{name}.jpg"
            image = Image.open(new[0]).convert("RGB")
            image.resize((WIDTH, round(image.height * WIDTH / image.width)), Image.LANCZOS).save(out, "JPEG", quality=88)
            new[0].unlink()
            return out

    MAIL = 0x01D60000          # the Modern camera driver's mailbox (src/modern_camera.cpp): eye, look, field of view, then the magic word
    MAGIC = 0x44555453

    def _camera(self, camera):
        """The game's camera held at `camera` ({eye, target, fov}), or given back when None"""
        if camera is None:
            if self.free_camera:
                self.run.send(f"poke {self.MAIL:x} 0")
                self.free_camera = False
                time.sleep(0.8)
            return
        values = list(camera["eye"]) + list(camera["target"]) + [camera["fov"]]
        for i, v in enumerate(values):
            self.run.send(f"poke {self.MAIL + 4 + 4 * i:x} {auto.float_hex(v)}")
        self.run.send(f"poke {self.MAIL:x} {self.MAGIC:08x}")
        self.free_camera = True

    def shot(self, room, hops, at, name, camera=None):
        """The picture (a JPEG path) of the game with the player at `at` in room number `room` after `hops` through the doors; from `camera` ({eye, target, fov}) when given, wherever that is"""
        from PIL import Image

        with self.lock:
            if not self.alive():
                raise RuntimeError("the game isn't running")
            if camera is not None and self.free_camera and self.room == room and not hops:
                self._camera(camera)             # the player is in place and the game already shows a view of this kind: only the view moves
                time.sleep(0.9)
            else:
                self._camera(None)
                for x, z in hops:
                    self._poke(x, z)
                    time.sleep(6)
                self._poke(at[0], at[1], at[2] if len(at) > 2 else None)
                time.sleep(3.5)
                if camera is not None:
                    self._camera(camera)
                    time.sleep(1.2)
            shots = self.folder / "shots"
            before = set(shots.glob("*"))
            self.run.send(f"snap {name}")
            for _ in range(60):
                time.sleep(0.25)
                new = [p for p in shots.glob(f"{name}*") if p not in before]
                if new:
                    time.sleep(0.4)
                    break
            else:
                raise RuntimeError("the game made no picture")
            out = self.folder / f"{name}.jpg"
            image = Image.open(new[0]).convert("RGB")
            image.resize((WIDTH, round(image.height * WIDTH / image.width)), Image.LANCZOS).save(out, "JPEG", quality=88)
            new[0].unlink()
            self.room = room
            return out

    def where(self):
        with self.lock:
            return self.room_now()


def door_graph(rooms):
    """{room id: [(room id it leads to, (x, z) to stand at to take that door)]} from the scene's rooms (the doors and exits of each room's first variant)"""
    graph = {}
    for r in rooms:
        if r["variant"]:
            continue
        edges = graph.setdefault(r["id"], [])
        for a in r["areas"]:
            if a["tag"] in (0, 1):
                q = a["quad"]
                edges.append((a["fields"][1], (sum(p[0] for p in q) / len(q), sum(p[1] for p in q) / len(q))))
    return graph


def route(graph, start, goal):
    """The (x, z) points to stand at, door by door, to walk from room `start` to room `goal`, or None"""
    if start == goal:
        return []
    seen, queue = {start: None}, [start]
    while queue:
        here = queue.pop(0)
        for to, point in graph.get(here, []):
            if to in seen or to not in graph:
                continue
            seen[to] = (here, point)
            if to == goal:
                out, at = [], to
                while seen[at]:
                    out.insert(0, seen[at][1])
                    at = seen[at][0]
                return out
            queue.append(to)
    return None


class Survey(threading.Thread):
    """Photographs every camera of every room of the scenario as the game draws it (the unedited scenario), into work/studio/survey/GAME/r<room>c<camera>.jpg, one game kept running
    through the doors; a picture already there is kept, so it can stop and go on"""

    def __init__(self, studio):
        super().__init__(daemon=True)
        self.studio = studio
        self.game, self.scenario = studio.game, studio.scenario
        self.info_start = auto.KNOWN[(self.game, self.scenario)]['start_room']
        self.done, self.total, self.current, self.error, self.finished = 0, 0, "", "", False
        self.stop_now = False

    @staticmethod
    def which_camera(room, eye, target):
        """The number of the room's camera nearest the one the game is using (a following camera moves a little)"""
        best, bd = 0, 1e18
        for i, c in enumerate(room["cameras"]):
            d = sum((a - b) ** 2 for a, b in zip(c["eye"], eye)) + 0.5 * sum((a - b) ** 2 for a, b in zip(c["target"], target))
            if d < bd:
                best, bd = i, d
        return best

    @staticmethod
    def places(room, graph):
        """Places to stand in the room: in each camera's box (its middle and four points toward its corners), the farthest from the room's doors and exits first (standing in one takes the player out)"""
        doors = []
        for a in room["areas"]:
            if a["tag"] in (0, 1):
                q = a["quad"]
                doors.append((sum(p[0] for p in q) / len(q), sum(p[1] for p in q) / len(q)))
        spots = []
        for c in room["cameras"]:
            b = c["box"][:4]
            cx, cz = sum(p[0] for p in b) / 4, sum(p[2] for p in b) / 4
            spots += [(cx, cz)] + [(cx + (p[0] - cx) * 0.5, cz + (p[2] - cz) * 0.5) for p in b]
        spots = [s for s in spots if min([((s[0] - d[0]) ** 2 + (s[1] - d[1]) ** 2) ** 0.5 for d in doors] or [1e9]) > 160]
        spots.sort(key=lambda s: -min([((s[0] - d[0]) ** 2 + (s[1] - d[1]) ** 2) ** 0.5 for d in doors] or [1e9]))
        return spots[:40]

    def shoot(self, preview, r, missing, graph, at_room):
        """Stands in the room's camera boxes and photographs each camera the game picks that is still wanted; the room the player is in now"""
        rid = r["id"]
        for point in self.places(r, graph):
            if not missing or self.stop_now:
                break
            room, eye, target = preview.stand(point[0], point[1])
            if room != rid:             # a door was stepped through: back to the room
                back = route(graph, room, rid)
                if back is None:
                    return room
                preview.walk(back)
                continue
            k = self.which_camera(r, eye, target)
            if k in missing:
                preview.photo(f"r{rid}c{k}")
                missing.discard(k)
                self.done += 1
                self.current = f"room {rid}, camera {k + 1}"
        return rid

    def run(self):
        try:
            scene = self.studio.scene()
            rooms = [r for r in scene["rooms"] if not r["variant"]]
            graph = door_graph(scene["rooms"])
            start = scene.get("start_room") or self.info_start
            jobs = [(r, k) for r in rooms for k in range(len(r["cameras"]))]
            self.total = len(jobs)
            patch = self.studio.build({}, name=f"survey-{self.game}.patch")["patch"]
            preview = Preview(self.game, self.scenario, "survey")
            out = preview.folder
            out.mkdir(parents=True, exist_ok=True)
            todo = [(r, k) for r, k in jobs if not (out / f"r{r['id']}c{k}.jpg").exists()]
            self.done = self.total - len(todo)
            if todo:
                self.current = "starting the game"
                preview.prepare(patch, start)
            at_room = start
            # rooms in the order the doors lead to them, so the walking between is short
            order = {}
            queue = [start]
            order[start] = 0
            while queue:
                here = queue.pop(0)
                for to, _ in graph.get(here, []):
                    if to not in order and to in graph:
                        order[to] = len(order)
                        queue.append(to)
            todo.sort(key=lambda j: (order.get(j[0]["id"], 999), j[1]))
            by_room = {}
            for r, k in todo:
                by_room.setdefault(r["id"], (r, set()))[1].add(k)
            unreachable = []
            for rid in sorted(by_room, key=lambda i: order.get(i, 999)):
                if self.stop_now:
                    break
                r, missing = by_room[rid]
                hops = route(graph, at_room, rid)
                if hops is None:
                    unreachable.append(rid)
                    continue
                self.current = f"room {rid}"
                try:
                    preview.walk(hops)
                    at_room = preview.where()
                    if at_room != rid:          # a door that wouldn't open (locked, or the story isn't there yet)
                        unreachable.append(rid)
                        continue
                    at_room = self.shoot(preview, r, missing, graph, at_room)
                except Exception:      # the game stopped: start it again where it begins
                    preview.stop()
                    preview.prepare(patch, start)
                    at_room = start
            # rooms the doors don't lead to: a game started in the room itself (the event file's start records), where the scenario has them
            import bh_studio

            for rid in unreachable:
                if self.stop_now:
                    break
                r, missing = by_room[rid]
                if not bh_studio.start_records(self.game, self.scenario)[1]:
                    self.done += len(missing)
                    continue
                self.current = f"room {rid}: starting a game there"
                try:
                    preview.stop()
                    built = self.studio.build({"rooms": {}, "start_room": rid}, name=f"survey-{self.game}-{rid}.patch")["patch"]
                    preview.prepare(built, rid)
                    self.shoot(preview, r, missing, graph, rid)
                except Exception:
                    preview.stop()
                self.done += len(missing)
            preview.stop()
        except Exception as error:
            self.error = str(error)
        self.finished = True
        self.current = ""


class SurveyAll(threading.Thread):
    """The surveys of every scenario whose menus are known, one after another (File 1's first, then File 2's)"""

    def __init__(self, make_studio):
        super().__init__(daemon=True)
        self.make_studio = make_studio
        self.current = None
        self.finished = False
        self.stop_now = False

    def run(self):
        for game, scenario in sorted(auto.KNOWN):
            if self.stop_now:
                break
            studio = self.make_studio(game, scenario)
            self.current = Survey(studio)
            self.current.run()          # in this thread
            if self.current.error:
                break
        self.finished = True
