#!/usr/bin/env python3
"""Makes "Outbreak Studio.app" on the Desktop (or --out DIR): double-click it and the editor opens in your browser. It runs this checkout's tools/bh_studio.py, so it
needs no rebuilding after changes to the editor; move the checkout and make the app again.

    python3 tools/make_studio_app.py [--out DIR]
"""
import argparse
import plistlib
import shutil
import stat
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

LAUNCHER = """#!/bin/sh
# starts the editor (or just opens it when it is already running)
cd "{root}" || exit 1
if curl -s -o /dev/null http://127.0.0.1:8770/; then open http://127.0.0.1:8770/; exit 0; fi
exec {python} tools/bh_studio.py {game} {scenario}
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(Path.home() / "Desktop"))
    ap.add_argument("--game", default="file2")
    ap.add_argument("--scenario", default="40")
    args = ap.parse_args()
    app = Path(args.out) / "Outbreak Studio.app"
    if app.exists():
        shutil.rmtree(app)
    (app / "Contents" / "MacOS").mkdir(parents=True)
    launcher = app / "Contents" / "MacOS" / "studio"
    launcher.write_text(LAUNCHER.format(root=ROOT, python=sys.executable, game=args.game, scenario=args.scenario))
    launcher.chmod(launcher.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    (app / "Contents" / "Info.plist").write_bytes(plistlib.dumps({
        "CFBundleName": "Outbreak Studio", "CFBundleDisplayName": "Outbreak Studio", "CFBundleIdentifier": "local.outbreak.studio",
        "CFBundleExecutable": "studio", "CFBundlePackageType": "APPL", "CFBundleVersion": "1", "LSUIElement": False}))
    print(f"made {app}")


if __name__ == "__main__":
    main()
