# 1. The release apps build the English patch on the player's Mac, with a Python they carry

Date: 2026-10-20. Status: accepted.

## Context

The apps are shared ready-made (as the Wrath of Cortex port's are): a player with an Apple-silicon Mac and their own Japanese
disc image unzips one and plays, with nothing installed. They contain the games' recompiled code, made from the executable,
and none of the discs' data.

The English patch (`tools/bh_build_patch.py`, docs/translation.md) can't ship in the app: it holds the game's own text tables
rebuilt in English and its pictures redrawn (the disc's art with English drawn over it), which would be distributing
Capcom's data. It has to be made from the player's disc, on their Mac, before the game starts. The builder is about 2,000
lines of Python using Pillow (drawing, fonts) and NumPy (the pictures' pixels), reading macOS's own fonts. It takes about 20
seconds on an M4. It's still being changed (the pictures' boxes and key caps), and its key caps are drawn from the player's
`bindings.txt`, so a change of keys needs the patch made again.

## Decision

The app carries a Python 3.13 with Pillow and NumPy in `Contents/Resources/python` (`tools/app_python.py`): a trimmed copy of
the python.org framework installed on the build Mac (`/Library/Frameworks/Python.framework/Versions/3.13`) and of its
Pillow and NumPy wheels, every Mach-O thinned to arm64, its install names and rpaths made relative (`@rpath`,
`@loader_path`), signed ad hoc, the standard library and packages compiled to `.pyc` (unchecked hashes) so nothing is ever
written into the signed app. The builder's own files (its modules, the translations, `config/`, the runtime's `disc.py` and
fonts) are in `Contents/Resources/english-patch`, in the repository's layout, so the same code runs in the repository and in
the app.

The app's executable is a shell script (`tools/app_launcher.sh`): the game's `--disc-check` (runtime) finds the disc (asking
with the file dialog the first time), checks it, writes `bindings.txt`'s defaults, and prints the disc and the settings
folder; the script builds `english.patch` in the settings folder when there's none, when the app or the disc changed
(`english.patch.made-from`), or when `bindings.txt` is newer than it; then it starts the game with `--patch`. The builder runs
with a clean environment (`env -i`, `PYTHONHOME` the app's Python, `PATH=/usr/bin:/bin`), so nothing of the player's (or the
developer's) Python is used.

`make_app.py --release` checks each app by launching it as a first launch (a new settings folder, only the system's PATH):
the patch it builds must equal, byte for byte, the one the repository's builder makes from the same disc and bindings.

## Options not taken

- **Port the builder to C++ inside the game.** No Python at all, and a smaller app (about 60 MB less unzipped, 20 MB zipped).
  Rejected for now: about 2,000 lines of image work (TIM2 and the compressed picture streams, inpainting, text fitted and
  drawn with FreeType's metrics, the font's glyphs), all to be rewritten and kept in step with the Python that the
  translation work goes on changing; the Python is the tested one, and two builders would drift. Worth revisiting once the
  pictures are final: a port would be checked against the Python's patches byte for byte.
- **Download a relocatable Python at first launch** (python-build-standalone, pip for Pillow and NumPy). Rejected: the app
  must work offline, and code fetched at run time is a supply-chain risk the player can't see.
- **python-build-standalone bundled at make time.** Equivalent to the decision but needs a download on the build Mac; the
  python.org framework already installed there is the same CPython, relocatable with `install_name_tool`.
- **Use the Mac's own Python** (`/usr/bin/python3`). It exists only once the Command Line Tools are installed (it otherwise
  asks to install them) and has no Pillow or NumPy.
- **Build the patch inside the game's process** (embedding libpython in the game). More coupling for no gain: the patch is
  built once, before the game starts, and a separate process keeps a builder failure from touching the game.
- **Ship the patch, or a diff against the disc.** Any form of it carries the game's text and pictures: rejected (see Context).

## Consequences

- The zips are about 33 MB (the game, SDL3, and about 60 MB unzipped of Python, Pillow and NumPy: 8 MB of it Pillow's image
  libraries). Their licences are in `Contents/Resources/licenses`.
- The build Mac needs the python.org Python 3.13 with Pillow and NumPy installed (`tools/app_python.py` says so if not).
- The builder may only use what the app's Python has: the standard library less its tests, Tk, curses and SSL, plus Pillow
  and NumPy. A new import of something else fails the release check (`make_app.py --release`), not the player.
- The builder reads macOS's own fonts (`/System/Library/Fonts`, on every Mac). A font macOS downloads on demand would not
  be there: only fonts in `/System/Library/Fonts` may be used.
- On the first launch macOS may ask whether the app may access the folder the disc image is in (Documents, Desktop,
  Downloads): the builder is a separate process, and the game reads the disc again on later launches without the file
  dialog. One "Allow" covers both.
