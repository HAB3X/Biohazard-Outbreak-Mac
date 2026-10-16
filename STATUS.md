# Status

Biohazard Outbreak (File 1, SLPM-65428) and Biohazard Outbreak File 2 (SLPM-65692), the Japanese discs, recompiled to native
Mac code on the shared PS2 runtime (`../ps2-native-runtime`; this work is on its `biohazard` branch, in the worktree
`~/Documents/woc-agents/biohazard/ps2-native-runtime`, not merged into its main yet).

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

## In progress

- Sound in play (SEs, BGM past the title and menus) unchecked by ear; the mix and reverb against a console aren't compared.

## Still to do

- USB keyboard (USBKB, server 0x80000073? and the chat), the network stack (AVETCP/SN modules, DEV9/SMAP) and DNAS. DNAS's
  overlays decrypt blocks of themselves before running them: File 2's netdnas.bin calls its decryptor 0x6DB1A0 with
  parameters kept inline after the call (e.g. 0x6B2274, its data at 0x6B229C) and keys from its own table at 0x6DC280, then
  FlushCache. Nothing comes from outside, so the decrypted code should be the same every run: decrypt it ahead of time and
  recompile that (each block an overlay variant the dispatcher picks by its words in memory), rather than interpret.
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
