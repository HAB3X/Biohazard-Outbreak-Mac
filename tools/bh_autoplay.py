#!/usr/bin/env python3
"""Starts a game in a window and takes it into a scenario and to a room, for the editor's Play: the menus are pressed through the runtime's --control FIFO (the way
tools/ai_scenario.py does for its headless runs), then the player is put in the doors and exits that lead from the scenario's first room to the room wanted (the game
changes room when the player stands in one: measured), then at the place asked.

    AutoPlay(game, scenario, patch).start(hops=[(x, z), ...], at=(x, z, facing)) in a thread

The game runs in its own settings and memory card folder (work/studio/play/GAME), so nothing of yours is touched. Only the first scenarios of both games have their menus known:
File 1's "Outbreak" (the bar) and File 2's "Wild Things" (the zoo)."""
import os
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

import ai_scenario as S

ROOT = Path(__file__).resolve().parent.parent
KNOWN = {("file1", 1): {"menus": "bar", "room_at": 0x3065AC, "player": 0x476DD0, "start_room": 1, "camera": 0x306338},
         ("file2", 40): {"menus": "zoo", "room_at": 0x3137BC, "player": 0x47BD30, "start_room": 3, "camera": 0x313548}}


# File 2's single play menu lists its scenarios in this order (the scenario number and the presses of Right to reach it, measured by where the player started: 20 first, 40 after
# one press, 26 after three, 15 after four; two presses gave nothing: that scenario isn't open to start with); File 1 starts with only its first open
FILE2_RIGHT = {20: 0, 40: 1, 26: 3, 15: 4}
for _scenario in (20, 26, 15):
    KNOWN[("file2", _scenario)] = {**KNOWN[("file2", 40)], "menus": f"file2:{FILE2_RIGHT[_scenario]}", "start_room": {20: 1, 26: 2, 15: 1}[_scenario]}
KNOWN[("file2", 40)]["menus"] = "file2:1"


def supported(game, scenario):
    return (game, scenario) in KNOWN


def keys_for(menus):
    """The key presses from the title menu into the scenario named by KNOWN's "menus" (the bar's, or File 2's with some presses of Right)"""
    if menus.startswith("file2:"):
        return [("E", 4), ("E", 6)] + [("Right", 3)] * int(menus.split(":")[1]) + [("E", 4), ("E", 4), ("E", 4), ("E", 3), ("Up", 1), ("E", 10)]
    return KEYS[menus]


class AutoPlay(S.Run):
    """A windowed game on the control FIFO (S.Run's methods; its constructor starts a headless one)"""

    def __init__(self, game, patch, headless=False, folder=None):
        self.game = game
        self.folder = folder or ROOT / "work" / "studio" / "play" / game
        (self.folder / "card").mkdir(parents=True, exist_ok=True)
        (self.folder / "card" / "graphics.txt").write_text("version = 2\n") if not (self.folder / "card" / "graphics.txt").exists() else None
        self.fifo, self.log = self.folder / "c", self.folder / "game.log"
        self.fifo.unlink(missing_ok=True)
        disc = os.environ.get(f"{game.upper()}_DISC") or __import__("json").loads((ROOT / "local.json").read_text())[game]
        cmd = [str(ROOT / "build" / f"biohazard-{game}"), "--disc", disc, "--control", str(self.fifo), "--memcards", str(self.folder / "card"), "--settings", str(self.folder / "card")]
        if headless:
            (self.folder / "shots").mkdir(parents=True, exist_ok=True)
            cmd += ["--headless", "--run-seconds", "6000", "--snapshots", str(self.folder / "shots")]
        if patch:
            cmd += ["--patch", patch]
        self.process = subprocess.Popen(cmd, stdout=open(self.log, "w"), stderr=subprocess.STDOUT, start_new_session=True)
        for _ in range(600):
            if self.fifo.exists():
                return
            time.sleep(0.1)
        raise RuntimeError("the game's control pipe didn't appear")

    def alive(self):
        return self.process.poll() is None


def float_hex(x):
    return "%08x" % struct.unpack("<I", struct.pack("<f", float(x)))[0]


def run(play, scenario, hops, at, status):
    """The automation, in a thread: `status(text)` says how far it got"""
    info = KNOWN[(play.game, scenario)]
    try:
        status("Starting the game…")
        S.through_menus(play, keys_for(info["menus"]))
        time.sleep(4)
        for x, z in hops:
            if not play.alive():
                return
            status("Walking through the doors…")
            play.send(f"poke {info['player'] + 0x38:x} {float_hex(x)}")
            play.send(f"poke {info['player'] + 0x40:x} {float_hex(z)}")
            time.sleep(6)
        if at is not None and play.alive():
            status("Placing the player…")
            play.send(f"poke {info['player'] + 0x38:x} {float_hex(at[0])}")
            play.send(f"poke {info['player'] + 0x40:x} {float_hex(at[1])}")
            if len(at) > 2:
                play.send(f"poke {info['player'] + 0x90:x} {int(at[2]) << 16:08x}")
        status("")
    except Exception as error:  # the window stays: the player can carry on by hand
        status(f"Couldn't take the game there: {error}")


# the menus' key sequences: File 1's (the title, single play, the scenario Outbreak, Kevin, normal) and File 2's (Wild Things, the first character and partners, easy)
KEYS = {"bar": [("E", 3), ("E", 4), ("E", 3), ("E", 3), ("E", 3), ("E", 10)],
        "zoo": [("E", 4), ("E", 6), ("Right", 3), ("E", 4), ("E", 4), ("E", 4), ("E", 3), ("Up", 1), ("E", 10)]}


def start(game, scenario, patch, hops, at, status, headless=False):
    play = AutoPlay(game, patch, headless)
    threading.Thread(target=run, args=(play, scenario, hops, at, status), daemon=True).start()
    return play
