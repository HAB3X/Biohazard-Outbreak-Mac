# Making the ready-made apps

`tools/make_app.py --release` makes the apps to share: `Biohazard-Outbreak-File1-macOS.zip` and
`Biohazard-Outbreak-File2-macOS.zip`, each with one app. Why they're made this way: [adr/0001](adr/0001-english-patch-in-the-app.md).

## What the build Mac needs

- The build of both games (`cmake --build build`), configured with the runtime that has `--disc-check` (runtime commit
  10180a9 or later, branch `biohazard-release`). The runtime copy in this repository's `ps2-native-runtime/` must be brought
  up to it before a release is made from it.
- The python.org Python 3.13 (`/Library/Frameworks/Python.framework/Versions/3.13`) with Pillow and NumPy installed in it:
  the app's Python is copied from it (`tools/app_python.py`).
- Homebrew's SDL3 (copied into the apps with its licence).
- Both disc images: each app is checked by a first launch with its disc.

## Making them

```sh
python3 tools/make_app.py file1 --release --disc "/path/to/file1.iso"
python3 tools/make_app.py file2 --release --disc "/path/to/file2.iso"
```

(`--out DIR` puts them elsewhere; the default is `build/dist`.) Each one:

1. Copies the program and SDL3 into the app (`Contents/Frameworks`, `@rpath`, the build's rpaths taken out), the launcher
   (`tools/app_launcher.sh`) as its executable, the trimmed Python with Pillow and NumPy (`Contents/Resources/python`), the
   English patch builder's files (`Contents/Resources/english-patch`), the licences (`Contents/Resources/licenses`) and an
   icon drawn from the game's name (the dev apps' icon is the game's title logo: not for sharing).
2. Signs it ad hoc and checks that no Mach-O in it loads anything outside the system or the app.
3. Launches it as a first launch would (a new settings folder, `PATH=/usr/bin:/bin`, headless for 3 seconds): the English
   patch it builds must equal byte for byte the one `tools/bh_build_patch.py` makes here from the same disc and bindings.
4. Zips it with `ditto` (keeping the signature) and prints the sizes.

## Checking them as a player

On the same Mac, from the zips (not the folder they were made in):

- Unzip into a new folder and launch with a new settings folder, e.g.
  `open -n -a "DIR/Biohazard Outbreak File 2.app" --args --settings /tmp/new --headless --snapshots /tmp/shots --snapshot-at 30,45`
  (make `/tmp/shots` first). The file dialog asks for the disc; a wrong disc is said and forgotten; then the patch's window,
  then the game.
- `otool -L` on `Contents/MacOS/biohazard-file*` and `Contents/Frameworks/*`: only `/usr/lib`, `/System` and `@rpath`.
- The first launch from Finder needs System Settings > Privacy & Security > Open Anyway (ad hoc signature); a disc image in
  Documents, Desktop or Downloads gets macOS's folder access question.

## Release notes (a starting point)

```
Biohazard Outbreak File 1 and File 2 for Mac 0.1.0

The Japanese PS2 releases of Biohazard Outbreak (SLPM-65428) and Biohazard Outbreak File 2 (SLPM-65692, v1.03), recompiled
to native Mac code: in English, with keyboard and mouse controls in the style of the PC Resident Evil 4 and 5, and online on
obsrv.org with your own forum account.

You need an Apple-silicon Mac with macOS 26 or later and your own disc image of the Japanese release. Unzip, open the app
(the first time: System Settings > Privacy & Security > Open Anyway), choose your disc image, and wait while the English patch
is made from it (less than a minute). The apps contain the games' recompiled code but none of the discs' data.

Bundled: Python 3.13 (PSF License), Pillow (MIT-CMU) and NumPy (BSD), which make the English patch on your Mac; SDL 3 (zlib);
the fonts Luckiest Guy (Apache 2.0) and Fredoka (SIL OFL 1.1); stb_truetype (public domain). Their licences are in each app's
Contents/Resources/licenses.

Known issues: docs/PLAYING.md.
```
