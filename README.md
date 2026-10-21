# Biohazard Outbreak for Mac

Resident Evil Outbreak File 1 and File 2 (the Japanese releases, Biohazard Outbreak) running natively on macOS. The PS2 code is
recompiled to native code, not emulated. Both games are playable in English, and they connect to the revived
[obsrv.org](https://obsrv.org) servers.

Not affiliated with Capcom or Sony. You need your own discs; no game data is included.

![File 2's title menu](milestones/file2-main-menu.png)
![Kevin in J's Bar](milestones/file2-in-game.png)

## Features

- Both games in one launcher, in English (the patch is made from your disc on first play)
- Metal renderer, 4x resolution by default, 30 fps
- Keyboard and mouse like the PC RE4/RE5, rebindable; controllers with PlayStation or Xbox prompts
- Snippy's Outbreak ModernCam (over the shoulder), first person and cheats, toggled in the F10 menu
- PCSX2 HD texture packs: drop them on the launcher
- Optional improved partner AI for offline play
- Online on obsrv.org: login and lobby work, online rooms are still being worked on

## Playing

You need an Apple-silicon Mac on macOS 26 or later and a disc image of the Japanese File 1 (SLPM-65428) and/or File 2
(SLPM-65692, v1.03).

1. Download `Biohazard-Outbreak-Launcher-macOS.zip` from [Releases](https://github.com/HAB3X/Biohazard-Outbreak-Mac/releases).
2. Open it. The first time, macOS blocks it: System Settings › Privacy & Security › Open Anyway.
3. The launcher finds your disc images in Downloads, Documents or the Desktop, or drop them on its window.
4. Press Play.

More in [docs/PLAYING.md](docs/PLAYING.md), including known issues.

## Building

```sh
brew install cmake ninja sdl3
python3 -m pip install pillow numpy
cmake -S . -B build -G Ninja -DFILE1_DISC=/path/to/file1.iso -DFILE2_DISC=/path/to/file2.iso
cmake --build build
./run.sh file2 --english
python3 tools/make_app.py both && python3 tools/make_app.py launcher
```

The first build recompiles the games, which takes a few minutes. [docs/RELEASING.md](docs/RELEASING.md) covers the release
apps; [STATUS.md](STATUS.md) has the details of what works.

## Credits

- Snippy: Outbreak ModernCam
- RaccoonRecomp: REO-ModernCam, First Person, the cheat research, launcher and control ideas
- Cheat codes: Codejunkies, Code Master, Lajos Szalay, Jay007, VirusPunk, Jarnold83, MadCatz, bungholio, 47iscool
- [reo](https://github.com/sp00nznet/reo) by sp00nznet and [REOF2](https://github.com/caprado/REOF2) by caprado, used as
  references
- [obsrv.org](https://obsrv.org) for keeping Outbreak online

Full list in [docs/CREDITS.md](docs/CREDITS.md).

## License

No licence chosen yet, so all rights are reserved for now. Bundled third-party parts keep their own licences (see
`ps2-native-runtime/runtime/third_party/` and each app's `Contents/Resources/licenses`).

Biohazard and Resident Evil are trademarks of Capcom. PlayStation is a trademark of Sony Interactive Entertainment.
