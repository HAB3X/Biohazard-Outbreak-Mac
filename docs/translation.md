# English patch: findings and tools

The patch is applied to your own disc at run time: nothing of the game is in this repository. `tools/bh_build_patch.py` reads
your disc image (local.json) plus `translations/<game>/*.tsv` (unit id, TAB, my English) and writes byte ranges; the runtime's
`--patch FILE` lays them over the disc's sectors as they are read (runtime/src/disc.cpp, game-neutral, off unless given).

## Where the text is (File 2; evidence by extraction and play)
- Executable SLPM_656.92: a pool of Shift-JIS strings at file offsets 0x13E060-0x143110 (menus' blurbs, dialogs, item verbs,
  names, collection labels), reached only through pointer tables in the data (890 of 903 strings); `ExePool` packs the English
  into the freed slots and rewrites the pointers (executable and overlay data). Control tags: `<p1>`, `<c0-7>`, `<I>`, `<P>`,
  `%t %r %a %s`. The pool also has a handle-name word filter (left Japanese on purpose).
- The programs' other strings (config `overlay_pools`, regions found by listing the strings of each data section): the
  executable's DUALSHOCK 2 warning (0x143700) and browser buttons (0x14A9F8); the collection overlay BIN/1.DAT (dialogs,
  viewer hints, movie names, records); the game overlay BIN/2.DAT ("used %s"); the network overlay BIN/3.DAT (netwk.bin,
  about 1,200 strings: menu help in `<BODY><SIZE=3><COLOR=6>...<END>` markup, dialogs, lobby, chat, memory card, connection
  settings, HTML error pages). Strings pointed at from the overlay's data are packed again like the executable's pool; the
  others (reached by code, or fields of fixed width like the chat phrase labels, 12 bytes) are rewritten in place. No netwk
  string is reached both by code and by a pointer (checked: lui/addiu pairs against data words). Left Japanese: the name
  entry's kana keyboard and romaji table, a font table, a test name.
- The executable's inverted Shift-JIS (every byte NOT, as in the pit files) at 0x143110-0x143700: the ending's 46 epilogue
  lines, each in a 16-48 byte slot (pointer table at 0x11EC20); the same lines are in dm subtitle files, so their English must
  fit the slot.
- Stage message files in NETBIO00.DAT's r0NN.afs: `rNNN.bin` (room messages), `dmNNN.bin` (cutscene subtitles),
  `spmSSAACC.bin` (speech; CC = speaker 00-07 the playable characters, 09-16 the same eight again, 08 others). Format: see
  `MsgBin` in tools/bh_text.py (u32 end, u32 arrays; records (count, offset); pointer list; strings `text CRLF NUL`,
  even-aligned; 0xFF fill). Every file rebuilds byte-identical from its parse (checked by bh_extract.py).
- Item names/descriptions, SP items, FILE documents and place names: Shift-JIS with every byte inverted in `pit_comn.dat` and
  `pit_sNNN.dat` (MOMO containers; tools/bh_pit.py).
- The network browser's pages: rfff.afs `*.HTM` (Shift-JIS HTML: the terms of use, the network cautions, registration).
  `HtmlPage`: a unit is the text between two tags (with its `<br>`s: the English breaks its own lines); a page keeps its size
  (the archive's table is left alone), padded with spaces. The browser reads settings from a comment (`<GAME-STYLE>`), kept.
- Baked in textures: button hints, scenario-select titles, error pictures (another agent's work; `tools/tm2.py`).
- Nothing else: the stage archives' and romdata.afs's other members hold no Shift-JIS text (scanned: models, textures, motion).
- Font: fixed pitch; a full-width character is two cells, a half-width Latin one cell (about 9.7 px at 640 wide). English
  lines should stay within the cell count of the Japanese window (about 48-50 cells in the scenario blurb box).
- Tags checked to be kept: `<p1>`-style codes, markup with attributes (`<SIZE=3>`, `<font color=...>`), printf codes
  (`%s %2d %1`). `<br>` is layout and free. A unit is one Japanese string wherever it occurs: its English must fit every place.

## Tools
- `bh_archive.py GAME ls|get|dump|where` : nested AFS access straight from the ISO.
- `bh_extract.py GAME work/f2/extract` : all units to units.jsonl (work/ only).
- `bh_batch.py`, `bh_merge_translation.py` : cut batches of untranslated units; add English (`prefix TAB english`).
- `bh_build_patch.py GAME out.patch` : checks (ASCII, same control tags, fits), builds the patch and prints the coverage per
  kind of container.
- Trace aids in the runtime: `--control` `ram FILE`, `PS2_TRACE_WATCH_SKIP/MAX`.

## Coverage (File 2, units translated / total, from bh_build_patch.py)
| kind | 2026-10-20 start | now |
|---|---|---|
| exe pool | 523 / 641 (118 kept) | 523 / 641 (118 kept) |
| rooms | 1213 / 1391 | 1391 / 1391 |
| subtitles | 1302 / 1302 | 1302 / 1302 |
| speech | 1279 / 15871 | 1279 / 15871 |
| pit (items, files, places) | 1561 / 1619 (58 kept) | 1561 / 1619 (58 kept) |
| programs' other strings (elf, BIN/1, BIN/2) | not extracted | 0 / 54 |
| network overlay BIN/3.DAT | not extracted | 0 / 1182 |
| browser pages (html) | not extracted | 0 / 146 |
