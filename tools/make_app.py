#!/usr/bin/env python3
"""Makes a double-clickable Mac app of a built game: Biohazard Outbreak (file1) or File 2 (file2), with an icon, on the Desktop
(or where --out says). The app plays in English and needs nothing else to start from Finder: it holds a copy of the build's
program, SDL3 (so it doesn't depend on this folder or Homebrew at run time), the English patch built here from your disc and
translations/ (tools/bh_build_patch.py), and the path of the disc image from local.json (or --disc); it's signed ad hoc as
Apple silicon needs.

    python3 tools/make_app.py file1|file2|both [--out DIR] [--disc PATH]

Run it again after a rebuild, a change to the translations, or a change of keys in the PC menus' Controls: the English
patch's key caps (the pictures' button prompts and the font's button glyphs) are drawn with the keys bound when the app is
made. The patch isn't rebuilt when the app starts: that needs this folder's tools and Python with Pillow and numpy (a Finder
launch has neither on its PATH) and takes about a minute.

The app keeps its settings, key bindings and saves where run.sh's runs do (~/Library/Application Support/PS2 Native
Runtime/<the game's executable>): the runtime's folder doesn't depend on how the game was started.

The icon is the game's own title logo (tools/make_icon.py): the first run starts the game headless to take a picture of its
title screen into work/ (ignored by git), and the icon is made from it.
"""
import argparse
import json
import os
import plistlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import make_icon  # noqa: E402

GAMES = {
    "file1": {"name": "Biohazard Outbreak", "id": "org.biohazardoutbreak.mac.file1", "exe": "biohazard-file1"},
    "file2": {"name": "Biohazard Outbreak File 2", "id": "org.biohazardoutbreak.mac.file2", "exe": "biohazard-file2"},
}


def run(*cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def sdl_library(binary):
    for line in run("otool", "-L", str(binary)).splitlines()[1:]:
        path = line.strip().split(" (compatibility")[0]
        if "libSDL3" in path:
            return Path(path).resolve(), path
    sys.exit(f"{binary}: no SDL3 in its libraries")


def title_picture(game, disc):
    """A picture of the game's title screen (work/title-GAME.png), taken by running the game headless if there isn't one."""
    picture = ROOT / "work" / f"title-{game}.png"
    if picture.exists():
        return picture
    shots = ROOT / "work" / f"title-{game}-shots"
    state = ROOT / "work" / f"title-{game}-state"
    for d in (shots, state):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)
    keys = ",".join(f"{t}:Escape:0.3" for t in (20, 27, 34, 41))
    subprocess.run([str(ROOT / "build" / GAMES[game]["exe"]), "--disc", str(disc), "--headless", "--run-seconds", "52",
                    "--key", keys, "--snapshot-at", "50", "--snapshots", str(shots), "--memcards", str(state),
                    "--settings", str(state)], check=True, capture_output=True)
    taken = sorted(shots.glob("*.png"))
    if not taken:
        sys.exit(f"{game}: the title screen wasn't photographed")
    taken[-1].rename(picture)
    return picture


def make(game, out, disc):
    g = GAMES[game]
    binary = ROOT / "build" / g["exe"]
    if not binary.exists():
        sys.exit(f"{binary}: not built (cmake --build build)")
    app = out / f"{g['name']}.app"
    if app.exists():
        shutil.rmtree(app)
    macos, frameworks, resources = app / "Contents/MacOS", app / "Contents/Frameworks", app / "Contents/Resources"
    for d in (macos, frameworks, resources):
        d.mkdir(parents=True)
    shutil.copy2(binary, macos / g["exe"])
    lib, reference = sdl_library(binary)
    shutil.copy2(lib, frameworks / lib.name)
    run("install_name_tool", "-id", f"@rpath/{lib.name}", str(frameworks / lib.name))
    run("install_name_tool", "-change", reference, f"@rpath/{lib.name}", str(macos / g["exe"]))
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(macos / g["exe"]))
    (resources / "disc.txt").write_text(str(disc) + "\n")
    # The English, from this disc and the translations as they are now (the keys of the settings folder's bindings.txt)
    subprocess.run([sys.executable, str(ROOT / "tools" / "bh_build_patch.py"), game, str(resources / "english.patch")],
                   check=True, stdout=subprocess.DEVNULL, env={**os.environ, f"{game.upper()}_DISC": str(disc)})
    # The app's own executable asks nothing: it starts the game with the disc the build was given, in English. Options given
    # to the app (open -a APP --args ...) are passed on; macOS's old -psn_ argument isn't an option of the game's
    launcher = macos / "Biohazard"
    launcher.write_text(f"""#!/bin/sh
DIR="$(cd "$(dirname "$0")" && pwd)"
DISC="$(cat "$DIR/../Resources/disc.txt")"
if [ ! -f "$DISC" ]; then
    osascript -e 'display alert "Disc image not found" message "Put the disc image back where it was, or run tools/make_app.py again with --disc."' >/dev/null
    exit 1
fi
for a in "$@"; do
    shift
    case "$a" in -psn_*) ;; *) set -- "$@" "$a" ;; esac
done
exec "$DIR/{g['exe']}" --disc "$DISC" --patch "$DIR/../Resources/english.patch" "$@"
""")
    launcher.chmod(0o755)
    make_icon.write_icns(title_picture(game, disc), game, resources / "AppIcon.icns")
    plist = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": "Biohazard",
        "CFBundleIconFile": "AppIcon",
        "CFBundleIdentifier": g["id"],
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": g["name"],
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "0.1.0",
        "CFBundleVersion": "0.1.0",
        "LSApplicationCategoryType": "public.app-category.action-games",
        "NSHighResolutionCapable": True,
        "GCSupportsControllerUserInteraction": True,
    }
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
    run("codesign", "--force", "--deep", "-s", "-", str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    # Nothing outside the system's libraries is left to load
    for line in run("otool", "-L", str(macos / g["exe"])).splitlines()[1:]:
        path = line.strip().split(" (compatibility")[0]
        if not path.startswith(("/usr/lib/", "/System/", "@rpath/")):
            sys.exit(f"{path}: still loaded from outside the app")
    run("touch", str(app))
    print(f"{app}")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("game", choices=["file1", "file2", "both"])
    p.add_argument("--out", default=str(Path.home() / "Desktop"))
    p.add_argument("--disc")
    a = p.parse_args()
    local = json.loads((ROOT / "local.json").read_text())
    for game in ("file1", "file2") if a.game == "both" else (a.game,):
        disc = Path(a.disc or local[game])
        if not disc.exists():
            sys.exit(f"{disc}: no such disc image")
        make(game, Path(a.out), disc)


main()
