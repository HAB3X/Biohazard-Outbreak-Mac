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
    def __init__(self, game, scenario):
        self.game, self.scenario = game, scenario
        self.info = auto.KNOWN[(game, scenario)]
        self.folder = ROOT / "work" / "studio" / "preview" / game
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
