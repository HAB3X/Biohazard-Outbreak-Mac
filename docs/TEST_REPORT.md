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

1. File 2 gameplay at 20 fps: profile the render and VU1 side (PS2_VU1_PROFILE, the GS draw), and whether windowed differs.
2. Mouse look and aiming: no use found for the right stick yet.
3. The game's own button glyphs and Japanese menu text remain as the PS2 has them.
