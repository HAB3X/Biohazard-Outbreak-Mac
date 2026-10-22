# Handoff: everything left to do

A work list for an AI picking up the Biohazard Outbreak Mac port. Read **Part A** completely before touching anything. Part B
is the task list: each task has a goal, where to start, and when it counts as done.

---

## Part A: context and rules

### The project

- Native Mac ports of the Japanese **Biohazard Outbreak File 1** (SLPM-65428) and **File 2** (SLPM-65692 v1.03). The PS2 code
  is statically recompiled to native code, not emulated.
- **Game repo:** `~/Documents/biohazard-outbreak-mac`, branch `main`, GitHub `HAB3X/Biohazard-Outbreak-Mac`.
- **Runtime:** work happens in `~/Documents/woc-agents/biohazard/ps2-native-runtime`, branch `biohazard`. This is what
  `build/CMakeCache.txt`'s PS2_RUNTIME_DIR points to. It is shared with the Wrath of Cortex and Tag Team Racing ports, so keep
  changes generic and opt-in through `Ps2GameInfo` fields.
- The game repo carries a copy of the runtime in `ps2-native-runtime/`. After committing runtime changes, sync it:
  ```sh
  rm -rf /tmp/rt && mkdir /tmp/rt
  git -C ~/Documents/woc-agents/biohazard/ps2-native-runtime archive biohazard | tar -x -C /tmp/rt
  rsync -rci /tmp/rt/ ~/Documents/biohazard-outbreak-mac/ps2-native-runtime/
  ```
  Then commit that copy in the game repo.
- **Read first:**
  - `STATUS.md`;
  - `README.md`;
  - `KNOWN BUGS.md`;
  - `docs/mods.md`;
  - `docs/controls.md`;
  - `docs/translation.md`;
  - `docs/RELEASING.md`;
  - `docs/CREDITS.md`;
  - `git log --oneline -40`.

### Rules (the owner's, non-negotiable)

1. **No AI attribution anywhere.** No `Co-Authored-By`, no mention of AI or agents in commits, docs or code. Commit as
   `git -c user.name=HAB3X -c user.email=bombman65@gmail.com commit ...`. The work reads as the owner's alone.
2. **Agents:** at most 3 at once, counting every level. Sub-agents must not spawn further agents.
3. **Coding standard:** follow `~/.claude/skills/perfect-code/SKILL.md`. In particular:
   - search before writing;
   - make the smallest complete change;
   - fix root causes, and label anything that's only containment;
   - fail loudly;
   - never weaken tests;
   - verify for real and report honestly;
   - match the surrounding code's comment style, which is plain prose saying why.
4. **GPL projects** (for example `reo` by sp00nznet) are reference only. Write your own code. RaccoonRecomp's and Snippy's mods
   are MIT and may be adapted; keep their notices and credit them in `docs/CREDITS.md`.
5. **Ask the owner before downloading anything.** Give the file name, source and size.
6. **Never permanently delete files.** Move them to the Trash. Never empty the Trash.
7. **Credit every mod and cheat author.**
8. **Releases:** update the existing release **v0.1.2** in place (`gh release upload v0.1.2 ... --clobber`). Never create a
   new release. The Desktop apps and GitHub must hold the same build.
9. **Don't touch the owner's settings files**
   (`~/Library/Application Support/PS2 Native Runtime/SLPM_654.28` and `SLPM_656.92`, and the launcher's folder) except
   through the launcher's own features.
   - Test with temporary folders: `--settings DIR --memcards DIR`.
   - For the launcher, use a temporary home: `HOME=/tmp/x CFFIXED_USER_HOME=/tmp/x`. A real home triggers macOS privacy
     prompts.
10. **Flag behaviour changes before shipping them,** such as a count that changes or a default that flips.
11. **Keep docs current:** update `STATUS.md`, `README.md` and `KNOWN BUGS.md` in the same commit as the change. Keep the
    README short and plain.

### Building and running

```sh
cd ~/Documents/biohazard-outbreak-mac
cmake --build build                    # both games (recompiles changed config/overrides)
./run.sh file2 --english               # play
python3 tools/make_app.py both         # Desktop apps (this Mac)
python3 tools/make_app.py launcher     # Desktop launcher (embeds the Desktop apps)
python3 tools/make_app.py file1 --release --disc "$F1"   # release zips (F1/F2 from build/CMakeCache.txt FILE1_DISC/FILE2_DISC)
python3 tools/make_app.py file2 --release --disc "$F2"
python3 tools/make_app.py launcher --release
```

- Check every step's exit status. Don't hide a failure behind `| tail`.
- The release build runs a first-launch check. If it fails on timing, retry once, then investigate.

### Testing tools

- **`tools/ai_scenario.py`:** `Run(game, folder, env, args=())` starts a headless game with a `--control` FIFO. Helpers:
  - `to_bar(run)`: File 1, Kevin in J's Bar;
  - `to_zoo(run)`: File 2, the zoo;
  - `to_title_menu(run)`.

  Put test options in `folder/card/options.txt` (for example `Camera: View = Modern`) and display options in
  `card/graphics.txt` (start that file with `version = 2`).
- **Control FIFO commands** (see the runtime's `runtime/src/control.cpp`):
  - `key NAME HOLD` and `tap NAME`;
  - `mouse X Y` and `wheel`;
  - `stick X Y`;
  - `snap NAME`: the game's 4:3 frame;
  - `window NAME`: the window as the player sees it, with the PC menus, picture adjustments and widescreen;
  - `peek` and `poke`.
- **Other options:**
  - `--ui "T:keys,shot"` scripts the PC menus.
  - `--snapshot-at 5,10,...` takes timed boot pictures.
  - `--log io|input|...` turns on logging.
- **Look at every picture you take.** "Looks done" isn't done.

### How game changes are made

- Code patches are **native overrides** of recompiled functions:
  - executable functions go in `config/fileN.toml` `[overrides]`;
  - overlay functions go in each `[[overlays]]` block's single `overrides = {...}` line. A second `overrides` key in the same
    block is a TOML error, so merge into the existing line.

  The override function lives in `src/fileN.cpp`, usually through `MOD_OVERRIDE(name, call, original)` from `src/mods.h`.
- **PC menu rows** come from `ModSettings::Add` (`src/mod_settings.h`). Add them before `Mods::Install` collects them in
  `main`.
- **Overlays share load addresses.** In File 2 the `demo` and `game` overlays both load at 0x543000, so name functions by
  overlay (`demo_0054f250`, `game_00636040`).
- **Disc patches:** `--patch FILE` lays byte ranges over the disc, and may be given more than once (later patches over
  earlier ones). The format and writer are in `tools/bh_patchfile.py`. Disc archives can be read with `tools/bh_archive.py`.

### Facts found so far (don't rediscover them)

- **Character models:**
  - They live in `NETBIO00.DAT/romdata.afs` as `Pcc_oo.nbd`.
  - They are swapped in place by `tools/bh_character_patch.py`.
  - **Changing an AFS table entry's size crashes File 2 at boot** (2 s in, in f_001d8e00). Rewriting an unchanged entry is
    harmless.
  - AFS offsets are 32-bit from the archive's start.
  - Free disc space within reach: File 1 has 483 MB after NETBIO02.DAT; File 2 has none.
- **The start-up sequences' START check:** File 2 demo 0x54F250, File 1 demo 0x540CB0. It sets the task's flag at +92
  (`src/intro.cpp`).
- **Modern camera:**
  - `src/modern_camera*.cpp`, documented in `docs/mods.md`;
  - Recenter is L3 (C), Alt view is L3+R3 (V), swap shoulder is R3;
  - F8 opens the runtime's `liveTab`.
- **Widescreen** is in `src/widescreen.cpp` (the SetPerspective override) and the runtime's `hw/gs/gswide.cpp`.
- **Display look settings** (brightness, gamma, colour filters, Reduce Flashing, scaling) are in the runtime's `presentmetal.mm`
  and `presentgl.cpp`. Every change needs testing on both, with `renderer = hardware` for OpenGL. Note that `filter` is a
  reserved word in GLSL.
- **Launcher:** a SwiftUI package in `launcher/`, built by `tools/make_app.py launcher`.
  - Test hooks: `BH_LAUNCHER_OPEN` (settings, mods, characters, discs) and `BH_LAUNCHER_PAGE` (a settings tab, for example
    Saves).
  - Capture its window by process ID with `screencapture -l`.

### Report format at the end of each task

```
Changed:      what and why
Verified:     commands run and what was actually seen
Not verified: what wasn't checked, and why
Watch out:    surprises, behaviour changes, follow-ups
```

---

## Part B: the tasks

Effort: S = hours, M = a day or two, L = bigger. **[OWNER]** marks things only the owner can do. Ask them; don't guess.

### 0. Owner decisions and checks [OWNER]

- **0.1** Should F8 (live camera panel) and F10 (full settings) merge? The proposal is one F10 menu whose Camera tab becomes
  the live panel, with the game unpaused, and F8 as a shortcut to it. Wait for the answer before changing anything.
- **0.2** These need a real screen, so ask the owner:
  - exclusive fullscreen;
  - switching widescreen on and off mid-game from the menu;
  - the flicker fix;
  - Gatekeeper's "Open Anyway" on a downloaded copy on another Mac.
- **0.3** Ask the owner for a log of starting a scenario from an online room (needed for task 1).

### 1. Online (obsrv.org)

- **1** Starting a scenario from an online room. Start from `docs/network.md`, `src/network.cpp`, `src/menus_lobby.cpp` and
  the owner's log. Done when a room starts a scenario with another player, as the owner confirms.
- **2** The game server (port 8590), handle names and chat, all untried since the last fix. Done when each works in a real
  session.
- **3** The mouse in the lobby screens. Done when every lobby screen can be used by mouse alone.
- **4** Turn the Modern camera and first person off in online play, as is already done for the cheats (`src/cheats.cpp`'s
  gating). Done when an online room forces the Classic camera and the Camera tab says why.

### 2. RaccoonRecomp's remaining features

- **5** J's Bar restroom mirrors with the Modern camera (File 1).
  - Reference: `~/Downloads/Resident Evil Outbreak/Community Projects/REO-ModernCam-main/src/game.c`, the `mirror_*`
    functions (MIT, US addresses).
  - Our port needs an override inside the room's draw loop (see `docs/mods.md`'s "Not ported").
  - Done when the mirrors reflect the room from any camera angle.
- **6** Ultrawide (21:9, 32:9) and a choice of HUD placement (centred, or stretched to the edges).
  - The widescreen factor is fixed at 4/3: generalise `Widescreen::SetProjection` and the runtime's `gswide.cpp`.
  - Done when 21:9 shows undistorted 3D with the HUD where chosen, in both games.
- **7** A launcher background video made from the player's own disc. No game data may ship.
  - Option: run the game headless through the title movie and encode the frames with AVFoundation, as
    `Games.capturePicture` already does for the still picture.
  - Done when the launcher plays a short loop behind its menu, made on first run.
- **8** A 60 fps mode, experimental and off by default. Find the game's frame pacing and check that animation and physics
  don't double in speed. Done when it's selectable, with known issues listed.
- **9** MSAA (2×/4×/8×) and Auto internal resolution (matched to the window), in both presenters and renderers.
- **10** General file-replacement mods: any disc file or AFS member replaced from a mod folder, not just costumes.
  - Extend `tools/bh_character_patch.py` (same size limits).
  - Done when a mod folder with a `files/` tree is applied at launch and the launcher lists what it replaces.

### 3. Mods

- **11** Character models bigger than their costume. Either find why changing the AFS table's size crashes File 2 at boot
  (f_001d8e00), or shrink the model by recompressing its TEX chunk (`tools/bh_sld.py`; check whether it can compress). Done
  when the owner's two oversized mods load.
- **12** HD texture packs: pale patches on trees in File 2's zoo. See the runtime's `hw/hw/replace.cpp` (alpha conversion).
- **13** File 2, first person: a pale rectangle at the left of the view in the zoo's first street (`src/first_person.cpp`).
- **14** Cheats not yet seen working in play, from `src/cheats.cpp`:
  - zombies don't move;
  - escape time;
  - walk through walls;
  - items;
  - virus never rises;
  - L1+Select;
  - zombie after death;
  - collection opened;
  - partners;
  - Player 1 modifiers.

  Test each in play and fix what fails. Record the results in `docs/mods.md`.
- **15** Port the item-slot cheat codes and Number of Players (no address found yet).

### 4. Menus and controls

- **16** File 2's collection: clicking a tab picks the wrong one (`src/menus_collection.cpp`).
- **17** File 1's difficulty menu only moves one step with the keys.
- **18** Scroll and zoom the map with the mouse.
- **19** Sweep the remaining mouse bugs across every menu in both games, and list what was found.

### 5. Game content

- **20** File 1's translation: finish the pass (`docs/translation.md`, `translations/file1/`). The 678 Japanese lines kept on
  purpose stay.
- **21** Check every Sofdec movie: the endings, the continue screen, `PS2demo01/02` and the other 18 or so. Done when each
  plays correctly or has a bug filed.
- **22** Compare sound against real hardware or PCSX2: effects, music, the mix and reverb.
- **23** Crash sweep: play every scenario in both games, offline, to the end. File and fix crashes.

### 6. Performance

- **24** Loading times: measure them, then reduce them (see docs/TEST_REPORT.md).
- **25** A steady 30 fps in the heaviest 3D scenes (the zoo with zombies). That needs fewer copies to the frame's
  destination in the Metal renderer.
- **26** Measure speed on slower Macs such as a base M1, and record it in `docs/PERFORMANCE.md`.

### 7. Launcher and app

- **27** Auto-update: check GitHub releases, show what's new, and update in place.
- **28** Import saves from PCSX2 (`.ps2` card images use the same layout, so copy them into the profile's folder) or from a
  real memory card dump.
- **29** Launcher music. It must come from the player's disc, the way the title picture does.
- **30** Merge the rumble branch (`biohazard-polish` in the runtime) and test it with a real controller [OWNER for the
  controller test].

### 8. Quality of life

- **31** (M) Skip or shorten door-opening animations, as an option.
- **32** (M) Quick save and load anywhere, offline only: snapshot the whole machine state (EE, IOP, GS and memory), like
  PCSX2 save states.
- **33** (S) Always show the scenario timer and the virus gauge as a percentage. File 2's meter is hard to read.
- **34** (S) A "really paused" option. Offline, the status screen doesn't stop the clock.
- **35** (M) Number keys to use or equip inventory slots directly.
- **36** (S) Partner commands: one toggle key per partner for Follow, Stay and Hold position.
- **37** (S–M) Subtitle size and a dark box behind subtitles.
- **38** (M) A corner mini-map using the game's own map data.
- **39** (S) Hold to skip cutscenes, with a "hold any button to skip" prompt. The runtime's `ps2_show_prompt` and
  `ps2_any_input_held` exist for this.
- **40** (S) Remember the last scenario and character on the select screens.
- **41** (S) An unlock-everything toggle for the collection, costumes and scenarios.
- **42** (M) Difficulty sliders for enemy damage, item amounts and virus speed.

### 9. Features from other Resident Evil games

- **43** (S) A one-key quick turn, as in the RE2 and RE3 remakes.
- **44** (M) A laser sight or aim dot while aiming with the classic fixed cameras (RE4 PC style). `ps2_set_crosshair` exists.
- **45** (M) Optional damage numbers and enemy health bars.
- **46** (M) An auto-sort button for the inventory and item box.
- **47** (M) Map markers for items picked and not yet picked, as in the RE2 remake.
- **48** (S–M) Assisted aim, or snapping to targets for the mouse.
- **49** (L) Photo mode: pause, free camera, HUD hidden. Reuse the Modern camera's driver.
- **50** (M) End-of-scenario records: clear times, kills and a rank, kept in a file, with a screen in the launcher.
- **51** (L) A Mercenaries-style score attack: a timer and kill-streak score on any scenario.

### 10. Features from PC ports and recompilation projects

- **52** (L) Rewind, offline only (builds on 32).
- **53** (S–M) A fast-forward key: hold to run the game at 2–3×.
- **54** (M) A speedrun timer from in-game time, and a LiveSplit autosplitter hook.
- **55** (M–L) Upscaled movies loaded from the mods folder (an `.sfd` replacement or a native video swap).
- **56** (M) Gyro aim on DualSense and Switch Pro controllers (SDL3 sensors).
- **57** (M) DualSense haptics and adaptive triggers (builds on 30).
- **58** (S) Discord Rich Presence: game, scenario and difficulty.
- **59** (S) A screenshot key that saves the window, with or without the HUD, to Pictures.
- **60** (S) Hold a key to show every binding as an overlay.
- **61** (M) An obsrv.org server browser and player count in the launcher.

### 11. Accessibility

- **62** (S) Toggle everything: every held control can latch (extend "Hold to toggle" beyond run and aim).
- **63** (M) High-contrast mode: outline enemies and pickups in solid colours.
- **64** (M) Audio cues near pickups and doors, for low-vision players.
- **65** (M) Text-to-speech for menus and files, using macOS's speech API.

### 12. Housekeeping

- **66** Rewrite the commit history to remove any AI traces. Do it only when no agents are running, and **confirm with the
  owner first**: it rewrites published history and needs a force push.
- **67** Merge the runtime's `biohazard` branch into the shared runtime's `main`, then rebuild and re-test the Wrath of Cortex
  and Tag Team Racing ports on it.

### Suggested order

0.1 → 1–4 (online) → 23 (crash sweep) → 16–19 (menus) → the small quick wins 31, 43, 37, 53, 39, 58 → everything else.
Release (update v0.1.2 in place) after each finished group, not after every task.
