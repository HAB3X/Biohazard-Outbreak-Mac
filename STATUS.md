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
- USB keyboard (USBKB, server 0x80000073? and the chat), the network stack (AVETCP/SN modules, DEV9/SMAP) and DNAS (its
  overlays decrypt themselves: they can't run as recompiled; DNAS has to be answered another way).
- Sofdec movies (CRI SFD/M2V on the EE: the IPU).
- PC controls, PC texts, the PC menus' game settings.
- The native renderer.

## Notes

- `tools/xref.py`, `tools/callers.py`: where code builds an address; a library's entry points.
- `work/` (ignored) holds files extracted from the discs for analysis.
- REOF2 (no licence) is read-only reference; reo (MIT) may be used with credit; libthanatos and OBTool are GPL (read-only).
