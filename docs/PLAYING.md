# Playing Biohazard Outbreak File 1 and File 2 on a Mac

These are native Mac ports of the Japanese PlayStation 2 releases of Biohazard Outbreak (File 1) and Biohazard Outbreak
File 2, made by recompiling the games' code, played in English with the keyboard and mouse, and online on
[obsrv.org](https://obsrv.org). The apps contain the games' recompiled code but none of the discs' data: you need your own
disc images, and the English is made from your disc on your Mac.

## What you need

- A Mac with Apple silicon (M1 or later) running **macOS 26 (Tahoe) or later**.
- A disc image of **your own** copy of the Japanese release:
  - File 1: Biohazard Outbreak, SLPM-65428, for **Biohazard Outbreak.app**;
  - File 2: Biohazard Outbreak File 2, SLPM-65692, version 1.03, for **Biohazard Outbreak File 2.app**;

  as an `.iso` (or a `.cue` with its `.bin`). Other releases (the USA and European Resident Evil Outbreak) aren't supported:
  the app checks the disc and says what's wrong with it. Only the Japanese discs are accepted by obsrv.org's servers.

Nothing else: the apps carry everything they need.

## First run

1. Unzip `Biohazard-Outbreak-File1-macOS.zip` or `Biohazard-Outbreak-File2-macOS.zip` and move the app wherever you like
   (Applications is fine).
2. The apps are signed ad hoc, not by an Apple developer account, so the first time macOS won't open them by a double-click:
   - Double-click the app. macOS says it can't verify it: click **Done**.
   - Open **System Settings > Privacy & Security**, scroll to Security, where it says the app was blocked, and click
     **Open Anyway**, then **Open Anyway** again (your password may be asked).

   (On older macOS this was right-click > **Open**; these apps need macOS 26, where it's done in System Settings.) You only
   have to do this once per app.
3. The app asks for your disc image: click **OK**, choose your `.iso` (or `.cue`) in the file dialog, and click **Open**. If
   it isn't the right disc, the app says why (another game, another version, not a disc image) and quits: open it again to
   choose another.
4. If your disc image is in your Documents, Desktop or Downloads folder, macOS asks whether the app may access that folder:
   click **Allow** (the app reads your disc image from there each time it starts).
5. The app makes the English patch from your disc: a window says so while it works (less than a minute). Then the game
   starts.

The app remembers your disc. The English patch is made again by itself when it's needed: after you change your keys (the
patch's pictures show your keys), after an update of the app, or with another disc image. To choose another disc image, delete
`disc_image.txt` from the settings folder (below) and open the app: it asks again.

## Where things are

Everything the game keeps is in **~/Library/Application Support/PS2 Native Runtime/SLPM_654.28/** (File 1) or
**.../SLPM_656.92/** (File 2). In Finder: Go > Go to Folder..., then paste the path.

| What | Where |
| --- | --- |
| Saves (the game's memory card) | `memcards/port0.ps2` |
| The options | `options.txt`, `graphics.txt` |
| Your controls | `bindings.txt` |
| The disc image's path | `disc_image.txt` |
| The English patch, made from your disc (don't share it: it's made of the game's own text and pictures) | `english.patch`, with `english.patch.made-from` and `english-patch.log` |
| The game's log: this run's, and the one before's | `logs/latest.log`, `logs/previous.log` |

## Controls

A gamepad works as the PS2's controller. With the keyboard and mouse, the layout follows the PC releases of Resident Evil 4
and 5:

| Key | Play | Menus |
|---|---|---|
| W A S D | move | move the cursor |
| Right mouse (hold) | ready the weapon; the mouse aims | confirm |
| Left mouse | attack, fire | point and click |
| E, Return | action, check, pick up | confirm |
| Left Shift (hold) | run | back |
| R | reload | |
| Space | ad-lib | |
| M | the map | |
| 1 2 3 4 (with F: call a partner) | the appeals | |
| Escape | the game's pause menu | back |
| Tab | the status screen (items, files) | |
| F10 | the PC menus (display, sound, controls) | |

Every key can be rebound in the PC menus' Controls. All of them, and how they follow the game:
[docs/controls.md](controls.md).

## Online

Network Play connects to the revived servers at [obsrv.org](https://obsrv.org). You need an obsrv.org forum account of your
own: sign up on obsrv.org's website first. In the game: NETWORK PLAY, confirm the caution, let the game make its NET file,
choose the network connection, then log in on obsrv.org's page with your forum ID and password (the game's software keyboard
takes the Mac's typing). The steps, in detail: [docs/network.md](network.md).

## Known issues

- Online: logging in and the lobby work; playing in an online room hasn't been tried yet.
- Speed: 30 frames a second in every scene measured on an M4; slower Macs (M1, M2) haven't been measured. If the game runs
  slowly, lower the internal resolution in the PC menus' Display.
- The mouse: hovering doesn't move the cursor over the character log's rows or the collection's grid (the wheel and a click
  do). SCREEN ADJUST and EDIT NETWORK CONFIGURATION are still listed in OPTIONS.
- A few texts stay Japanese on purpose: the name entry's kana tables and the online handle name's word filter.
- The game pauses while its window isn't in front (the PC menus' Gameplay > Pause in the background): click into it to go on.
- Sound hasn't been compared by ear with a console.
- Full playthroughs of every scenario haven't been finished.

## If something goes wrong

Every run writes its log to the settings folder (above): `logs/latest.log` is the last run's (or the one running now),
`logs/previous.log` the one before (so after a crash and a second start, the crash's log is `previous.log`). Each is 32 MB at
most. Online, the log also has a line for each message the game exchanges with obsrv.org's lobby and game server (their
commands only: no passwords, no chat). Please send the log with a bug report: it says what the game was doing. To watch it
live, run the app from Terminal, for example `"/Applications/Biohazard Outbreak File 2.app/Contents/MacOS/Biohazard"`. If the
English patch couldn't be made, the app says so and `english-patch.log` in the settings folder has the details.

## Licences

Biohazard Outbreak and its content belong to Capcom. The apps carry Python 3.13 (PSF License), Pillow (MIT-CMU) and NumPy (BSD)
to make the English patch, SDL 3 (zlib), the fonts Luckiest Guy (Apache 2.0) and Fredoka (SIL Open Font License), and
stb_truetype (public domain): their licences are in each app's `Contents/Resources/licenses`.
