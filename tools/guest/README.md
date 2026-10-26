# Guest games

Readers that bring other PS2 games' assets into Outbreak, starting with Resident Evil 4 (USA, SLUS-211.34). Work in progress:
nothing here is used by the game or the Studio yet.

Verified so far (2026-10-27):
- `BIO4DAT.AFS` members read with `bh_archive` (2628 members: dat, tpl, snd, bin, adx, rel...).
- An `emNN.dat` pack: u32 count, offsets at 0x10, then a 4-letter tag per entry (BIN models, TPL textures, FCV/SEQ motion).
- `re4_bin.py`: a BIN model's skeleton (16-byte bone records from 0x50: number, parent, rest offset) and its parts' vertex
  batches (DMA chains of VIF uploads: a weight table, a header with the vertex scale 1/16, V4-16 triplets of position + weight
  set, normal + strip flag, texture coordinate). em10.dat entry 551 renders as a soldier's body with a gatling gun, in
  bind pose; a few strips still come out missing.

Not done: texture coordinates' units and the TPL textures, the weight sets' bone palette, motion (FCV/SEQ), sounds,
the item/weapon/stage packs, and everything on Outbreak's side.
