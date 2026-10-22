# Playing

## You need

- An Apple-silicon Mac (M1 or later) on macOS 26 or later
- A disc image (`.iso`, or `.cue` + `.bin`) of your own Japanese copy:
  - File 1: SLPM-65428
  - File 2: SLPM-65692, v1.03

Other versions (USA, Europe) aren't supported. obsrv.org only accepts the Japanese discs.

## First run

1. Unzip `Biohazard-Outbreak-Launcher-macOS.zip` and move the launcher anywhere (Applications is fine).
2. Open it. macOS blocks it the first time: System Settings › Privacy & Security › Open Anyway.
3. The launcher looks for your disc images in Downloads, Documents and the Desktop. Or drop them on its window.
4. Press Play. The English patch is made from your disc the first time (under a minute).

If macOS asks whether the app may access a folder, click Allow: that's where your disc image is.

## The launcher

- **Settings** (slider icon): the same settings as the in-game F10 menu, plus Saves.
- **Mods** (puzzle icon): drop in HD texture packs and character mods, switch them on or off.
- **Saves**: profiles, portable saves, and backups you can put back.
- **Discs**: change or add a disc image.

## Controls

| Key | In play | In menus |
|---|---|---|
| W A S D | move | move |
| Right mouse (hold) | ready weapon, aim | confirm |
| Left mouse | attack, fire | click |
| E, Return | action, pick up | confirm |
| Left Shift (hold) | run | back |
| R | reload | |
| Space | ad-lib | |
| M | map | |
| 1–4 (F to call a partner) | appeals | |
| Tab | status screen | |
| Escape | pause menu | back |
| F10 | settings | |
| F8 | camera settings while playing | |
| C / V / middle mouse | recenter / alt view / swap shoulder (Modern camera) | |

Everything can be rebound in F10 › Controls. Controllers work as a PS2 pad.

## Online

Make an account on [obsrv.org](https://obsrv.org) first. In the game: NETWORK PLAY, let it make a NET file, then log in with
your forum ID and password. Details: [network.md](network.md).

## Where your files are

`~/Library/Application Support/PS2 Native Runtime/SLPM_654.28` (File 1) or `SLPM_656.92` (File 2). In Finder: Go › Go to
Folder.

| What | File |
|---|---|
| Saves | `memcards/port0.ps2` (backups in `memcards/backups`) |
| Settings | `options.txt`, `graphics.txt`, `bindings.txt` |
| Logs | `logs/latest.log`, `logs/previous.log` |

## If something goes wrong

Send `logs/latest.log` with your [bug report](https://github.com/HAB3X/Biohazard-Outbreak-Mac/issues/new/choose). After a
crash and a restart, the crash is in `logs/previous.log`. Known bugs: [KNOWN BUGS.md](../KNOWN%20BUGS.md).

## Licences

Biohazard Outbreak belongs to Capcom. The apps include Python, Pillow, NumPy, SDL 3, Luckiest Guy, Fredoka and stb_truetype;
their licences are in each app's `Contents/Resources/licenses`.
