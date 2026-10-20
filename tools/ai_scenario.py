#!/usr/bin/env python3
"""Runs a partner AI scenario headless, several times and side by side, with the game's own AI or this port's, and sums up
what the partners did (tools/ai_report.py on each run's BH_AI_LOG).

    ai_scenario.py SCENARIO MODE [--seeds N] [--parallel N] [--seconds S] [--out DIR]

SCENARIO  bar-idle      File 1, Outbreak (J's Bar), Kevin, normal: the player stands still from the start of play while
                        the zombies come in; the partners alone stand between them and the player
          bar-commands  the same, the player running off and back and giving each command in turn (This way!, Mark and
                        Cindy by name, Go!, Help me!), 12 s apart, then standing still
          zoo-idle      File 2, Wild Things (the zoo), the first character and partners, easy: the player standing still
MODE      New or Original (the PC menus' Gameplay: Partner AI)
--seconds the game time measured from the start of play (240); a run ends early when the partners stop thinking (the
          player's game over): its survived_seconds says when
--seeds   runs with seeds 0..N-1: the game runs the same for the same input, so a seed (the player's few steps first) is
          what makes runs differ; the same seed in both modes is the same game until the AIs differ

Each run gets its own folder under --out (memory card, settings, pictures every 30 s, ai.log, game.log). Runs use the build's
programs (build/biohazard-file1 and -file2) and the English patch run.sh makes. The game's speed depends on the machine's
load, so the length is game time (the log's), not the clock's."""
import json
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import ai_report  # noqa: E402


class Run:
    """One game, headless, driven through the runtime's --control FIFO."""

    def __init__(self, game, folder, env):
        self.game, self.folder = game, folder
        self.fifo, self.log = folder / "c", folder / "game.log"
        self.fifo.unlink(missing_ok=True)  # a FIFO left by an earlier run would be taken for this game's
        disc = os.environ.get(f"{game.upper()}_DISC") or json.loads((ROOT / "local.json").read_text())[game]
        cmd = [str(ROOT / "build" / f"biohazard-{game}"), "--disc", disc, "--patch", str(ROOT / "build" / f"english-{game}.patch"),
               "--headless", "--run-seconds", "6000", "--fps", "--control", str(self.fifo), "--snapshots", str(folder / "shots"),
               "--memcards", str(folder / "card"), "--settings", str(folder / "card")]
        self.process = subprocess.Popen(cmd, stdout=open(self.log, "w"), stderr=subprocess.STDOUT, env={**os.environ, **env},
                                        start_new_session=True)
        for _ in range(600):
            if self.fifo.exists():
                return
            time.sleep(0.1)
        raise RuntimeError(f"{folder}: no control FIFO")

    def send(self, line):
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        os.write(fd, (line + "\n").encode())
        os.close(fd)

    def key(self, keys, hold=0.3, wait=0.0):
        self.send(f"key {keys.replace(' ', '_')} {hold}")
        time.sleep(hold + 0.35 + wait)

    def peek(self, address, words=1):
        start = self.log.read_text(errors="replace").count("\n")
        self.send(f"peek {address:x} {words}")
        tag = f"peek {address:08x}:"
        for _ in range(100):
            time.sleep(0.05)
            for line in self.log.read_text(errors="replace").split("\n")[start:]:
                if tag in line:
                    return [int(w, 16) for w in line.split(tag)[1].split()]
        raise RuntimeError(f"{self.folder}: no answer to peek {address:x}")

    def quit(self):
        try:
            self.send("quit")
        except OSError:
            pass
        try:
            self.process.wait(10)
        except subprocess.TimeoutExpired:
            os.killpg(self.process.pid, 9)


# The games run the same for the same input (headless): two runs of a scenario are the same game (their logs byte for byte;
# a longer wait on the title menu changes nothing). A run's seed has the player walk that many times this long first, and
# from there the enemies and partners go their own ways
SEED_STEP = 0.25

# The games' task list and the tasks that tell play (src/controls.cpp's Layout)
# (and the title's state and cursor: the title menu is the executable's loop alone, the state at its menus' value and the
# byte 12 before the cursor 1; File 1's game overlay's own state byte, 2 in play and 14 in its opening credits)
TASKS = {"file1": dict(count=0x23BB30, list=0x29B0D0, game=0x341B80, cutscene=0x341AF0, status=0x6B9450, base=0x1B3400,
                       title=0x200D5C, title_menus=1, cursor=0x90D1CC, play_state=0x48BF60, playing=2),
         "file2": dict(count=0x24AD3C, list=0x2AA890, game=0x344CB0, cutscene=0x344C10, status=0x6C8FA0, base=0x1B3FA0,
                       title=0x24AC70, title_menus=3, cursor=0x8BFE4C, play_state=0, playing=0)}


def byte(run, address):
    return (run.peek(address & ~3)[0] >> (8 * (address & 3))) & 0xFF


def tasks(run):
    t = TASKS[run.game]
    n = min(run.peek(t["count"])[0], 8)
    return run.peek(t["list"], n) if n else []


def in_play(run):
    t = TASKS[run.game]
    active = tasks(run)
    if t["game"] not in active or t["cutscene"] in active or t["status"] in active:
        return False
    return not t["play_state"] or byte(run, t["play_state"]) == t["playing"]


def on_title_menu(run):
    t = TASKS[run.game]
    return (tasks(run) == [t["base"]] and run.peek(t["title"])[0] == t["title_menus"] and
            byte(run, t["cursor"] - 12) == 1)


def to_title_menu(run, timeout=240):
    """From boot: Escape (START) every few seconds through the logos, movies and PRESS START until the title menu is up"""
    start = time.time()
    while time.time() - start < timeout:
        if on_title_menu(run):
            return
        if tasks(run) == [TASKS[run.game]["base"]]:
            run.key("Escape", 0.4)
        time.sleep(3)
    raise RuntimeError(f"{run.folder}: no title menu")


def wait_for_play(run, timeout=240):
    """Skips cutscenes (Escape while the cutscene task runs, or while File 1's game overlay plays its opening credits),
    closes the status screen a skip opens as a scene ends, and never presses Escape in play, where it's the pause."""
    t = TASKS[run.game]
    start = time.time()
    while time.time() - start < timeout:
        if in_play(run):
            return
        active = tasks(run)
        if t["status"] in active:
            run.key("Tab")  # a skip that landed as the scene ended opened the status screen (START): closed again
        elif t["cutscene"] in active or (t["game"] in active and t["play_state"] and byte(run, t["play_state"]) == 14):
            run.key("Escape")
        time.sleep(2)
    raise RuntimeError(f"{run.folder}: play didn't start")


def to_bar(run):
    """File 1 from boot: the title, single play, scenario (Outbreak), Kevin, normal."""
    to_title_menu(run)
    for wait in (3, 4, 3, 3, 3, 10):
        run.key("E", wait=wait)
    wait_for_play(run)


def to_zoo(run):
    """File 2 from boot: the title, single play, new game, Wild Things (the second scenario), the first character and
    partners, easy."""
    to_title_menu(run)
    run.key("E", wait=4)
    run.key("E", wait=6)
    run.key("Right", wait=3)
    for wait in (4, 4, 4, 3):
        run.key("E", wait=wait)
    run.key("Up", wait=1)
    run.key("E", wait=10)
    wait_for_play(run)


def log_time(log):
    """The last time the AI log has (seconds of game time), 0 before its first line."""
    last = 0.0
    if log.exists():
        for line in log.read_text(errors="replace").splitlines()[-40:]:
            if line.startswith("S t="):
                last = float(line.split()[1][2:])
    return last


STALL = 30  # seconds of the clock without a new line in the AI log: the partners aren't thinking (game over, a load)


def play_until(run, log, end):
    """Waits for the game time `end`, a picture every 30 s; stops early if the log stalls (game over)."""
    shot, last, seen = 0, time.time(), log_time(log)
    while log_time(log) < end:
        time.sleep(5)
        now = log_time(log)
        if now != seen:
            seen, last = now, time.time()
        elif time.time() - last > STALL:
            return
        if time.time() - last < 6 and int(now) // 30 > shot:
            shot = int(now) // 30
            run.send(f"snap s{shot}")


def idle(run, log, seconds, seed):
    start = log_time(log)
    if seed:
        run.key("W", SEED_STEP * seed)  # a few steps forward first: the seed's own game
    play_until(run, log, start + seconds)
    return start


def wait_game(log, seconds):
    """Waits for `seconds` of game time (the AI log's)."""
    until = log_time(log) + seconds
    while log_time(log) < until:
        time.sleep(0.5)


def commands(run, log, seconds, seed):
    """Before the zombies come in: the player runs off and calls "This way!", runs back and calls Mark (F+3, the call
    with code 9), runs off again and calls Cindy (F+1, code 10), runs back and says "Go!", then "Help me!"; each 12 s of
    game time apart; then stands still"""
    start = log_time(log)
    for move, call in (("W", "2"), ("S", "F+3"), ("W", "F+1"), ("S", "1"), (None, "3")):
        if move:
            run.key(f"Left_Shift+{move}", 2.5)
        run.key(call)
        wait_game(log, 12)
    play_until(run, log, start + seconds)
    return start


SCENARIOS = {"bar-idle": ("file1", to_bar, idle), "bar-commands": ("file1", to_bar, commands),
             "zoo-idle": ("file2", to_zoo, idle)}


def one_run(scenario, mode, seed, folder, seconds):
    game, navigate, play = SCENARIOS[scenario]
    shutil.rmtree(folder, ignore_errors=True)
    (folder / "card").mkdir(parents=True)
    (folder / "shots").mkdir()
    (folder / "card" / "options.txt").write_text(f"pause_in_background = 0\nGameplay: Partner AI = {mode}\n")
    log = folder / "ai.log"
    run = Run(game, folder, {"BH_AI_LOG": str(log)})
    try:
        navigate(run)
        start = play(run, log, seconds, seed)
    finally:
        run.quit()
    samples, events = ai_report.parse(str(log), start, start + seconds)
    result = ai_report.report(samples, events)
    result["survived_seconds"] = round(min(seconds, log_time(log) - start), 1)
    return result


def main():
    args = sys.argv[1:]
    if len(args) < 2 or args[0] not in SCENARIOS or args[1] not in ("New", "Original"):
        sys.exit(__doc__)
    opt = {args[i]: args[i + 1] for i in range(2, len(args) - 1, 2)}
    seeds, parallel = int(opt.get("--seeds", 1)), int(opt.get("--parallel", 2))
    seconds = float(opt.get("--seconds", 240))
    out = Path(opt.get("--out", "/tmp/biohazard-ai")) / f"{args[0]}-{args[1]}"
    with ThreadPoolExecutor(parallel) as pool:
        results = list(pool.map(lambda n: one_run(args[0], args[1], n, out / f"seed{n}", seconds), range(seeds)))
    (out / "summary.json").write_text(json.dumps(results, indent=1))
    for n, r in enumerate(results):
        print(f"seed {n}: {ai_report.one_line(r)}")
    print("mean:", ai_report.one_line(ai_report.mean(results)))


if __name__ == "__main__":
    main()
