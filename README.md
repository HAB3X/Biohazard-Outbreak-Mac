# Biohazard Outbreak for Mac

Resident Evil Outbreak File 1 and File 2 (the Japanese releases) running natively on Mac. Recompiled, not emulated. Both
games are in English and play online on [obsrv.org](https://obsrv.org).

Not affiliated with Capcom or Sony. You need your own discs; no game data is included.

## Features

- Both games in one launcher, in English
- Widescreen, 4x resolution, Metal renderer
- Keyboard and mouse like RE4/RE5 on PC, fully rebindable; PlayStation or Xbox controllers
- Snippy's ModernCam (over the shoulder), first person and cheats
- HD texture packs and character mods
- Brightness, gamma, colour-blind filters, reduce flashing
- Skip intro movies, save backups, save profiles
- Optional smarter partner AI
- Online: login, lobby and rooms

## Playing

You need an Apple-silicon Mac on macOS 26+ and a disc image of the Japanese File 1 (SLPM-65428) and/or File 2 (SLPM-65692
v1.03).

1. Download `Biohazard-Outbreak-Launcher-macOS.zip` from [Releases](https://github.com/HAB3X/Biohazard-Outbreak-Mac/releases).
2. Open it. The first time, go to System Settings › Privacy & Security › Open Anyway.
3. The launcher finds your disc images, or drop them on the window.
4. Press Play.

More help: [docs/PLAYING.md](docs/PLAYING.md). Bugs: [KNOWN BUGS.md](KNOWN%20BUGS.md) ·
[report one](https://github.com/HAB3X/Biohazard-Outbreak-Mac/issues/new/choose).

## Building

```sh
brew install cmake ninja sdl3
python3 -m pip install pillow numpy
cmake -S . -B build -G Ninja -DFILE1_DISC=/path/to/file1.iso -DFILE2_DISC=/path/to/file2.iso
cmake --build build
./run.sh file2 --english
```

## Credits

- Snippy: Outbreak ModernCam
- RaccoonRecomp: REO-ModernCam, First Person, cheats, launcher and control ideas
- Cheat codes: Codejunkies, Code Master, Lajos Szalay, Jay007, VirusPunk, Jarnold83, MadCatz, bungholio, 47iscool
- [reo](https://github.com/sp00nznet/reo) by sp00nznet and [REOF2](https://github.com/caprado/REOF2) by caprado (references)
- [obsrv.org](https://obsrv.org) for keeping Outbreak online

Full list: [docs/CREDITS.md](docs/CREDITS.md).

## License

[MIT](LICENSE). Biohazard and Resident Evil are trademarks of Capcom. PlayStation is a trademark of Sony Interactive
Entertainment.
