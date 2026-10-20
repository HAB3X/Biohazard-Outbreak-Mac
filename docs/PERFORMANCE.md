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

## Changes

| Change | Measured | Before | After |
|---|---|---|---|
| 8 bit image transfers written a block's row at a time (runtime 1ae762a) | zoo replay, render thread CPU | 2.1 ms a vblank | 1.47 (load 10-12) |
