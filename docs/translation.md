# English patch: findings and tools

The patch is applied to your own disc at run time: nothing of the game is in this repository. `tools/bh_build_patch.py` reads
your disc image (local.json) plus `translations/<game>/*.tsv` (unit id, TAB, my English) and writes byte ranges; the runtime's
`--patch FILE` lays them over the disc's sectors as they are read (runtime/src/disc.cpp, game-neutral, off unless given).

## Where the text is (File 2; evidence by extraction and play)
- Executable SLPM_656.92: a pool of Shift-JIS strings at file offsets 0x13E060-0x143110 (menus' blurbs, dialogs, item verbs,
  names, collection labels), reached only through pointer tables in the data (890 of 903 strings); `ExePool` packs the English
  into the freed slots and rewrites the pointers (executable and overlay data). Control tags: `<p1>`, `<c0-7>`, `<I>`, `<P>`,
  `%t %r %a %s`. The pool also has a handle-name word filter (left Japanese on purpose).
- Stage message files in NETBIO00.DAT's r0NN.afs: `rNNN.bin` (room messages), `dmNNN.bin` (cutscene subtitles),
  `spmSSAACC.bin` (speech; CC = speaker 00-07 the playable characters, 08-16 others). Format: see `MsgBin` in tools/bh_text.py
  (u32 end, u32 arrays; records (count, offset); pointer list; strings `text CRLF NUL`, even-aligned; 0xFF fill). Every file
  rebuilds byte-identical from its parse (checked by bh_extract.py). 19205 distinct Japanese strings in all.
- Not plain Shift-JIS (still Japanese): item names/descriptions, FILE documents and SP items are glyph-index text (16-bit codes
  0x6A00-0x7EFF, 0xF5 = newline, `C3 xx CE C1` colour) in `pit_*.dat` (MOMO containers) and the exe (0x143110 on); the codes
  go through a table to Shift-JIS in RAM. Decoding them (glyph font in pit_comn entry 3: 1169 16x16 glyphs) is open.
- Baked in textures: button hints, scenario-select titles, error pictures (`Error.tm2`, `select.tex`...). `tools/tm2.py`
  reads standard TIM2; the `.tex`/`.sld` members use a variant not yet decoded.
- Network overlay BIN/3.DAT (1043 strings, `<BODY><SIZE=3>` markup) and rfff.afs HTML pages: not started.
- Font: fixed pitch; a full-width character is two cells, a half-width Latin one cell (about 9.7 px at 640 wide). English
  lines should stay within the cell count of the Japanese window (about 48-50 cells in the scenario blurb box).

## Tools
- `bh_archive.py GAME ls|get|dump|where` : nested AFS access straight from the ISO.
- `bh_extract.py GAME work/f2/extract` : all units to units.jsonl (work/ only).
- `bh_batch.py`, `bh_merge_translation.py` : cut batches of untranslated units; add English (`prefix TAB english`).
- `bh_build_patch.py GAME out.patch` : checks (ASCII, same control tags, fits) and builds the patch.
- Trace aids in the runtime: `--control` `ram FILE`, `PS2_TRACE_WATCH_SKIP/MAX`.

## Checklist (done / left)
- Done in the patch: exe pool (menus, dialogs, memory card and HDD texts, scenario blurbs, collection labels), Training
  Ground room messages, 820 speech lines (the most used). Seen in play: controller settings, scenario select.
- Left: the other ~17,000 message lines, glyph-index text (items, files), textures, network, File 1, per-screen visual checks.
