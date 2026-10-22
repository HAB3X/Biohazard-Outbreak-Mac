#!/usr/bin/env python3
"""Makes a double-clickable Mac app of a built game: Biohazard Outbreak (file1) or File 2 (file2). Both kinds hold a copy of
the build's program and SDL3 (each non-system library in Contents/Frameworks: nothing is loaded from Homebrew) and are signed
ad hoc, as Apple silicon needs.

    python3 tools/make_app.py file1|file2|both [--out DIR] [--disc PATH]
    python3 tools/make_app.py file1|file2|both --release [--out DIR] [--disc PATH]

For this Mac (the default, on the Desktop): the app starts the game with the disc image from local.json (or --disc) and the
English patch built here from that disc and translations/ (tools/bh_build_patch.py), with the game's title logo as its icon.
Make it again after a rebuild, a change to the translations, or a change of keys in the PC menus' Controls: its patch's key
caps are the keys bound when it's made.

--release: the app to share (docs/RELEASING.md), with none of the disc's data in it. On its first launch it asks for the
player's disc image and checks it (the game's --disc-check), then builds the English patch from it into the settings folder
with the Python, Pillow and NumPy it carries (tools/app_python.py) and the builder's files (Resources/english-patch); it builds
it again when the app or the disc changes, or the keys do (bindings.txt newer than the patch: tools/app_launcher.sh). Its icon
is drawn, not the game's. Each app is checked by launching it as a player would (with --disc's or local.json's disc, headless)
and zipped as Biohazard-Outbreak-File1-macOS.zip / -File2- in --out (default build/dist).

Both keep their settings, key bindings and saves where run.sh's runs do (~/Library/Application Support/PS2 Native
Runtime/<the game's executable>): the runtime's folder doesn't depend on how the game was started. Options can be passed for
a test run: open -a "Biohazard Outbreak" --args --run-seconds 60 --snapshots DIR --snapshot-at 50.
"""
import argparse
import hashlib
import json
import modulefinder
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import app_python  # noqa: E402
import bh_archive  # noqa: E402
import make_icon  # noqa: E402

GAMES = {
    "file1": {"name": "Biohazard Outbreak", "id": "org.biohazardoutbreak.mac.file1", "exe": "biohazard-file1",
              "code": "SLPM_654.28", "zip": "Biohazard-Outbreak-File1-macOS.zip"},
    "file2": {"name": "Biohazard Outbreak File 2", "id": "org.biohazardoutbreak.mac.file2", "exe": "biohazard-file2",
              "code": "SLPM_656.92", "zip": "Biohazard-Outbreak-File2-macOS.zip"},
}
VERSION = "0.1.0"
SYSTEM = ("/usr/lib/", "/System/")
RELEASE_OUT = ROOT / "build" / "dist"


def run(*cmd, **kwargs):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kwargs).stdout


def references(binary):
    """The libraries a Mach-O loads (otool -L), its own install name left out."""
    own = run("otool", "-D", str(binary)).splitlines()[1:]
    lines = run("otool", "-L", str(binary)).splitlines()[1:]
    return [p for p in (line.strip().split(" (compatibility")[0] for line in lines) if p not in own]


def runtime_dir():
    return bh_archive.runtime_tools().parent


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


def bundle(game, out, launcher):
    """The app's skeleton: the program in Contents/MacOS with each non-system library it loads in Contents/Frameworks (its
    install name @rpath/NAME), the launcher as the app's executable, the Info.plist. Returns (app, [the libraries' files])."""
    g = GAMES[game]
    binary = ROOT / "build" / g["exe"]
    if not binary.exists():
        sys.exit(f"{binary}: not built (cmake --build build)")
    app = out / f"{g['name']}.app"
    if app.exists():
        shutil.rmtree(app)
    macos, frameworks = app / "Contents/MacOS", app / "Contents/Frameworks"
    for d in (macos, frameworks, app / "Contents/Resources"):
        d.mkdir(parents=True)
    program = macos / g["exe"]
    shutil.copy2(binary, program)
    libraries = []
    for reference in references(program):
        if reference.startswith(SYSTEM):
            continue
        library = Path(reference).resolve()
        if not library.exists():
            sys.exit(f"{binary}: can't find {reference}")
        if any(not r.startswith(SYSTEM) for r in references(library)):
            sys.exit(f"{library}: loads other non-system libraries ({references(library)}): bundle those too")
        shutil.copy2(library, frameworks / library.name)
        run("install_name_tool", "-id", f"@rpath/{library.name}", str(frameworks / library.name))
        run("install_name_tool", "-change", reference, f"@rpath/{library.name}", str(program))
        libraries.append(library)
    # Its libraries from the app only: a build's rpaths (Homebrew's lib) would be searched first
    for rpath in app_python.rpaths(program):
        run("install_name_tool", "-delete_rpath", rpath, str(program))
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(program))
    (macos / "Biohazard").write_text(launcher)
    (macos / "Biohazard").chmod(0o755)
    plist = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": "Biohazard",
        "CFBundleIconFile": "AppIcon",
        "CFBundleIdentifier": g["id"],
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": g["name"],
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": VERSION,
        "CFBundleVersion": VERSION,
        "LSApplicationCategoryType": "public.app-category.action-games",
        "LSMinimumSystemVersion": minimum_macos(binary),
        "NSHighResolutionCapable": True,
        "GCSupportsControllerUserInteraction": True,
    }
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
    return app, libraries


def minimum_macos(binary):
    """The macOS the program was built for at least (its LC_BUILD_VERSION's minos)."""
    for line in run("otool", "-l", str(binary)).splitlines():
        if line.strip().startswith("minos "):
            return line.split()[1]
    sys.exit(f"{binary}: no minimum macOS in its load commands")


def sign_and_check(app):
    """Signed ad hoc (each library, then the bundle), the signature verified, and no Mach-O in the app loading anything from
    outside the system or the app."""
    for library in (app / "Contents/Frameworks").iterdir():
        run("codesign", "--force", "--sign", "-", str(library))
    run("codesign", "--force", "--sign", "-", str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    outside = []
    for path in app.rglob("*"):
        if path.is_file() and not path.is_symlink() and app_python.is_macho(path):
            outside += [f"{path.relative_to(app)}: {r}" for r in references(path)
                        if not r.startswith(SYSTEM + ("@rpath/", "@loader_path/"))]
            outside += [f"{path.relative_to(app)}: rpath {r}" for r in app_python.rpaths(path) if not r.startswith("@")]
    if outside:
        sys.exit("still loaded from outside the app:\n" + "\n".join(outside))
    run("touch", str(app))


def make(game, out, disc):
    """The app for this Mac: this disc, the English patch built now."""
    g = GAMES[game]
    app, _ = bundle(game, out, f"""#!/bin/sh
DIR="$(cd "$(dirname "$0")" && pwd)"
DISC="$(cat "$DIR/../Resources/disc.txt")"
if [ ! -f "$DISC" ]; then
    osascript -e 'display alert "Disc image not found" message "Put the disc image back where it was, or run tools/make_app.py again with --disc."' >/dev/null
    exit 1
fi
for a in "$@"; do
    shift
    case "$a" in -psn_*) ;; --prepare) exit 0 ;; *) set -- "$@" "$a" ;; esac
done
# The launcher's character mods, as tools/app_launcher.sh lays them (from this repository's tools)
SETTINGS="$HOME/Library/Application Support/PS2 Native Runtime/{g['code']}"
if grep -q '^[^#]' "$SETTINGS/characters.txt" 2>/dev/null; then
    if "{sys.executable}" "{ROOT / 'tools/bh_character_patch.py'}" {game} "$SETTINGS/characters.txt" "$SETTINGS/characters.patch" \
        >"$SETTINGS/characters-patch.log" 2>&1; then
        set -- --patch "$SETTINGS/characters.patch" "$@"
    else
        osascript -e "display alert \"The character mods couldn't be put in\" message \"$(tail -n 2 "$SETTINGS/characters-patch.log")\"" >/dev/null
    fi
fi
exec "$DIR/{g['exe']}" --disc "$DISC" --patch "$DIR/../Resources/english.patch" "$@"
""")
    resources = app / "Contents/Resources"
    (resources / "disc.txt").write_text(str(disc) + "\n")
    # The English, from this disc and the translations as they are now (the keys of the settings folder's bindings.txt)
    subprocess.run([sys.executable, str(ROOT / "tools" / "bh_build_patch.py"), game, str(resources / "english.patch")],
                   check=True, stdout=subprocess.DEVNULL, env={**os.environ, f"{game.upper()}_DISC": str(disc)})
    make_icon.write_icns(make_icon.draw(title_picture(game, disc), game), resources / "AppIcon.icns")
    sign_and_check(app)
    # The disc remembered in the game's settings folder too, as a release app's first launch does: the launcher finds it there
    run(str(app / "Contents/MacOS" / g["exe"]), "--disc", str(disc), "--disc-check", "--headless")
    print(app)


# --- the release ----------------------------------------------------------------------------------------------------------

def builder_files(game):
    """{path in Resources/english-patch: the file}: what tools/bh_build_patch.py reads to build GAME's patch, in the
    repository's layout (the builder finds them from its own folder; the runtime's part from $PS2_RUNTIME_DIR)."""
    files = {}
    # (and the character patch's, tools/app_launcher.sh's: a finder each, as each script is its __main__)
    for script in ("bh_build_patch.py", "bh_character_patch.py"):
        finder = modulefinder.ModuleFinder(path=[str(ROOT / "tools"), str(runtime_dir() / "tools")])
        finder.run_script(str(ROOT / "tools" / script))
        for module in finder.modules.values():
            source = Path(module.__file__ or "")
            if source.parent == ROOT / "tools":
                files[f"tools/{source.name}"] = source
            elif source.parent == runtime_dir() / "tools":
                files[f"ps2-native-runtime/tools/{source.name}"] = source
    if "ps2-native-runtime/tools/disc.py" not in files:
        sys.exit("bh_build_patch.py no longer imports the runtime's tools/disc.py: check builder_files")
    translate = tomllib.loads((ROOT / "config/translate.toml").read_text())
    for name in {game, translate[game].get("fallback", game)}:
        files.update({f"translations/{name}/{p.name}": p for p in (ROOT / "translations" / name).iterdir() if p.is_file()})
    files["translations/pictures-shared.toml"] = ROOT / "translations/pictures-shared.toml"
    for config in ("translate.toml", f"{game}.toml"):
        files[f"config/{config}"] = ROOT / "config" / config
    files["src/controls.cpp"] = ROOT / "src/controls.cpp"  # the controls' defaults: bh_pictures reads its table
    files.update({f"ps2-native-runtime/runtime/fonts/{p.name}": p for p in (runtime_dir() / "runtime/fonts").glob("*.ttf")})
    return files


def licences(resources, libraries, python_licences):
    """Contents/Resources/licenses: Python's, Pillow's, NumPy's, each bundled library's (its Homebrew package's LICENSE.txt),
    the fonts' and stb_truetype's."""
    folder = resources / "licenses"
    fonts = runtime_dir() / "runtime/fonts"
    found = list(python_licences)
    for library in libraries:  # /opt/homebrew/Cellar/sdl3/3.4.16/lib/libSDL3.0.dylib: the package is lib's parent
        package = library.parent.parent
        found.append((package / "LICENSE.txt", f"{package.parent.name}/LICENSE.txt"))
    found += [(fonts / "OFL-Fredoka.txt", "fonts/OFL-Fredoka.txt"),
              (fonts / "LICENSE-LuckiestGuy.txt", "fonts/LICENSE-LuckiestGuy.txt"),
              (fonts / "README.md", "fonts/README.md"),
              (runtime_dir() / "runtime/third_party/stb/LICENSE.txt", "stb/LICENSE.txt")]
    for source, name in found:
        if not source.exists():
            sys.exit(f"{source}: missing licence")
        (folder / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, folder / name)
    (folder / "README.txt").write_text(
        "The licences of what this app carries besides its own code:\n"
        "  python/  Python 3.13 (PSF License Agreement), which builds the English patch on your Mac\n"
        "  pillow/  Pillow (MIT-CMU), with the licences of the libraries in it (FreeType, HarfBuzz, libjpeg, libpng, ...)\n"
        "  numpy/   NumPy (BSD 3-Clause), with the licences of what it bundles\n"
        "  sdl3/    SDL 3 (zlib)\n"
        "  fonts/   Luckiest Guy (Apache 2.0) and Fredoka (SIL Open Font License 1.1): the PC menus' and the patch's pictures'\n"
        "  stb/     stb_truetype (public domain or MIT)\n")


def make_release(game, out, disc):
    """The app to share (no disc data in it), checked as a player would launch it, and zipped. Returns the zip."""
    g = GAMES[game]
    launcher = (ROOT / "tools/app_launcher.sh").read_text()
    for key, value in (("@GAME_UPPER@", game.upper()), ("@GAME@", game), ("@EXE@", g["exe"]), ("@NAME@", g["name"])):
        launcher = launcher.replace(key, value)
    app, libraries = bundle(game, out, launcher)
    resources = app / "Contents/Resources"
    python_licences = app_python.copy(resources / "python")
    builder = resources / "english-patch"
    digest = hashlib.sha256()
    for name, source in sorted(builder_files(game).items()):
        (builder / name).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, builder / name)
        digest.update(name.encode() + b"\0" + source.read_bytes())
    # The patch is made again when this changes: an app with other translations or tools
    (builder / "VERSION").write_text(f"{VERSION} {digest.hexdigest()[:16]}\n")
    run(str(resources / "python/bin/python3"), "-m", "compileall", "-q", "--invalidation-mode", "unchecked-hash",
        str(builder), env=app_python.environment(resources / "python"))
    licences(resources, libraries, python_licences)
    make_icon.write_icns(make_icon.draw_plain(game, runtime_dir() / "runtime/fonts/Fredoka-Bold.ttf"),
                         resources / "AppIcon.icns")
    sign_and_check(app)
    check_release(game, app, disc)
    archive = out / g["zip"]
    archive.unlink(missing_ok=True)
    run("ditto", "-c", "-k", "--keepParent", str(app), str(archive))
    print(f"{archive}: {archive.stat().st_size / 1e6:.1f} MB (the app {app_python.size(app) / 1e6:.0f} MB)")
    return archive


def check_release(game, app, disc):
    """The app launched as on a player's first launch (a new settings folder, only the system's PATH, headless for a few
    seconds) must build the same patch as this folder's builder does with the same bindings, and leave the app's signature
    good (nothing written into it)."""
    with tempfile.TemporaryDirectory(prefix="bh-release-check-") as tmp:
        settings = Path(tmp) / "settings"
        log = settings / "english-patch.log"
        result = subprocess.run([str(app / "Contents/MacOS/Biohazard"), "--disc", str(disc), "--settings", str(settings),
                                 "--headless", "--mute", "--run-seconds", "3"],
                                env={"PATH": "/usr/bin:/bin", "HOME": tmp}, capture_output=True, text=True)
        if result.returncode != 0 or not (settings / "english.patch").exists():
            sys.exit(f"{app.name}: the first launch failed ({result.returncode}):\n{result.stderr[-2000:]}\n"
                     f"{log.read_text() if log.exists() else ''}")
        here = Path(tmp) / "here.patch"
        subprocess.run([sys.executable, str(ROOT / "tools/bh_build_patch.py"), game, str(here)], check=True,
                       stdout=subprocess.DEVNULL,
                       env={**os.environ, f"{game.upper()}_DISC": str(disc), "BH_SETTINGS": str(settings)})
        if here.read_bytes() != (settings / "english.patch").read_bytes():
            sys.exit(f"{app.name}: its English patch differs from this folder's builder's")
        run("codesign", "--verify", "--deep", "--strict", str(app))
    print(f"{app.name}: first launch checked (its English patch is this folder's builder's, byte for byte)")


# --- the launcher ---------------------------------------------------------------------------------------------------------

LAUNCHER = {"name": "Biohazard Outbreak Launcher", "id": "org.biohazardoutbreak.mac.launcher",
            "zip": "Biohazard-Outbreak-Launcher-macOS.zip"}


def make_launcher(out, games, release):
    """The launcher (launcher/, SwiftUI) with both games' apps inside it (Contents/Resources/Games, from `games`: the
    Desktop's for this Mac, build/dist's for the release); the release's zipped."""
    run("swift", "build", "-c", "release", "--package-path", str(ROOT / "launcher"))
    binary = Path(run("swift", "build", "-c", "release", "--package-path", str(ROOT / "launcher"), "--show-bin-path").strip())
    binary = binary / "Launcher"
    apps = [games / f"{GAMES[g]['name']}.app" for g in GAMES]
    missing = [a for a in apps if not a.is_dir()]
    if missing:
        sys.exit(f"the games' apps aren't made yet: {', '.join(map(str, missing))}")
    app = out / f"{LAUNCHER['name']}.app"
    if app.exists():
        shutil.rmtree(app)
    (app / "Contents/MacOS").mkdir(parents=True)
    resources = app / "Contents/Resources"
    (resources / "Games").mkdir(parents=True)
    shutil.copy2(binary, app / "Contents/MacOS/Launcher")
    for game_app in apps:
        run("ditto", str(game_app), str(resources / "Games" / game_app.name))
    make_icon.write_icns(make_icon.draw_plain("launcher", runtime_dir() / "runtime/fonts/Fredoka-Bold.ttf", "FILE 1+2"),
                         resources / "AppIcon.icns")
    plist = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleExecutable": "Launcher",
        "CFBundleIconFile": "AppIcon",
        "CFBundleIdentifier": LAUNCHER["id"],
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "Biohazard Outbreak",
        "CFBundleDisplayName": LAUNCHER["name"],
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": VERSION,
        "CFBundleVersion": VERSION,
        "LSApplicationCategoryType": "public.app-category.action-games",
        "LSMinimumSystemVersion": "14.0",
        "NSHighResolutionCapable": True,
        # Disc images and mods can be opened with it (dropped on its icon): it takes them as a drop on its window
        "CFBundleDocumentTypes": [{"CFBundleTypeName": "Disc image or mod", "CFBundleTypeRole": "Viewer",
                                   "LSHandlerRank": "Alternate", "LSItemContentTypes": ["public.item", "public.folder"]}],
    }
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(plist))
    # The games inside are signed as they were made; the launcher's own program, then the whole
    run("codesign", "--force", "--sign", "-", str(app / "Contents/MacOS/Launcher"))
    run("codesign", "--force", "--sign", "-", str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    print(app)
    if release:
        archive = out / LAUNCHER["zip"]
        archive.unlink(missing_ok=True)
        run("ditto", "-c", "-k", "--keepParent", str(app), str(archive))
        print(f"{archive}: {archive.stat().st_size / 1e6:.1f} MB")


def main():
    p = argparse.ArgumentParser()
    p.add_argument("game", choices=["file1", "file2", "both", "launcher"])
    p.add_argument("--release", action="store_true", help="the apps to share, zipped (see the top of this file)")
    p.add_argument("--out")
    p.add_argument("--disc", help="the disc image (default local.json's); --release only checks the app with it")
    a = p.parse_args()
    out = Path(a.out or (RELEASE_OUT if a.release else Path.home() / "Desktop"))
    out.mkdir(parents=True, exist_ok=True)
    if a.game == "launcher":
        make_launcher(out, RELEASE_OUT if a.release else Path.home() / "Desktop", a.release)
        return
    local = json.loads((ROOT / "local.json").read_text()) if (ROOT / "local.json").exists() else {}
    for game in ("file1", "file2") if a.game == "both" else (a.game,):
        disc = Path(a.disc or local.get(game) or os.environ.get(f"{game.upper()}_DISC", ""))
        if not disc.is_file():
            sys.exit(f"{game}: no disc image (--disc, local.json or ${game.upper()}_DISC)")
        (make_release if a.release else make)(game, out, disc)


main()
