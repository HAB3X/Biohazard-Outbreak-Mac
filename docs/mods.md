# The community mods

Ports of community mods for Resident Evil Outbreak into these native builds, with their authors' permission (MIT): who made
what is in [CREDITS.md](CREDITS.md). They were written as mods of other programs (Snippy's for PCSX2, RaccoonRecomp's for
Resident Evil Outbreak Recompiled's mod host on the US disc); here each is native code of the program, its switches rows of the
PC menus (F10).

In a static recompilation writing the game's code in memory does nothing (the code that runs is the compiled one), so every
code patch of a mod is a native override of the game function it patched (`config/fileN.toml`'s `overrides`), which runs the
game's own function as recompiled and does what the patch did while its switch is on. Data writes are made to the game's
memory at the addresses of the Japanese discs.

## The Modern camera (File 1)

`src/modern_camera.cpp` (the driver), `modern_camera_config.cpp` (the Camera tab), `modern_camera_game.cpp` (the game's
memory), `modern_camera_rig.cpp` (the rig, the follow heading, the walls; tested by `tests/modern_camera_rig_test.cpp`).
Adapted from RaccoonRecomp's REO-ModernCam 2.1.1 (its driver.c, game.c, rig.c, collision.c, config.c), itself a recreation
of Snippy's Outbreak ModernCam. ModernCam was written for this very disc; REO-ModernCam moved its addresses to the US disc and
noted Snippy's Japanese ones beside them. Snippy's own address maps (ro_addresses.py for File 1, ro_addresses_f2.py for
File 2) are in his ModernCam 1.2.0 release: read from it without running it.

**Switched** in the PC menus' Camera tab: View, Classic (the game's fixed cameras) or Modern. The tab has the framing
(distance, shoulder, height, field of view, the aim's distance and field of view), following, the look (mouse and stick
sensitivity, invert), aiming (Modern aim, free gun elevation, lock on enemy, aim sensitivity, the crosshair's style, size and
colour) and the room (collision, matching the room's cameras, hidden objects, the character light). The values it doesn't
offer keep ModernCam's File 1 defaults (`modern_camera_config.h`).

**Controls.** The game's own in play: the right mouse button (R1) aims, the left fires, WASD walk her relative to the camera
(the game's stick walking is relative to the camera block, which is this camera). The mouse turns the camera while it drives
(the runtime's `PS2_MOUSE_LOOK`: the pointer captured, its motion the camera's); with a gamepad the right stick does, and the
game then sees the right stick centred (it's the appeals' stick: turning would say lines). Swap shoulder is R3: the middle
mouse button or X (`camera_shoulder` in Controls), taken from the game's pad while the camera drives.

**How it holds the game's camera** (each override runs the game's function unless the camera drives):

| Override | Function (File 1) | What it does while the camera drives | ModernCam's patch it stands for |
|---|---|---|---|
| `mods_frame_end` | exe 0x19ECB0, the frame's end (the vblank wait and flip, called by 0x1B9140 before it stores the frame count 0x3065C4) | the game's, then the camera's frame: computed and written to the camera block | REO's frame mark (US 0x19F6C0) |
| `mods_pad_map` | exe 0x1AFC60, the button mapper (a0 a character's record) | for her record: the mapped record's sticks centred as needed, R3 taken, her walk hold let go and the stick turned where the game would step her back; then the game's | the VBTN hook at 0x1AFD38 (`lw t4,0(a2)`): nothing before it in the mapper changes the record |
| `game_camera_fixed_cut` | GAME.BIN 0x58EC70, the cut dispatcher's handler 1 (called only from 0x58EAEC) | skipped | the call NOP'd at 0x58EAEC |
| `game_camera_rail_cut`, `_spline_cut` | 0x58ED80 (handler 3, from 0x58EB0C) and 0x58F230 (handler 2, from 0x58EAFC) | run, the camera block 0x306320-0x30638F put back after (the camera init 0x345480 they call writes that block; the dispatcher's camera apply rebuilds the projection from it after them) | REO's narrowed freeze (the init call and the block's stores); ModernCam NOPs the calls |
| `game_camera_follow` | 0x5A9080, the follow handler (doors; reached by a callback pointer) | skipped | `jr ra` at its entry |
| `game_gun_elevation` | 0x556800 (a0 a record), whose player branch sets the gun's elevation +0xBC8 by the servo 0x346BB0 and four stores | with free aim, for her record: +0xBC8 put back as the camera set it | the servo calls (0x556968, 0x556980, 0x556998) and stores (0x5569CC, 0x5569D8, 0x5569E4, 0x5569E8) NOP'd |

The dispatcher (0x58E7B0) calls the camera apply 0x345740 every frame after its handlers: the projection from the field of
view 0x306380 and the view from the eye and target, so the camera written at the frame's end shows in the next frame's
picture. The words at the patch sites are Snippy's (ro_code_orig.json, ro_code_orig_f1_freeaim.json) and were checked on the
disc.

**Where it hands back.** It drives only in play: the game's loop task (0x341B80) without the cutscene task (0x341AF0) and the
game overlay not in its opening credits (0x48BF60 = 14) (the PC controls' layout, `BiohazardControls::File1`). In a cutscene
the game's camera is untouched (seen: the bar's zombie entrance plays with its own letterboxed camera; play resumes behind
her). The status screen over play keeps the view.

**Addresses** (File 1, `modern_camera_game.cpp`), Snippy's unless said, each checked:

| What | Address | Checked |
|---|---|---|
| eye, target, near value, far plane, field of view | 0x306338, 0x306344, 0x306374, 0x306378, 0x306380 | the fixed handler's stores (0x58EC94..0x58ED60); in play: eye (8505, 197, 12764), field of view 40 |
| aspect (4/3) | 0x239D48 | 1.3333 in memory |
| camera block written by the init | 0x306320-0x30638F | the init 0x345480's stores |
| the character light | 0x306298, mode 0x3062BC | Snippy's |
| frame count | 0x3065C4 | 0x1B9184 stores it; counts vblanks in play |
| control type, button map | 0x3065BC, 0x2168C0 | the mapper's loads (0x1AFD34, 0x1AFD08); type A's row is square 0x100, cross 0x10, circle 0x20 |
| pad records | 0x306410 (+8 held) | the mapper's 0x306418 + 48 × index; R3 held sets 0x2000 there |
| raw pad frames | 0x258698 → 0x292B80 | Snippy's RAW_PAD 0x292B9C is its +0x1C |
| characters | 0x476DD0, 0x10E0 each | the partners' AI's; the local player's id 0x48BF7D (the mapper's compare at 0x1AFC8C), record 0 offline |
| aim target (+0x24) | | the aim stance's entry 0x61CEBC stores the target finder 0x62D6A0's result there and sets the stance 2 |
| enemies | 0x3FFCA0, 0xD90 each | the partners' AI's |
| cuts | 0x3AEF50 | 10 cuts in the bar, the current 3 |
| objects: count, flags, matrices | 0x470902, 0x470930, 0x470BB0 | the room's draw data 0x470090 (US 0x49EEF0 - 0x2EE60): 96 objects in the bar |
| walls | 0x3938C4 → 0x925730 in the bar | Snippy's WALL_PTRS file1 |
| near-camera hide radii | 0x6D6CF0 | 20, 80, 180, 300 in memory |

**Not ported** (the reasons): the camera's own Aim, Shoot, Reload, Sprint and Special buttons (the game's controls here already
aim with R1, fire, reload with L1 and run); its reload call (the game's L1 reloads); the reveal camera and Watch next
player (the last needs online play); the J's Bar restroom mirrors and light-bar
glow cards (drawn by calling the game's drawing functions from inside the room's draw loop, a mark mid-function that has no
override here); the swap of shoulder written back into the settings (it lasts the session).

**Added since:** Recenter (L3, C: the camera swings behind her over 0.3 s, the port's `recenterTimer`) and the Alt view (L3
and R3 at once, V: the Alt rig's distance, height and field of view, each the main one's unless set, in the main rig's place
until pressed again), as REO-ModernCam has them; and its camera window: F8 opens the Camera tab over the running game (the
runtime's `Ps2GameInfo::liveTab`).

**Checked** (File 1, headless, Metal 4x, J's Bar): the view behind Kevin; the mouse turning it; W walking her away from the
camera and S toward it (she turns to the way she walks); the right mouse aiming (stance 2, the gun's elevation following the
mouse's pitch), the crosshair drawn while she aims (the window's picture); swap shoulder (X); the status screen over the view;
the zombies' entrance cutscene with the game's own camera and the camera back behind her after it; the walls pulling the
camera in as it turns; 30 frames a second throughout (60 s with the zombies in).

## First person (File 1)

`src/first_person.cpp`, adapted from REO-FirstPerson 2.6.8 by RaccoonRecomp (MIT): View: First person. The same overrides hold
the game's camera; the mouse (or a gamepad's right stick) turns the view. Its restroom mirrors and On / Off key aren't ported.
Checked: the bar from Kevin's eyes, the mouse turning the view, walking, aiming along the 45 Auto's sights.

## The cheats (File 1)

`src/cheats.cpp`: the regular and Greatest Hits lists merged (duplicates by effect folded into one row with every author
credited). Code patches are overrides: a round used (GAME.BIN 0x581A00), an item used (0x581B30), the gauge over time
(0x6359D0) and from a bite (0x62E6A0), damage to an enemy (0x60F0C0) and to a character (0x62F130), the death check
(0x62EE10), the single-play check (0x1AF4D0, at the death's call site only, by its return address 0x62E374), the movement
routine (0x62CA30, from the zombie update only: return 0x6644B8), the event timers (0x65A8C0), the collision (0x631CA0),
DEMO.BIN's collection check (0x554EC0) and scenario select readers. Addresses: records and submain's bss are the US disc's
- 0x2EE60 (the timers and files - 0x2EE70), the save block - 0x1DE00 (0x301D60), each checked in code or memory.
Not ported: the master codes (device hooks), item slot codes, Number Of Players (no address found), the codes Raccoon marks
as not working.

## File 2

Matched by masking addresses and field offsets in File 1's functions and aligning them with File 2's: frame 0x19F390, mapper
0x1B08B0, handlers 0x595410/0x595520/0x5959C0, follow 0x5AA6C0, gun 0x557B30, ammo 0x586500, item 0x5867A0, gauge 0x63B1F0,
bite 0x633950, enemy damage 0x615A40, character damage 0x634720, death check 0x634350, death path's single-play return 0x6335B8,
movement 0x6318E0 (zombie update return 0x674EB0), timers 0x66BC20, collision 0x637630; fields in src/modern_camera_game.cpp's
File2Fields and src/cheats.cpp's File2. The single-play check is 0x1AFB10 (File 1's 0x1AF4D0's code over the play mode at
0x3137D2; the death path calls it from 0x6335B0). Installed in src/file2.cpp; what's been seen in play is in STATUS.md.
