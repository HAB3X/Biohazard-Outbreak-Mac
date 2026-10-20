# Biohazard Outbreak for Mac: File 1 and File 2, recompiled

Biohazard Outbreak (File 1, SLPM-65428) and Biohazard Outbreak File 2 (SLPM-65692), the Japanese PlayStation 2 releases,
statically recompiled into native Mac programs. Every function of the games' R5900 code, and of the code overlays they load,
is turned into C ahead of time; a runtime stands in for the rest of the console. The games are played in English, with
keyboard and mouse controls in the style of the PC releases of Resident Evil 4 and 5, and online on the revived servers at
[obsrv.org](https://obsrv.org).

> This project isn't affiliated with or endorsed by Capcom or Sony. You need your own discs. This repository's source contains
> none of the games' code or data. The ready-made apps contain the games' code recompiled (made from the discs' executables)
> but none of the discs' data: the English patch is made on your Mac from your disc image the first time you play.

## Status

**Both games are playable from boot to their scenarios, in English, at 30 frames a second, with sound. Online play reaches
obsrv.org, logs in, runs the lobby and connects to the game server; play in an online room hasn't been tried yet.**

| Stage | State |
|---|---|
| Recompiling the executables and their overlays (File 2: 5,706 functions + 17,022 in nine overlays; File 1: 5,567 + 15,091 in eight) | Done |
| Boot, movies (Sofdec, decoded by the IPU), title, menus, selects | Done |
| Single play: scenarios load and play, the status screen, items, files | Done (full playthroughs not finished) |
| Sound: Capcom's IOP sound driver and CRI ADX, stood in natively | Done (not compared by ear with a console) |
| English: File 2 22,027 of 22,206 text units, File 1 12,621 of 13,299; pictures redrawn | Done (the rest kept Japanese on purpose: word filters, kana tables) |
| Keyboard and mouse controls, the game's own pause on Esc, prompts showing your keys | Done; **mouse in every menu and in the online browser not finished** |
| Speed: 30 frames a second in every scene measured | Done on an M4; slower Macs not measured yet |
| Online: network, DNAS, the KDDI browser, obsrv.org's login, lobby and game server | **Login and lobby work; rooms and games not tried yet** |
| Saves (a memory card image per game) | Done |
| A native Metal renderer replacing the GS's OpenGL path | **Being made**: drawing every scene tried, off unless chosen (`--gs metal`) |

Known issues: [docs/PLAYING.md](docs/PLAYING.md#known-issues).

## Screenshots

![File 2's title menu](milestones/file2-main-menu.png)
File 2's title menu (the HDD INSTALL item is gone from current builds: the Mac has no hard disk to install to).

![File 2, the Training Ground](milestones/file2-in-game.png)
File 2's Training Ground: Kevin in J's Bar.

![File 1, Outbreak](milestones/file1-in-game.png)
File 1's first scenario, Outbreak.

## Getting Started

### Playing (the ready-made apps)

Download `Biohazard-Outbreak-File1-macOS.zip` or `Biohazard-Outbreak-File2-macOS.zip` from the
[releases](https://github.com/HAB3X/Biohazard-Outbreak-Mac/releases) and read [docs/PLAYING.md](docs/PLAYING.md). You need
an Apple-silicon Mac with macOS 26 or later and your own disc image of the Japanese release; nothing else is installed. The
app asks for your disc image the first time, checks it, and makes the English patch from it (less than a minute).

### Building from source: quick start

```sh
brew install cmake ninja sdl3
python3 -m pip install pillow numpy
git clone https://github.com/HAB3X/Biohazard-Outbreak-Mac.git
cd Biohazard-Outbreak-Mac
cmake -S . -B build -G Ninja -DFILE1_DISC="/path/to/file1.iso" -DFILE2_DISC="/path/to/file2.iso"
cmake --build build
python3 tools/make_app.py both
```

This makes "Biohazard Outbreak.app" and "Biohazard Outbreak File 2.app" on your Desktop, in English, for your Mac.

### Step by step

What you need:

- **A Mac with Apple silicon** and Xcode's command line tools (`xcode-select --install`). [tested: macOS 26.6, M4]
- **The games**: disc images of the Japanese releases, File 1 (SLPM-65428) and/or File 2 (SLPM-65692, version 1.03), as
  `.iso` files. Only the Japanese discs are accepted by obsrv.org's servers.
- **CMake** 3.20 or later, **Ninja** and **SDL3**: `brew install cmake ninja sdl3`. [tested: CMake 4.0.1]
- **Python 3.11 or later** for the recompiler, and **Pillow** and **NumPy** for the English patch's pictures. [tested: 3.13]

1. Configure with one or both discs (each game is its own program):
   `cmake -S . -B build -G Ninja -DFILE2_DISC="/path/to/file2.iso"`
2. Build: `cmake --build build`. The first build recompiles the executables and overlays, which takes a few minutes.
3. Play: `./run.sh file2 --english` (or `file1`). The first run with `--english` builds `build/english-file2.patch` from
   your disc and the translations (about half a minute).
4. Or make the apps: `python3 tools/make_app.py both`. Make them again after a rebuild or after changing your keys.
5. The apps to share (no disc data in them, their own Python to make the English patch on the player's Mac):
   `python3 tools/make_app.py file2 --release --disc /path/to/file2.iso` ([docs/RELEASING.md](docs/RELEASING.md)).

Trip-ups:

- "Set FILE1_DISC and/or FILE2_DISC": CMake needs at least one disc image.
- `run.sh` finds your disc from `FILE1_DISC`/`FILE2_DISC` or from `local.json`: `{"file1": "/path", "file2": "/path"}`.
- A game window that loses focus pauses (the PC options' "pause in the background"): click into it to go on.

## Usage

```sh
./run.sh file2 --english            # File 2 in English
./run.sh file1 --english            # File 1 in English
./run.sh file2 --english --fps      # with the frame rate and each thread's CPU time in the log
```

Controls ([docs/controls.md](docs/controls.md) has them all; every key can be rebound in the PC options, F10):

| Key | Play | Menus |
|---|---|---|
| W A S D | move | move the cursor |
| Mouse | aim while the weapon is readied | point and click |
| Right mouse (hold) | ready the weapon | |
| E, Return | action, check, pick up | confirm |
| Left Shift (hold) | run | back |
| Esc | the game's own pause and status screen | back |

Online ([docs/network.md](docs/network.md)): from the title, NETWORK PLAY, confirm the caution, choose the third row of the
connection list (the Mac's network), wait for DNAS, then log in on obsrv.org's page with your obsrv.org forum account.

## Building from source

The repository's own code is what's specific to these games: the recompiler configurations (`config/`), the games' IOP
sound driver stand-in, the controls and online stand-ins (`src/`), and the English translation and patch tools
(`translations/`, `tools/`). `ps2-native-runtime/` is the shared runtime: the kernel, DMA, the IOP's modules, the SPU2, and
the GS, VIF and VU graphics hardware drawn on the GPU. [STATUS.md](STATUS.md) has the details of where everything stands.

## Documentation

- [docs/PLAYING.md](docs/PLAYING.md): playing with the ready-made apps
- [STATUS.md](STATUS.md): what works, what's being worked on
- [docs/controls.md](docs/controls.md): the keyboard and mouse controls, and how they follow the game
- [docs/translation.md](docs/translation.md): the English patch, its coverage and how it's built
- [docs/network.md](docs/network.md): online play, and what stands in for the games' network modules
- [docs/PERFORMANCE.md](docs/PERFORMANCE.md): how speed is measured, and what was changed
- [ps2-native-runtime/DESIGN.md](ps2-native-runtime/DESIGN.md): how the runtime is put together
- [docs/RELEASING.md](docs/RELEASING.md): making the ready-made apps; [docs/adr/](docs/adr/): decisions and why

## References

- [reo](https://github.com/sp00nznet/reo) by sp00nznet: a recompilation of the USA release of Biohazard Outbreak (MIT)
- [REOF2](https://github.com/caprado/REOF2) by caprado: research on File 2's Japanese release (read for reference only)
- [obsrv.org](https://obsrv.org): the revived Biohazard Outbreak servers
- The PS2 hardware documentation, and the PCSX2 and PS2SDK projects

## Contributors

HAB3X.

## License

No licence has been chosen for this repository's code yet; until one is, all rights are reserved. The runtime's third-party
parts keep their own licences (`ps2-native-runtime/runtime/third_party/`, `ps2-native-runtime/runtime/fonts/`). The
ready-made apps also carry Python (PSF License), Pillow (MIT-CMU), NumPy (BSD) and SDL 3 (zlib), whose licences are in each
app's `Contents/Resources/licenses`.

Biohazard and Resident Evil are trademarks of Capcom Co., Ltd. PlayStation is a trademark of Sony Interactive Entertainment.
