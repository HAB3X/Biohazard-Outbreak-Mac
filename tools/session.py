#!/usr/bin/env python3
"""Runs a game headless with a key script and reports what happened: fatals, what the runtime didn't do, speed, audio, and a
contact sheet of pictures taken during the run.

usage: session.py GAME NAME 'KEYS' SECONDS [--every S] [--at S,S,...] [--out DIR]
  GAME  file1 or file2          NAME  the session's name (its files go in DIR/NAME.*)
  KEYS  --key's script: "S:KEY[+KEY][:HOLD],..." (key names as bindings.txt has them: Escape, E, W, Left Shift...)
Prints one summary line block; the picture sheet is DIR/NAME.png and the log DIR/NAME.log.
"""
import array
import math
import re
import subprocess
import sys
import wave
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent


def main():
    a = sys.argv[1:]
    game, name, keys, seconds = a[0], a[1], a[2], float(a[3])
    rest = [x for x in a[4:] if x != "--keep-card"]
    opt = {rest[i]: rest[i + 1] for i in range(0, len(rest) - 1, 2)}
    out = Path(opt.get("--out", "/tmp/biohazard-sessions"))
    out.mkdir(parents=True, exist_ok=True)
    shots = out / name
    for f in shots.glob("*.png") if shots.exists() else []:
        f.unlink()
    shots.mkdir(exist_ok=True)
    wav = out / (name + ".wav")
    # each session has a memory card and settings of its own (sessions may run side by side)
    card = out / (name + "-state")
    card.mkdir(exist_ok=True)
    if "--keep-card" not in a:
        for f in card.glob("*"):
            if f.is_file():
                f.unlink()
    cmd = [str(ROOT / "run.sh"), game, "--headless", "--run-seconds", str(seconds), "--key", keys, "--fps",
           "--memcards", str(card), "--settings", str(card),
           "--snapshots", str(shots), "--audio-dump", str(wav)]
    if "--at" in opt:
        cmd += ["--snapshot-at", opt["--at"]]
    else:
        cmd += ["--snapshot-every", opt.get("--every", "20")]
    log = out / (name + ".log")
    with open(log, "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT)
    text = log.read_text(errors="replace")
    fatals = re.findall(r"fatal: .*", text)
    notdone = sorted(set(re.findall(r"\[ps2\] (.*not done.*)", text)))
    frames = [int(m) for m in re.findall(r": (\d+) frames, \d+ vblanks", text)]
    unknown = sorted(set(re.findall(r"\[ps2\] ((?:IOP|CRI|mc2|ds2|libsd|spu2)[^\n]*(?:unknown|unsupported|ignored|isn't done)[^\n]*)", text)))
    print(f"== {game} {name}: {seconds:.0f} s")
    print("fatal:", fatals[0] if fatals else "none")
    if frames:
        s = sorted(frames)
        print(f"speed: frames/s median {s[len(s)//2]}, min {s[0]}, max {s[-1]} over {len(s)} s")
    if wav.exists():
        w = wave.open(str(wav))
        d = array.array("h")
        d.frombytes(w.readframes(w.getnframes()))
        rate = w.getframerate() * w.getnchannels()
        peaks = [max((abs(x) for x in d[i:i + rate * 10]), default=0) for i in range(0, len(d), rate * 10)]
        rms = math.sqrt(sum(x * x for x in d) / max(1, len(d)))
        print(f"audio: {len(d)/rate:.0f} s, rms {rms:.0f}, peak per 10 s {peaks[:20]}")
    for line in notdone[:12]:
        print("not done:", line[:150])
    for line in unknown[:8]:
        print("note:", line[:150])
    pics = sorted(shots.glob("*.png"))
    if pics:
        W, H = 320, 280
        cols = 5
        rows = (len(pics) + cols - 1) // cols
        sheet = Image.new("RGB", (W * cols, H * rows))
        for i, p in enumerate(pics):
            sheet.paste(Image.open(p).convert("RGB").resize((W, H)), ((i % cols) * W, (i // cols) * H))
        sheet.save(out / (name + ".png"))
        print("sheet:", out / (name + ".png"), len(pics), "pictures")


main()
