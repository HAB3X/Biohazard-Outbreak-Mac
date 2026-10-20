# Status

Biohazard Outbreak (File 1, SLPM-65428) and Biohazard Outbreak File 2 (SLPM-65692), the Japanese discs, recompiled to native
Mac code on the shared PS2 runtime (`../ps2-native-runtime`; this work is on its `biohazard` branch, in the worktree
`~/Documents/woc-agents/biohazard/ps2-native-runtime`, merged into the runtime's main on 2026-10-20 (WoC and CTTR rebuilt and
run on it); the rumble is on `biohazard-polish`, not merged yet).

## The plan (the owner's decisions)

1. Both games playable on the runtime's current graphics path (the game's own drawing, the GS drawn on the GPU), needing only
   the disc.
2. Mouse and keyboard controls like the original PC releases of Resident Evil 4 and 5 (WASD-style movement, mouse aim and
   look, the same action keys and prompts), with Esc opening the game's own native pause menu (what START does on the pad).
   The game's mentions of the PS2 (memory card, controller, button glyphs) said for a computer, unless a controller is used.
3. Online play on the revived servers (obsrv.org, which accept only the Japanese discs: these).
4. Then a native Metal renderer replacing the GS path piece by piece, checked against the working picture
   (`docs/research/renderer.md` has what's known).

## Where it is (2026-10-16)

- Both executables and their overlays recompile (File 2: 5,706 functions + 17,022 in nine overlays; File 1: 5,567 + 15,091 in
  eight) and build. The runtime gained code overlays, stripped executables, data kept in the text, encrypted (DNAS) overlays,
  the IOP version read from the reset image, CRI's ADX IOP driver (merged from `biohazard-sound`), a DMAC fix (CHCR.TAG), the
  IPU (merged from `biohazard-ipu`: MPEG-2 decoding on the registers and DMA channels) and the pad and memory card modules.
- File 2 runs its real start-up at 30 frames a second: the violence warning, the Sofdec/ADX and ACCESS logos, then **the
  opening movie plays to its end** (Capcom Presents, the credits over the RPD scenes; `milestones/file2-opening-movie.png`),
  every picture decoded by the IPU. The movie was black until the kernel's thread calls returned the thread's id: CRI's SVM
  compares ResumeThread's result with the id to wake its level 6 thread, where Sofdec's decoder runs.
  Its DualShock 2 works (SDK 2.8's dbcman and ds2o_s1, merged from `biohazard-pads`): a scripted press reaches the game's pad
  data. Its memory card is a .ps2 image (port0.ps2 in --memcards, PCSX2's layout); the game's first card commands are
  answered, no save tried yet.
- **File 2 plays**: START skips the opening movie; the title screen ("PRESS START BUTTON" over the title movie), the main menu
  (SINGLE PLAY, COLLECTION, CHARACTER LOG, OPTIONS; NETWORK PLAY is greyed), scenario, character and partner select work (○
  confirms, ✕ goes back, as the Japanese game has them), and the Training Ground scenario loads into the game: Kevin in the bar
  with the HUD and the item menu (`milestones/file2-main-menu.png`, `file2-in-game.png`). The keyboard and mouse (src/controls.h):
  WASD to move, Left Shift run (and cross: back in menus), E or Return action/confirm (circle), Left Mouse attack (square),
  Right Mouse ready the weapon (R1), Space command the partner (triangle), Q/R/F L1/R2/L2, arrows the D-pad, Left Ctrl walk;
  Escape is START, the game's own pause menu (item, special and file screens: `file2-pause-file.png`). It's the RE4/RE5 PC
  layout as far as the buttons' roles in this game allow; every control is rebindable in the PC menus. Mouse look (the right
  stick) isn't bound: the game has no free camera to aim.
- **File 1 plays too**: its memory card module (mc2_s1 2811) is in, and with the same menus (scenario, difficulty, character
  select) it loads the "Outbreak" scenario into the game with its three characters (`milestones/file1-in-game.png`). Same
  controls (src/controls.h).
- **Sound is heard** (branch `sound`, runtime `biohazard-sound`): Capcom's own IOP sound driver (DRIVE.DAT's unnamed module 10,
  `src/tsnd.cpp`, docs/capcom_sound.md in the runtime) is stood in natively on the runtime's modhsyn/modmsin synthesizer and libsd, so
  the SPU2's cores are enabled and set up as the module does. The File 2 dump has the opening movie's soundtrack, the title's
  music and the menu's confirm sounds (a burst at each START/CROSS press); no clipping. File 1 boots with the driver and is
  audible too. Not yet judged by ear: the waveform and levels are sane but the mix against the real console isn't compared.

## Integration, 2026-10-20

The open branches are merged: the runtime's `biohazard` has `biohazard-translate` (--patch) and `biohazard-input` (latched
presses, Escape bound by the game, the pointer over menus); the game's `main` has `translate` and `input`. Before this, main's
`./run.sh file2 --english` asked the runtime for --patch, which only those branches had. Checked on the merged build: File 2
with `--english` goes from boot through the title, main menu, scenario/character/partner select (English bios) into the
Training Ground (menus 30 fps, the room 30 once loaded, the load ~21); File 1 to its Outbreak scenario's opening cutscene
(median 30 fps); both with sound, no fatal. Recompiler tests pass.

## Online play (branch `online`, runtime `biohazard-online`), 2026-10-20

Network Play reaches obsrv.org's login page on both discs: the network setup (the Mac's network as the one configuration,
DNS 208.72.237.31), DHCP, DNAS to 100% (its overlays decrypted at build time, the runtime answering DNAS's IOP helper) and
the KDDI browser over HTTPS, all on the Mac's sockets. The Mac's keyboard types into the game's software keyboard (the
game's USB keyboard, while the keyboard is up): checked with test text in the login form, nothing submitted. Logging in
needs the player's own obsrv.org forum account; after login is untested. How to, what stands in for what, what's left:
docs/network.md (the runtime's docs/network.md has the design).

## Polish, 2026-10-20 (branch `polish`, runtime `biohazard-polish`)

- Rumble: the DualShock 2 driver's actuator command (ds2's SendData) rumbles the first gamepad through SDL (big motor low
  frequency, small motor high), off with the PC menus open. The game sends it (both games: a confirm on the title menu rumbles
  big then small); `--log input` shows it. Not felt: no gamepad was connected here.
- File 1's scenario and character selects step with the wheel and with clicks beside the card or on the arrows (the title
  overlay's screen byte says which select is up). File 1's options list and collection still get no menu keys from the
  keyboard (docs/controls.md says why).
- The font's ✕ means Esc in the menus and Left Shift in play: texts about play use other font cells drawn with play's keys
  ({play:cross}..., docs/translation.md).
- The English patch kept a blank line blank: empty strings in a pool's padding are kept (File 1's "no NET file" dialog
  showed 'id "Your'); the network settings' shared second line no longer says "check or change" on the delete and test
  screens. The suspected clipped line at NETWK.BIN 0xBB750 is read from its start (docs/translation.md).
- Made with tools/make_app.py and launched with `open -a` (headless for the pictures): both reach their English title menus
  with the settings and memory cards of run.sh's folder. A windowed launch from a script stays paused at the warning screen
  until the window has the focus (the PC options' pause_in_background, on in this machine's settings).
- Found, not changed: the in-play roles in src/controls.cpp don't match the game's own (its controller settings, type A: ○
  confirm/attack, ✕ cancel/dash, □ ad-lib, △ map screen, L1 reload, R1 search ready, R2 forward ready, L2 appeal shift, the
  right stick appeal): the left mouse button (□) is ad-lib, not attack, and Space (△) the map, not the partner command.

## The Mac apps

`python3 tools/make_app.py both` makes "Biohazard Outbreak.app" and "Biohazard Outbreak File 2.app" on the Desktop (`--out DIR`
elsewhere): each starts from Finder in English, with nothing else needed at run time (the program, SDL3, the English patch
built from your disc at make time, the disc's path; signed ad hoc). They keep their settings, key bindings and saves where
run.sh's runs do (~/Library/Application Support/PS2 Native Runtime/SLPM_654.28 and SLPM_656.92). **Make them again after a
rebuild** (`ninja -C build`), a change to translations/, or a change of keys in the PC menus' Controls: the patch's key caps
are drawn with the keys bound when the app is made (the patch isn't rebuilt at launch: that needs this folder's Python tools
with Pillow and numpy, which a Finder launch doesn't have, and about a minute). Options can be passed for a test run:
`open -a "Biohazard Outbreak" --args --run-seconds 60 --snapshots DIR --snapshot-at 50`.

## English patch (branch `translate`, runtime `biohazard-translate`)

`./run.sh file2 --english` builds `build/english-file2.patch` from your disc and `translations/file2/*.tsv` (my own English,
keyed by unit ids) and plays with it (runtime `--patch`). File 2's text is all in English: menus, dialogs, item names and
descriptions, FILE documents, place names, room messages, subtitles, all 15,871 speech lines, the epilogue, the network
screens and the browser's pages (22,027 of 22,206 units; the other 179 are kept Japanese on purpose: the handle-name word
filter, the name entry's kana tables, puzzle letter grids). Wording is device-neutral (save data, Confirm/Back).
`./run.sh file1 --english` does the same for File 1 (branch translate-file1): 12,621 of 13,299 units, the other 678 kept
Japanese on purpose (word filter, kana and keyboard tables); File 2's English is used for the lines both games share. The
pictures with Japanese or PS2 buttons are redrawn at build time in both games, those with buttons (and the fonts' button
glyphs) in two variants: key caps while the keyboard and mouse were used last, PS2 symbols with a gamepad (patch version 2).
Details, window widths and what was checked in play: docs/translation.md.
The save dialogs say nothing of the PS2's memory card (branch `slots`): no slot, port or "PlayStation 2 memory card (8MB)"
line in either game. Those lines are written `{drop}` and taken out of the dialogs' line tables at build time, the slot
number with them; the NET file screens' header reads "NET File". With the one save location the games never ask which slot
(seen in play: saving, loading, autosave, Network Play's NET file). docs/translation.md "Save dialogs".

## In progress

- Speed (2026-10-17): zoo 6 to 15 fps, Training Ground 24-26, loads 3 to 14+; fixes in the runtime (EFU in the VU1 translation, pixel-overlap batching); details in docs/TEST_REPORT.md. 30 in the 3D scenes still needs fewer destination copies (Metal renderer).

- Sound in play (SEs, BGM past the title and menus) unchecked by ear; the mix and reverb against a console aren't compared.

## Still to do

- Online after login (docs/network.md): the lobby, handle name and chat, games with others (UDP and peer connections in the
  runtime's AVE-TCP); the configuration list's invisible row text.
- Sofdec movies: the opening plays; check the other 20 .sfd files (NETBIO01.DAT's adxsnd.afs: PS2demo01.sfd, the endings,
  PS2continue.sfd), and the title screen after the opening.
- PC texts (the game's button glyphs and its Japanese menu text said for a computer), the PC menus' game settings; mouse aiming
  if the game has a use for the right stick.
- The native renderer.

## Notes

- `tools/xref.py`, `tools/callers.py`: where code builds an address; a library's entry points.
- `tools/pointer_functions.py`: functions reached only through tables of pointers (the game's state handlers): the
  recompiler starts functions at calls, so the run stops with "not a function start". It finds data words that point at a
  prologue and code after zero padding that follows a JR RA, and writes them into the config's `extra` lists (`--write`).
  Run it, rebuild, run again until no start is missing.
- A trace build (the runtime's PS2_TRACE_ENTER; `cmake -S . -B build-trace -DCMAKE_C_FLAGS=-DPS2_TRACE_ENTER
  -DCMAKE_CXX_FLAGS=-DPS2_TRACE_ENTER`): function entry counts to $PS2_TRACE_FILE at the end of a --run-seconds run,
  $PS2_TRACE_ARGS (entries with their argument registers), $PS2_TRACE_FIND=LOW-HIGH (entries with an argument in a range),
  $PS2_TRACE_WATCH=ADDR[:BYTES] (stores into a range, with the guest's call stack), $PS2_TRACE_POKE=S:ADDR=VALUE. It's how the
  movie's missing decoder thread was found.
- `work/` (ignored) holds files extracted from the discs for analysis.
- REOF2 (no licence) is read-only reference; reo (MIT) may be used with credit; libthanatos and OBTool are GPL (read-only).
