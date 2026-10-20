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
- Font: fixed pitch; a full-width character is two cells, a half-width Latin one cell. Measured in play (pictures of the
  English patch): the FILE document window holds 36 cells a line and 7 lines (the Japanese uses up to 6); the item window 35
  cells, and no more lines than the Japanese; room messages and cutscene subtitles are centred lines on the screen's bottom,
  about 12 px a cell at 640 wide, so about 53 cells edge to edge: the English keeps within 50 (the Japanese's longest is 38 for
  rooms and speech, 46 for subtitles). Speech uses the same message line (not seen in play: see below).
- Tags checked to be kept: `<p1>`-style codes, markup with attributes (`<SIZE=3>`, `<font color=...>`), printf codes
  (`%s %2d %1`). `<br>` is layout and free. A unit is one Japanese string wherever it occurs: its English must fit every place.

## Where the text is (File 1; `./run.sh file1 --english`)
- The same containers as File 2, found the same way (config/translate.toml `[file1]`): the executable SLPM_654.28's pool
  0x132440-0x134C88 (the same pointer-table kind), its strings outside it (`elf`: the scenarios' short names, the browser's
  buttons), the overlays PROG/DEMO.BIN (title menus, options, records, gallery), GAME.BIN (results, save, pause, item use),
  SINGLE.BIN, NETAQ.BIN (the network game's event log) and NETWK.BIN (lobby, network settings, error pages, chat phrases),
  the stage message files (r001 r002 r028 r035 r041), the browser's pages (rfff.afs, the same pages as File 2's).
- Item and message files instead of the pit files (tools/bh_itemmsg.py): in romdata.afs and the stage archives, inverted
  Shift-JIS in fixed fields: `itmsg` (item names and descriptions, 0x40 records), `spitem` (SP items: name 0x14, description
  0xEC), `evelist`/`newfact` (goals, new-feature notices), `dlmsg` (loading-screen captions), the map files (room and floor
  names), and `fileNNN.dat` (FILE documents, as File 2's DocChain). English must fit each field.
- File 2's English is the fallback (config `fallback`): a unit with the same Japanese in both games takes File 2's line unless
  translations/file1 has it; File 1's own overrides are mostly shorter lines for its smaller slots and device-neutral wording
  (translations/file1/devices.tsv: save data instead of the memory card, the font's button glyphs instead of button names).
- NETWK.BIN: the in-place strings must fit their slots; the error page at 0xA1A80 starts after 3 bytes of a binary table
  (the region begins there).
- Empty strings live in other strings' padding: the compiler keeps "" in a NUL after another string, and tables point at it
  (File 1's NETWK.BIN: 70 message tables' blank line is 0x5F5348, in the padding after the string at 0xB5320). Packing the
  English into that padding showed the tail of another line where the game wants a blank one ('id "Your' in the "no NET
  file" dialog). ExePool now ends a slot at an empty string a pointer points at (found in both games' pools: File 1's
  executable 2, DEMO.BIN 1, NETWK.BIN 3; File 2's executable 8, BIN/3.DAT 3); seen fixed in play (the dialog's blank line).
- The string at 0xBB750 ("確認･変更する接続プロバイダー設定の", the check/change screen's first line) is read from its start:
  its only reference is the pointer in the screen's line table at 0x5EBE14, and the line drawer (0x5CB7B0) passes the
  table's pointer unchanged to the text printer (0x5CCC90, then the executable's 0x1B6180); nothing builds 0x5FB755 (no code
  pair, no data word in the executable or any overlay). Its second line (0x5FB780) is shared by the check/change, delete and
  test screens, so it now says only where to load from and the first lines name the action. Left Japanese on purpose: the software keyboard's rows, the romaji-to-kana table, the kana
  dakuten tables, digit tables.

## Pictures (both games; tools/bh_pictures.py, tools/bh_sld.py, tools/tm2.py)
Pictures with Japanese, and pictures with PS2 button icons, are redrawn at patch-build time from your disc (nothing of the
game's art is in the repository): translations/<game>/pictures.toml and translations/pictures-shared.toml give the boxes to
erase (clear, inpaint for text on photos, a fill) and the English to draw (macOS system fonts at build time, or the runtime's
Fredoka/Luckiest Guy). File 1's .tex are LZ-compressed TIM2 streams (bh_sld), File 2's MOMO containers of them; an edited
picture is quantised back to its own palette (unchanged pixels keep their bytes) and recompressed, and must not grow.
A picture with PS2 buttons gets two variants (disc patch version 2: variant 1 laid while the keyboard and mouse were used last,
2 while a gamepad was): the gamepad's keeps the PS2 symbols, the keyboard's draws key caps with the keys of bindings.txt (or
src/controls.cpp's defaults). The text fonts' four symbol glyphs (in-text ○ × △ □, and the English's {circle} {cross}
{triangle} {square}) get the same two variants (tools/bh_font.py: E, Esc, Spc, F by default): those are the menus' keys.
A button can mean another key in play: ✕ is Esc (back) in the menus but Left Shift (the game's Cancel/Dash) in play, □ is F
in the menus but the left mouse button (Ad-lib) in play. So a line about play writes {play:cross} (and {play:circle}...):
four other font cells, ∈ ∋ ⊆ ⊇ (no unit of either disc uses them), drawn with play's keys (E, a Shift arrow, Spc, LMB by
default) or, for the gamepad, the PS2 symbols. Where ✕ appears (from both discs' units): in menus, File 1's DEMO.BIN hints
("○: start ×: back") and NETWK.BIN's (cancel, end); in play texts, File 1's FILE documents (file001: "Lending a Shoulder",
stop with ×; file028: × while ready) and the map and ad-lib ones (△, □, ○), which now use the play glyphs; File 2's
documents name the game's actions instead (Action, Cancel, Ad-lib, Ready, Map: the controller settings' own labels), and
its two lines that said "X" say Cancel. Checked in play (File 1, a test line on the scenario select): the play cells show
the play keys (work/file1-glyph-test.png on the machine that made it).
- Both: cockpit.tex (status screen button glyphs, the map's R1/L1, the item categories and map controls), Error.tm2 (network
  error), lobby_00/03/05 (lobby buttons and hints, keys), Softkey.tm2, BS_SKIN.TM2 (browser button icons, keys), netwk.tex
  (network menus' button icons, keys).
- File 1: select.tex (scenario names, goals, the select screens' hint strip with keys; the five scenario cards), result.tex
  (names, labels, hints with keys, the EXIT button), demo_com.tex (the violence warning, the ACCESS notice, the character log
  tabs, the options' button icons with keys), lobby_01 (server and Capcom ID titles), player_00/01 (start message),
  prize01 (AVAILABLE, button icons with keys), scr54_55...scr72_73 (the lobby's ten scenario cards).
- File 2: select.tex (hint strip with keys, the twelve scenario cards), result.tex (labels, hints with keys, EXIT), init.tex
  (ACCESS notice), lobby_01, lobby_15 (standby buttons), prize.tex (AVAILABLE; the collection's button icons with keys).
- Left as they are: the options' controller picture and its button-name labels (they show the gamepad), the analog stick
  icons, English art (PRESS START BUTTON, NOW LOADING, costume and gallery pictures).

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
| exe pool | 523 / 641 (118 kept) | 523 / 641 (118 kept: the handle-name word filter) |
| rooms | 1213 / 1391 | 1391 / 1391 |
| subtitles | 1302 / 1302 | 1302 / 1302 |
| speech | 1279 / 15871 | 15871 / 15871 |
| pit (items, files, places) | 1561 / 1619 (58 kept) | 1568 / 1619 (51 kept: puzzle letter grids, data fragments, "Dummy" slots) |
| programs' other strings (elf, BIN/1, BIN/2) | not extracted | 48 / 54 (6 kept: percent formats with the full-width ％) |
| network overlay BIN/3.DAT | not extracted | 1177 / 1182 (5 kept: the name entry's kana and digit tables) |
| browser pages (html) | not extracted | 146 / 146 |
| total | 20825 units extracted | 22026 / 22206 (180 kept on purpose, none left untranslated) |

## Coverage (File 1, units translated / total, from bh_build_patch.py)
| kind | translated |
|---|---|
| exe pool | 222 / 331 (109 kept: the handle-name word filter, save-data titles, full-width floor labels) |
| elf (outside the pool) | 8 / 8 |
| overlays DEMO, GAME, SINGLE, NETAQ | 445/446 (1 kept), 34/34, 1/1, 56/56 |
| network overlay NETWK.BIN | 1157 / 1725 (568 kept: kana, keyboard and digit tables) |
| item files (items, SP items, goals, notices, captions, maps, FILE documents) | 1431 / 1431 |
| rooms | 1411 / 1412 (1 kept) |
| subtitles | 675 / 675 |
| speech | 7034 / 7034 |
| browser pages (html) | 146 / 146 (File 2's English) |
| total | 12620 / 13299 (679 kept on purpose, none left untranslated) |

Kept Japanese on purpose (`-` in the TSVs) is counted apart; everything else has English.

## Checked in play (headless pictures of the English patch)
Title and single play menus, scenario select blurbs (Training, Wild Things), character and partner select (bios), the Wild
Things opening subtitle, the Training Ground (CHECK!, the task line, the FILE "About Training" document), the item window. The
FILE and item windows' widths were measured there and every document and description rewrapped to them.

File 1 (2026-10-20): the ACCESS notice, title and single play menus, scenario select (card, blurb, key-cap hints), character
select and difficulty, the Outbreak opening subtitles, a speech line in the bar, the item window (names and descriptions), the
status screen tabs, the pause menu (options with the font's key glyphs).

## Not checked in play
- Speech lines (spm): the partners' lines did not come up in the scripted sessions; they use the bottom message line whose
  width was measured on room messages and subtitles.
- File 1: room messages, FILE documents, loading captions, results (the room ran at about 5 frames a second here).
- The gamepad variant of the pictures and glyphs (no gamepad here; its bytes are the disc's own).
- The network screens and browser pages (the network doesn't run here), the endings (the epilogue lines fit their slots by
  the builder's check).
