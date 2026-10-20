# Test report, 2026-10-16 (File 1 and File 2, current state)

Method: `tools/session.py` runs a game headless with a key script and reports fatals, what the runtime didn't do, frames a second
(the runtime's `--fps` log), audio levels, and a contact sheet. About 15 sessions, 2-5 minutes each, up to three side by side
(each with its own memory card), plus a 7 minute random-key fuzz of File 2. Nothing crashed. Fatals met on the way were all the
recompiler missing a function start (fixed: `tools/pointer_functions.py`).

## What works

| Area | File 1 | File 2 |
|---|---|---|
| Boot, logos | yes (Capcom) | yes (Sofdec, ADX, ACCESS AVE-TCP, Capcom) |
| Opening movie | the opening CG plays in-engine at scenario start | the 3.5 minute opening movie, credits included |
| Title and attract demo | yes | yes: title movie, demo play |
| Main menu | Single Play, Network Play (reaches the caution screen), Collection, Character Log, Options, HDD Install | the same; Collection (Data Convert), Character Log (single/network data), Options (controller, sound, brightness, screen, save/load, network configuration) all open |
| Scenario select | Outbreak only (the rest locked on a new card) | Training Ground, Wild Things, Underbelly, Flashback, Desperate Times |
| Character, partner, difficulty | yes | yes |
| Into the game | Outbreak: Kevin, Mark, Cindy in the bar | Training Ground (Kevin in the bar), Wild Things (the zoo gate cutscene with subtitles, then control) |
| In-game | walk, run, action ("CHECK!"), pause/item/file screens, subtitles, loading screens | the same |
| Memory card | untested | blank card: "not formatted, format?" then YES formats and saves ("save finished"); a second save asks to overwrite |
| Sound | audible (peaks to 32766 in menus) | audible through logos, movie, title, menus, play |
| Keyboard and mouse | RE4/RE5 PC layout, Escape = the game's own pause | the same |

## Frames a second (headless, from `--fps`; the PS2's game runs at 30)

Steady state is after the first 15 seconds. "Side by side" sessions shared the machine with one or two others.

| Session | Median | Mean | 10th percentile | 90th | Share of seconds at 28+ |
|---|---|---|---|---|---|
| File 2 menus (side by side) | 30 | 29.9 | 29 | 31 | 95% |
| File 2 options and collection (side by side) | 30 | 30.1 | 30 | 31 | 99% |
| File 2 HDD install screen | 30 | 29.1 | 23 | 31 | 83% |
| File 2 save/format/load screens | 30 | 30.2 | 27 | 31 | 89% |
| File 2 scenario select (side by side) | 30 | 30.1 | 28 | 31 | 93% |
| File 2 Wild Things, loading and cutscene | 30 | 24.2 | 5 | 31 | 68% |
| **File 2 Training Ground, played (alone on the machine)** | **20** | **22.4** | **20** | **30** | **25%** |
| File 1 menus | 30 | 30.0 | 30 | 31 | 98% |
| File 1 Outbreak, intro CG and play (2 side by side) | 30 | 27.5 | 16 | 31 | 84% |

Menus, movies, loading and cutscenes hold 30. Gameplay in File 2's Training Ground runs at 20 on its own: the 3D scene is the
slow part (File 1's bar scene held 30 with only the first seconds of play, so its gameplay speed isn't measured yet). Loads dip
(minimum 0-6).

## What the runtime doesn't do (logged once each)

- `RPC 80000001 (fileio) 20 and 23`: the HDD/PFS probes (hdd.irx and pfs.irx are loaded; no drive exists): answered with zeros,
  the game carries on (HDD Install screen opens).
- `RPC 80000211 function 1`, `syscall 0x6b`: unknown; the game carries on.
- `RPC 01270010 functions 0 and 1`: seen when the Network/menu screens are entered (File 2 menus, File 1 menus).
- `ds2`: rumble isn't done.
- The network: AVETCP/SN, DEV9/SMAP, DNAS. Network Play stops at its caution screen.

## Not tested yet

Finishing a scenario, dying and game over, area transitions beyond the first, the other scenarios' characters and items, loading a
save back, File 1's save/load, HDD Install's flow, a gamepad, a real window's speed (these runs are headless), long play (more
than ten minutes), and any of the online play.

## Findings worth following

1. File 2 gameplay speed: see "Speed work, 2026-10-17" below (zoo 6 to 15, Training Ground 20 to 24-26); still short of 30; windowed not measured.
2. Mouse look and aiming: no use found for the right stick yet.
3. The game's own button glyphs and Japanese menu text remain as the PS2 has them.

## Stage 2 follow-up (live play, same day)

Played File 2 with the runtime's `--control` channel (tools/live/): the Training Ground (read one file; the other two weren't
found by a sweep of the room) and Wild Things (the zoo: cutscene, then control, camera cuts as the character walks).

- **Two more missing function starts** surfaced by play (the game overlay's state-handler tables: 0x64d5c0, 0x676410). Both are in
  the config now; `tools/pointer_functions.py` also takes pointer targets that follow a JR RA.
- **Speed in the zoo (Wild Things) is 6 frames a second**, idle and with nothing taking pictures; the Training Ground is 20, menus
  and movies 30. So 3D gameplay is the slow part, and it varies a lot by scene. This is the first thing to profile.
- Not reached: a door or loading transition inside a scenario, game over, loading a save back, the other movies.

## Speed work, 2026-10-17 (headless `--fps`, same machine; other games were running on it at times, so GPU-bound numbers vary)

Profile (`sample`, `PS2_VU1_PROFILE`, `--gs-record` + `--gs-replay`): two causes.
1. **VU1 programs 12 and 14 (the 3D vertex programs) ran interpreted**: the translator stopped each block at ERSQRT, ERLENG, MFP
   and WAITP, so the rest of their loops was interpreted (6.1 billion interpreted instructions in a 13 minute load). The EFU is
   translated now (runtime 4f067ee): 4.1 billion instructions, all translated, 1.8 million runs checked by `PS2_VU1=verify`, 0 differences.
2. **The GPU path**: the game masks one bit of red (FBMSK 1) on every 3D draw, which the renderer blends in the shader on a copy
   of the destination, and each triangle's batch broke on bounding rectangles that touch. Batches now break only on real
   pixel overlap (runtime af6eade): replay of the Training Ground 4.7x faster, the loading screen 5.6x, identical picture hashes in all 740 vblanks.

| Scene | before | after |
|---|---|---|
| Loading screens | 0-6 (3 typical) | 14-25 |
| File 2 Training Ground (idle in the bar) | about 20 (played); 6-7 measured under load with the old build | 24-26 (machine busy) |
| File 2 Wild Things, zoo gate | 6 | 15 (quiet machine, steady) |
| File 2 partner select menu | not measured | 9-15 (text glyph + shadow sprites overlap: one copy each) |
| File 2 other menus, movies | 30 | 30 (not re-run in full) |
| File 1 intro CG and its first cutscene | 30 | 30 (its bar gameplay not reached by the script) |

Still slow, best theory: overlapping alpha geometry (foliage, text shadows) needs the destination per primitive, 100 copies and
render-pass changes a picture in the zoo, and OpenGL on Metal submits a command buffer for each; the zoo's replay is 75% waiting
on the GPU. A native Metal renderer (programmable blending) is the real fix; a copy shared by non-overlapping draws of different
state would save about a quarter. Not tried: a real window's speed. Tools: `ctl.py r "record 300 FILE"` / `r stop`.

## Network Play, 2026-10-20 (branch `online`)

File 2 and File 1: main menu, Network Play, NET file made and card formatted, the Mac's configuration, DHCP, DNAS 100%, obsrv's
login page in the KDDI browser. The software keyboard took "Test1" (Shift, Backspace, Return) from the Mac's keyboard on both,
File 2's Password field "test"; a memory dump showed the form holding them; nothing submitted. Keys typed while the keyboard was
still opening at 6-8 frames a second went to the pad (the first two letters lost once on File 1): wait for it to be drawn.
Single play afterwards: File 2's Training Ground loads, Kevin walks. Login itself and the lobby untested (needs the owner's
account). Details: docs/network.md.

## After login, 2026-10-20 (branch `online`)

The owner logged in (File 2, English patch, forum auto-login): the lobby (208.72.237.15:8200) ran five minutes, then joining
a game connected to the game server (208.72.237.31:8590) and stopped: the AVE wrapper's function 4 (a connection's option
got, AVE-TCP 0x4109) wasn't done. Functions 3 and 4 and AVE-TCP's options are in now; `ps2_avetcp_test` (the runtime's)
checks them against a local server (31 checks pass), with the call the game made. The new build reaches obsrv's login page
again (File 2, English); past login it waits for the owner's next run.
