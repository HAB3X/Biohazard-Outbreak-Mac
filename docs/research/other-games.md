# Other games' assets: what's known

A first try at bringing assets from other PS2 horror games into Outbreak (commits b20856d to 6f13a63, 2026-10-26) was
reverted on 2026-10-27: most of it was invented (made-up catalogs drawn as coloured boxes, synthesised "sounds", an "RE4 AI
port" that was new code with RE4 names on it), nothing was ever seen working in the game, and placing its enemies crashed
File 1. What it found that was real, checked by rendering the output from the discs, is below. The code is in those commits'
history if it's useful again; check where the Haunting Ground reader came from before reusing it.

## Haunting Ground (Europe)

- Models are `.PCK` with textures in `.TEX`, inside the disc's CVM container (`bh_hg.py` in eaa6ff2).
- Fiona, Debilitas and Hewie decoded with correct meshes and textures.
- The item entries were textures only, no models.

## Silent Hill 2 Director's Cut (Europe)

- Character models are `.mdl` (`data/chr/...`; Pyramid Head is `data/chr/red/red.mdl`). Geometry decodes correctly with the
  bones' bind matrices and strip indices (`bh_sh2.py` in 6f13a63): Pyramid Head, James, Nurse, Maria checked.
- Textures came out garbled (wrong pixel order, mostly transparent): not solved.
- Item names were guesses (`b_ami.mdl`, called a radio, is a fence).

## Resident Evil 4

- `BIO4DAT.AFS` reads (`bh_re4.py` in b20856d). Figurines and Leon come out recognisable but with spurious strip triangles;
  the three "enemies" tried were all the same 57-triangle prop. TPL textures decoded as noise.

## Outbreak's side

- A TIM2 written for Outbreak needs its picture count set (`<4sBBH8x`: version 4, alignment 0, one picture); the reverted
  code wrote 0 pictures, so every texture it made was empty.
- An enemy record given a kind of 100 or more kills File 1 at creation (`game_00689980`, called from 0x688C40): File 1's map
  of kinds to AI routines (GAME.BIN 0x6D7040) has 70 entries.
- Enemies of File 2's kinds already run in File 1 (`tools/bh_transplant.py`), but most of them stand still: not finished.
