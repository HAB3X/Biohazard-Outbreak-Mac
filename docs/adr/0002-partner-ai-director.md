# 2. The partners' AI: a native director over the game's own AI

Date: 2026-10-21. Status: accepted.

## Context

The owner wants offline partners that behave like a competent modern partner (RE5's Sheva): they fight what threatens the
player, stay with the player and don't get stuck, heal and share sensibly, and obey the ad-lib commands. Online play has
human players and must not change.

What the games do (docs/research/partner-ai.md has the addresses and the evidence):

- The partners' behaviour is code in SINGLE.BIN, the single-play overlay; network play loads NETAQ.BIN at the same address
  instead. The characters' update calls SINGLE's **think** for each partner, in single play only.
- The think drives the partner through a **virtual pad** in its record (the same buttons the player's pad gives the
  player's character), from a layered AI: a perception (enemies, the player's commands, its wants), a planner that picks one
  of 16 plans by the highest of eight wants (follow the player, wait, wander to a random node, fight, help the player up, use
  or swap items...), route following over each room's node graph (doors included), and per-enemy-kind attack routines (aim,
  fire, reload, melee) chosen by the partner's state.
- Measured (tools/ai_scenario.py, File 1's bar with the player standing still): its weak points are decisions, not skills.
  Mark's wants send him wandering to random nodes (into the next room and back, 41% of the time away from the player in
  the first measurement), his first shot came seconds to minutes after the zombies reached the player, the fight plan
  first walks to a random node of the room, and a partner with a gun in the pack fights with what's in hand (nothing, or a
  stick it picked up).

## Decision

A native **director** decides what each partner should be doing, and puts that into the game's own AI through its own
seams, so the game's skills (routes and doors, aiming and firing, reloading, swapping weapons, helping the player up,
Cindy's herbs) carry it out:

- The think's perception (File 1 0x822520, File 2 0x7E9AD0) is overridden (config's `single` overlay): the game's runs,
  then `src/partner_ai_brain.cpp` decides from a plain snapshot of the world (`src/partner_ai_world.cpp`: characters,
  enemies, items in hand and carried) and the player's commands: **fight** an enemy (the one on the player first, then those
  after the player, then those after the partner; only with a loaded gun unless the enemy is on the player), **follow**
  the player (back when farther than 700, until within 300; 260/150 after "This way!" or a call by name), **guard** (stand
  by the player facing them while enemies are in the room), or leave it to the game (**free**: nothing about).
- `src/partner_ai.cpp` applies it: the wants set so the planner picks the matching plan (follow 3, wait 1, fight 5), the plan
  cut short through the game's own plan starter when another runs (never the item, door, help or heal plans), the fight
  plan started at its attack step against the chosen enemy (without its walk to a random node), and the game's item check
  asked to take out a loaded gun carried when enemies are about.
- The decision logic is pure and tested (`tests/partner_ai_brain_test.cpp`, ctest); its numbers are in one place
  (`Tunables`).
- `BH_AI_LOG` telemetry and `tools/ai_scenario.py` / `tools/ai_report.py` measure both AIs the same way. The game is
  deterministic for the same input, so the same seed in both modes is the same game until the AIs differ.
- The pad is taken over only where the game's skills are too slow or unsafe: shooting an enemy within 500 of the player
  (turned to it with the weapon down, readied, fired in the aiming stance; given back to the game after 6 presses that
  don't hurt it), and a partner without a gun turning away and running from an enemy within 280 that isn't on the player.
- The PC menus' **Gameplay: Partner AI** setting chooses **New** (the default) or **Original** (the game's own, untouched:
  the overrides run the game's functions and add nothing).

Offline only by construction: the overrides are SINGLE.BIN's functions, which only single play loads and calls (network play
runs NETAQ.BIN's code at those addresses, and the characters' update calls the think only with the play mode at 0); no
network code is touched.

## The partners leave the puzzles to the player

The owner's rule: the AI may not be dumb, but it must not solve the scenarios' puzzles or move them on for the player. What
the game's own AI can do to the world (docs/research/partner-ai.md, "What a partner can do to the world") is: go through
doors that aren't locked (its route code turns back at locked ones), pick up items lying in the room, use, combine (reload),
swap and give the items it carries, help the player up, and Cindy's healing. It never operates a device, enters a code or
uses an item on the world: none of SINGLE.BIN's calls into the game do, and it presses the action button only to fire.
Picking up is the one way in: the game's partners pick up anything they have room for, keys and event items included. With
this module's AI the item chooser (File 1 0x833250, File 2 0x7E3AA0) is overridden too: an item that isn't a weapon,
ammunition or a recovery item is left where it lies, so the partners never carry what the player needs for the scenario.
The director's own decisions are following, guarding, fighting and asking the game's item check (its picks filtered as
above); the only button it presses is the attack, with the weapon readied (tests/partner_ai_brain_test.cpp checks both, and
the telemetry logs every pick-up the chooser makes, allowed or refused).

## Options not taken

- **A native AI that writes the virtual pad itself, the game's think replaced.** The cleanest separation, but it means
  re-making every skill the game already has: routes over each room's node graph and through doors, item pickup and use,
  weapon swaps, the per-enemy attack routines, helping the player up, healing, the speech replies. Months of work to reach
  the game's own level before improving on it, for both games. Kept open for single skills where the game's is poor (the
  think override can rewrite the pad after the game's think).
- **Rewriting the game's decision functions one by one in native code.** They share a large work area (+0xFF0..+0x10E0 of
  the record) with dozens of fields whose meaning isn't all known, and File 2's are a different version of the same code
  (more conditions, moved fields), so each rewrite would be two and fragile. Steering their inputs (wants, plan, target) is
  a much smaller surface.
- **Pokes from outside (a vblank hook writing the records).** Unsynchronised with the think and the plan step, and against
  the project's rule of integrating through the game's own code.

## Consequences

- The partners act with the game's own skills, so where those are weak (an attack routine's own waits, its aim), the
  director can only choose better; going further means taking over the pad in those cases (the think override is the
  place).
- Each game needs its field offsets and function addresses (partner_ai.cpp's two Layouts, each value with its evidence);
  the decision logic is shared.
- The Original setting is the measured baseline and a fallback if a scenario shows the director doing worse.
