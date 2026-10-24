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
        self.folder = ROOT / "work" / "studio" / kind / game
        self.run = None
        self.patch = None
        self.room = None
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
            self.run = auto.AutoPlay(self.game, patch, headless=True, folder=self.folder)
            S.through_menus(self.run, auto.KEYS[self.info["menus"]])
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

    def shot(self, room, hops, at, name):
        """The picture (a JPEG path) of the game with the player at `at` in room number `room` after `hops` through the doors"""
        from PIL import Image

        with self.lock:
            if not self.alive():
                raise RuntimeError("the game isn't running")
            for x, z in hops:
                self._poke(x, z)
                time.sleep(6)
            self._poke(at[0], at[1], at[2] if len(at) > 2 else None)
            time.sleep(3.5)
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
        self.done, self.total, self.current, self.error, self.finished = 0, 0, "", "", False
        self.stop_now = False

    @staticmethod
    def places(room, k, graph):
        """Places to stand in camera k's box, the one farthest from the room's doors and exits first (standing in one takes the player out of the room)"""
        b = room["cameras"][k]["box"][:4]
        cx, cz = sum(p[0] for p in b) / 4, sum(p[2] for p in b) / 4
        spots = [(cx, cz)] + [(cx + (p[0] - cx) * f, cz + (p[2] - cz) * f) for p in b for f in (0.35,)]
        doors = []
        for a in room["areas"]:
            if a["tag"] in (0, 1):
                q = a["quad"]
                doors.append((sum(p[0] for p in q) / len(q), sum(p[1] for p in q) / len(q)))
        spots.sort(key=lambda s: -min([((s[0] - d[0]) ** 2 + (s[1] - d[1]) ** 2) ** 0.5 for d in doors] or [1e9]))
        return [(s[0], s[1], 0) for s in spots[:3]]

    def run(self):
        try:
            scene = self.studio.scene()
            rooms = [r for r in scene["rooms"] if not r["variant"]]
            graph = door_graph(scene["rooms"])
            start = scene.get("start_room") or auto.KNOWN[(self.game, self.scenario)]["start_room"]
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
            for r, k in todo:
                if self.stop_now:
                    break
                self.current = f"room {r['id']}, camera {k + 1}"
                for point in self.places(r, k, graph):
                    hops = route(graph, at_room, r["id"])
                    if hops is None:
                        break
                    try:
                        preview.shot(r["id"], hops, point, f"r{r['id']}c{k}")
                        at_room = preview.where()
                    except Exception:      # the game stopped: start it again where it begins
                        preview.stop()
                        preview.prepare(patch, start)
                        at_room = start
                        continue
                    if at_room == r["id"]:
                        break
                    (out / f"r{r['id']}c{k}.jpg").unlink(missing_ok=True)      # the player stepped through a door and the picture is of another room
                self.done += 1
            preview.stop()
        except Exception as error:
            self.error = str(error)
        self.finished = True
        self.current = ""
