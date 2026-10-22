# Biohazard Outbreak for Mac

Resident Evil Outbreak File 1 and File 2 (the Japanese releases, Biohazard Outbreak) running natively on macOS. The PS2 code is
recompiled to native code, not emulated. Both games are playable in English, and they connect to the revived
[obsrv.org](https://obsrv.org) servers.

Not affiliated with Capcom or Sony. You need your own discs; no game data is included.

## Features

- Both games in one launcher, in English (the patch is made from your disc on first play)
- Metal renderer, 4x resolution by default, 30 fps
- Widescreen 16:9: a wider view, with the HUD and menus keeping their shape (switchable)
- Keyboard and mouse like the PC RE4/RE5, rebindable; controllers with PlayStation or Xbox prompts
- Snippy's Outbreak ModernCam (over the shoulder), first person and cheats, toggled in the F10 menu; F8 tunes the camera
  while you play, with recenter and an alt view
- PCSX2 HD texture packs and character mods: drop them on the launcher
- Brightness, gamma, colour-blind filters, Reduce Flashing, picture scaling, borderless or exclusive fullscreen
- Skip intro movies; save profiles, portable saves and automatic save backups
- Optional improved partner AI for offline play
- Online on obsrv.org: login, the lobby and joining rooms work

## Playing

You need an Apple-silicon Mac on macOS 26 or later and a disc image of the Japanese File 1 (SLPM-65428) and/or File 2
(SLPM-65692, v1.03).

1. Download `Biohazard-Outbreak-Launcher-macOS.zip` from [Releases](https://github.com/HAB3X/Biohazard-Outbreak-Mac/releases).
2. Open it. The first time, macOS blocks it: System Settings › Privacy & Security › Open Anyway.
3. The launcher finds your disc images in Downloads, Documents or the Desktop, or drop them on its window.
4. Press Play.

Known bugs: [KNOWN BUGS.md](KNOWN%20BUGS.md). Found another? [Report it](https://github.com/HAB3X/Biohazard-Outbreak-Mac/issues/new/choose).

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

[MIT](LICENSE). The camera and cheat code adapted from RaccoonRecomp's mods keeps their MIT notice; bundled third-party
parts keep their own licences.

Biohazard and Resident Evil are trademarks of Capcom. PlayStation is a trademark of Sony Interactive Entertainment.
