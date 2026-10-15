# Status

Biohazard Outbreak (File 1, SLPM-65428) and Biohazard Outbreak File 2 (SLPM-65692), the Japanese discs, recompiled to native
Mac code on the shared PS2 runtime (`../ps2-native-runtime`; this work is on its `biohazard` branch, in the worktree
`~/Documents/woc-agents/biohazard/ps2-native-runtime`, not merged into its main yet).

## The plan (the owner's decisions)

1. Both games playable on the runtime's current graphics path (the game's own drawing, the GS drawn on the GPU), needing only
   the disc.
2. Mouse and keyboard controls; the game's mentions of the PS2 (memory card, controller, button glyphs) said for a computer,
   unless a controller is being used.
3. Online play on the revived servers (obsrv.org, which accept only the Japanese discs: these).
4. Then a native Metal renderer replacing the GS path piece by piece, checked against the working picture
   (`docs/research/renderer.md` has what's known).

## Where it is (2026-10-15)

- Both executables and their overlays recompile (File 2: 5,706 functions + 17,022 in nine overlays; File 1: 5,567 + 15,091 in
  eight) and build. The runtime gained code overlays, stripped executables, data kept in the text, encrypted (DNAS) overlays,
  and the IOP version read from the reset image.
- File 2 draws its NOW LOADING screen; both reset the IOP with `DNAS280.IMG`, load their modules from `DRIVE.DAT`, open
  `NETBIO00.DAT`, then stop in CRI's sound initialisation (`ps2rna_init_psj`, File 2 0x13DF78), waiting on the CRI ADX
  driver's IOP side (`RPC 90000200`, DTX).

## In progress

- CRI ADX driver stand-in (DTX, SJX, RNA/PSM, ADX streams): runtime branch `biohazard-sound`, game branch `sound`.
- DualShock and memory card (libpad2/libdbc/libmc2 with dbcman, ds2o_s1 / File 1's ds2u_s1, mc2_s1): runtime branch
  `biohazard-pads`, game branch `pads`.

## Still to do

- Capcom's own sound driver (DRIVE.DAT's unnamed module, on modhsyn/modmsin): sound effects.
- USB keyboard (USBKB, server 0x80000073? and the chat), the network stack (AVETCP/SN modules, DEV9/SMAP) and DNAS. DNAS's
  overlays decrypt blocks of themselves before running them: File 2's netdnas.bin calls its decryptor 0x6DB1A0 with
  parameters kept inline after the call (e.g. 0x6B2274, its data at 0x6B229C) and keys from its own table at 0x6DC280, then
  FlushCache. Nothing comes from outside, so the decrypted code should be the same every run: decrypt it ahead of time and
  recompile that (each block an overlay variant the dispatcher picks by its words in memory), rather than interpret.
- Sofdec movies: 21 .sfd files in NETBIO01.DAT's adxsnd.afs (PS2demo01.sfd, the endings, PS2continue.sfd); CRI SFD/M2V on
  the EE decode through the IPU, which the runtime doesn't have yet.
- PC controls, PC texts, the PC menus' game settings.
- The native renderer.

## Notes

- `tools/xref.py`, `tools/callers.py`: where code builds an address; a library's entry points.
- `work/` (ignored) holds files extracted from the discs for analysis.
- REOF2 (no licence) is read-only reference; reo (MIT) may be used with credit; libthanatos and OBTool are GPL (read-only).
