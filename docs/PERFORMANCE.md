# Performance

The goal: a steady 30 frames a second (the games' own rate) in every scene on a base Apple Silicon Mac (M1, 8 GB, its
integrated GPU), with headroom. The measuring machine is an M4 with 16 GB, shared with other work, so every number below says
the load average (`sysctl -n vm.loadavg`, the 5 minute figure) it was taken at, and CPU time per frame is given beside frames a
second.

## How it's measured

- **Frames a second**: the runtime's `--fps` log (headless; a window's v-sync isn't involved).
- **Where the CPU goes**: `/usr/bin/sample PID 10` on the running game (`pgrep -x biohazard-file2`), summed per thread into
  samples doing work (not waiting); a sample is a millisecond. The threads: the EE (the game's code, VIF1 and VU1), the render
  thread (the GS: GIF, local memory, the OpenGL renderer), the sound (SPU2 mixing on SDL's audio thread, or its own headless),
  the timer (vblanks), the main thread (the presenter and input).
- **The GS alone**: `--gs-record` / `--control record N FILE` and `--gs-replay FILE:PASSES`; the replay logs each pass's wall
  time and the render thread's CPU time. The median of passes 2 to N (the first warms the caches), in ms a vblank (the games
  draw a picture every other vblank). The same recording drawn by two builds gives the same picture hashes (`--snapshots`'
  replay.txt) when nothing that shows changed: every renderer change here was checked that way on all the recordings.
- Recordings: File 1's bar (60 vblanks), File 2's Training Ground and zoo (120 each), File 2's partner select (120).

## The profile (2026-10-20, M4, scale 3, before this round's changes)

| Scene | fps | EE thread | render thread | sound | notes |
|---|---|---|---|---|---|
| File 2 Training Ground, the bar, idle (load 4.5) | 30 | 22% busy (7.3 ms a frame): VU1 programs 17 and 14 most of it | 11% (3.8 ms) | 5% | |
| File 2 Wild Things, the zoo gate, idle (load 4.6) | 30 | **59% (19.6 ms a frame)**: VU1 program 14 20%, 17 9%, 12 2%, VU0 macro `vu0_madd` 3%, VIF unpack 2% | 42% (13.9 ms): **8 bit texture uploads 20%** (WriteImage, PixelAddress8) | 7% | the tightest scene: a base M1's cores are about 1.5x slower |
| File 2 Wild Things cutscene (load 4.4) | 30 | 9% | 7% + driver | 5% | |
| File 2 partner select (load 3.5) | **10-11** | light | **100%: 82% blocked submitting Metal command buffers** | | text drawn with the shader's blend: each glyph and its shadow overlap, ~310 copies of the target and render passes a frame |
| File 2 character select (the 3D models) | **7-8** | | | | seen in a scripted run; not profiled yet |

The GPU: the replays' wall time is close to their CPU time in the 3D scenes (the GPU keeps up), except the partner select's,
whose render thread waits on the driver: 27.6 ms a vblank of wall time for 14.7 of CPU.

### What the partner select showed about the OpenGL-on-Metal driver

Each render pass change is a command buffer submitted (`GLDContextRec::endRenderPass` → `flushContext` → `submitCommandBuffer`),
and the submit itself blocks: the partner select's replay takes the same time at scale 1 as at 3, 4 or 6 (14-19 vblanks a
second), so it's the number of command buffers, not pixels. About 170 µs each. `glTextureBarrierNV` (reading the target in place
instead of a copy) also ends the pass and submits: tried, every picture the same, but slower (16.6 vblanks a second against
23.8), so not kept. Fewer shader-blended batches is the only way within OpenGL.

### CPU a frame by thread (`--fps`'s CPU times, idle in play after 25 s, 20 s medians)

The `--fps` line gives each second's CPU time of the EE's thread, the render thread, the VU1 thread, the sound's and the
timer's (runtime cc107f7). Under load the CPU times grow too (threads land on the efficiency cores or slower clocks: the zoo's
EE thread measured 13 ms a frame at load 5 and 17 at load 25), so the comparisons below are back to back at a low load.

| Scene | EE | render | VU1 thread | sound |
|---|---|---|---|---|
| File 1 bar, before the VU1 thread (load 8-4) | 13.8 ms | 5.3 | - | 1.1 |
| File 1 bar, the VU1 thread (load 4) | 4.1 | 5.3 | 11.4 | 1.1 |
| File 2 zoo gate, before (load 5) | 13.2 | 5.4 | - | 1.0 |
| File 2 zoo gate, the VU1 thread (load 6-5) | 3.9 | 5.4 | 9.2 | 1.0 |
| File 2 Training Ground bar, before (load 12-5) | 6.5 | 2.5 | - | 1.0 |
| File 2 menus, the opening movie (IPU on the EE), the VU1 thread | 4-7 | 1.5-3 | 0.1 | 1.0 |

What the EE's thread did before the VU1 thread (sample of the zoo and File 1's bar): VU1's translated programs 55-59% (programs
14 and 17 most), the recompiled game code 17-19%, VU0 macro mode 6-7%, VIF1's unpacking 5-6%, the EFU 2-3%, the VU1
dispatcher 2-3%, `ps2_call`'s overlay lookups 2%.

## Changes

| Change | Measured | Before | After |
|---|---|---|---|
| 8 bit image transfers written a block's row at a time (runtime 1ae762a) | zoo replay, render thread CPU | 2.1 ms a vblank | 1.47 (load 10-12) |
| Text's region-clamped glyphs blended by GL's rounding (gsRoundedRegionBlends, runtime c062798, game b5e59c2 and f8435a5) | the selects' replays, wall ms a vblank | File 2 character 31, partner 29; File 1 character 67 | 2.1, 2.9; 2.1 (load 5-18) |
| | the selects live, fps | 7-22 | 30 |
| | pictures | | text edges at most 4/255 different on 3% of pixels; the 3D recordings identical |
| `--fps` logs the threads' CPU time (runtime cc107f7) | | | |
| VIF1 and VU1 on a thread of their own (vu1Thread, runtime 7d04c36, game 94d8577) | the 3D scenes' longest thread a frame | File 1 bar 13.8 ms (EE), zoo 13.2 | 11.4 (VU1), 9.2; the EE's 4.1 and 3.9 |
| | GS recordings | | the same pictures (the GS gets the same data in the same order by construction) |

| VU1 status flag's sticky bits kept only when a program reads them (runtime 26f1566, 6dc93a1) | File 1's bar with the zombies in, the VU1 thread a frame | 11.7 ms | 9.1 (load 4) |
| | PS2_VU1=verify | | 636,811 runs, 1.8 billion instructions, no differences |
| The frame threads under macOS's time constraint policy (runtime 178a660) | the zoo beside 20 / 40 busy processes (a compiler-like loop each), fps | 26-33 / 11-20 | 30 / 30 steady |
| | File 1's bar with the zombies in, beside 30 busy processes | | 30 steady |
| File 1: a missing function start (game a7db74b) | | the bar with the zombies in stopped after ~2 minutes ("not a function start" 0x66F430) | plays on |

### File 1's bar with the zombies in (the owner's 14 frames a second)

Reached by `to_zbar` (the bar, the intro skipped, walk to the door: the zombies' cutscene, then play). On the M4 alone it holds
30 (EE 4.5 ms a frame, VU1 thread 9.1, render 6.8; its GS replay 2.2 ms a vblank), headless, with the presenter's path forced
on, and with the English patch. Beside 20 busy processes it fell to 13-21, beside 40 to 11-20: the owner's 14 came with three
agents building on the same Mac. The time constraint policy keeps it at 30 under that load. The depth buffer fix merged from
`biohazard` (a 32 bit depth cleared to 0) costs nothing: the replays' times are the same or lower.

## Looked at and not kept

- `glTextureBarrierNV` in place of the destination copies (the shader reads the frame itself): the same pictures, but the driver
  submits a command buffer at each barrier too: the partner select's replay went from 23.8 to 16.6 vblanks a second.
- The recompiled game code at -O2 instead of -O1: the zoo's EE thread 13.3 ms a frame against 13.2 (no difference; the build
  takes 1.5 minutes longer). The game's own code is under a fifth of the EE's time.
- FXAA (the lead's report of 5-6 frames a second with `anti_aliasing = 1`): headless runs never draw it (the presenter returns
  before drawing when headless); the Training Ground ran at 30 with it, and at 30 with scale 5 and trilinear filtering. The
  replays at scale 3, 4, 5 and 8 took 2.8, 3.2, 4.9 and 11.8 ms a vblank of GPU (Training Ground; filtering no different), so
  scale 8 (4x with 2x supersampling) is 23.5 ms a frame on the M4's GPU. The slow runs were most likely scripted runs left on
  the partner or character select (10-11 and 7-8 frames a second then), which the text change fixed.
