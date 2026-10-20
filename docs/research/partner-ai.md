# The partners' AI: what's known

Goal (the owner's): partners that fight usefully, follow and don't get stuck, heal and share items sensibly, and obey the
ad-lib commands (Go!, This way!, Help me!, Thanks/Sorry; F with 1-4 calls one by name).

## Found so far (File 1, 2026-10-20)

- No AI data on the disc: romdata.afs has the characters' (pl*, pc_*), enemies' (E*, e*, enemy*) and stage data, nothing
  that reads as behaviour tables. The partners' behaviour is code, in PROG/GAME.BIN (the game overlay, 0x540000).
- The pads: dbcman's socket 0 receives into 0x292B80 and 0x292C00 (`--log iop`: "socket 0 made ... buffers"); the SDK's
  per-pad state is a table at 0x292D60, 24 bytes a pad, set up by the executable's f_001A51A0.
- The ad-lib commands are the right stick's four directions (src/controls.cpp's appeal keys), so the game sees a command as
  the player's right stick.
- Not yet known: whether a partner is driven through a virtual pad (the way the online mode drives the other players'
  characters would suggest it, but that's unchecked), where the characters' records are (positions aren't vec4 floats with
  w = 1; RE games of this age keep them as integers), and the per-character update's player/CPU branch.

## Approach

Find the per-character update and its CPU branch; if partners read a virtual pad, a native AI can fill that pad from the
game's state (characters, enemies, health, items, commands) and leave the game's code as it is; otherwise override the
decision functions one by one. Offline only: online play has no AI partners.
