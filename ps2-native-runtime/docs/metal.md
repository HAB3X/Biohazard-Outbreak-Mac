# The Metal renderer

The GS drawn by Metal instead of OpenGL, on the Mac's own graphics interface. A decision record: why, what was weighed, what
was chosen, and where it stands.

## Why

The GPU renderer (`hw/hw/hwgs.cpp`, DESIGN.md "The picture") draws the GS's primitives with OpenGL 3.3, which macOS runs as a
layer over Metal. Two of its limits cost the most:

- **Blends OpenGL's blender can't make** (part of a channel masked, the destination alpha test where the draw changes the bit it
  tests, a blend into a 16 bit frame, a texture's region modes blended with the GS's truncation, colour wrapping) are made in
  the shader from a **copy of the target**, and a batch has to end wherever two of its primitives can draw the same pixel. The
  copy is a render pass, and the OpenGL-on-Metal driver submits a command buffer at every render pass change, about 170 µs
  each whatever the size (docs: Biohazard Outbreak's `docs/PERFORMANCE.md`). Biohazard Outbreak's text, foliage and masked 3D
  were tens to hundreds of those a frame; `gsLooseFrameMasks` and `gsRoundedRegionBlends` (Ps2GameInfo) trade a little
  exactness for speed to get round it.
- **Approximations forced by fixed-function state**: depth as a float (exact below 2^24, a reversed encoding for buffers near
  the top), the alpha test's AFAIL as a second pass (out of the GS's order where primitives overlap), the destination alpha
  test as a stencil made in a pre-pass, wrapping sums in a float buffer resolved later.

Apple's GPUs read the destination in the fragment shader (programmable blending: `[[color(n)]]` inputs, in primitive order for
each pixel). With it every one of those is the GS's own per-pixel rule in the shader, with no copy, no extra pass and no batch
break: blending, the frame mask bit for bit, the destination alpha test, AFAIL, colour wrap, dithering, and the depth test on
the GS's 32 bit integer values.

## Options weighed

1. **Port HwGs's structure to Metal**: a second renderer file beside `hwgs.cpp`, its own targets, page tracking and texture
   cache. Rejected: it forks 6,500 lines whose page and texture logic (targets by GS page, the CPU's writes merged on the GPU,
   the texture decode cache, content hashing) took months of fixes found in WoC, CTTR and Biohazard Outbreak; every later fix
   would have to be made twice and the copies would drift (the perfect-code rule against forked subsystems).
2. **A separate Gs::Backend that reuses only small pieces** (the swizzle tables, the vertex placement) and redoes the target
   and texture caching in a simpler Metal-only design. Rejected for the same reason, less starkly: the hard part of HwGs is the
   cache policy, not the GPU calls.
3. **A device seam under HwGs** (chosen): HwGs keeps everything that is the GS's and the caches' policy (targets by page, the
   pages' owners, uploads and downloads, the texture decode and content caches, CLUT and TEXA, region ranges, FBMSK, the games'
   flags, batching, the PCRTC's circuits), and every GPU call goes through a small interface, `Hw::GpuDevice`
   (`hw/hw/gpudevice.h`): make and free a target or a texture, write and read a target's pixels, GS memory's copy on the GPU
   and its conversions to and from targets, decode texture levels from it, merge the CPU's writes into it, draw a batch, merge
   the PCRTC's circuits, hand the shown picture over. `GlDevice` (`hw/hw/gldevice.cpp`) is the OpenGL code moved out of HwGs
   unchanged; `MetalDevice` (`hw/hw/metaldevice.mm`) is the Metal one. Where the two take different routes to the same
   picture (OpenGL's copies and passes, Metal's programmable blending) HwGs asks the device's capability once
   (`ReadsDestination()`) and the batch key carries the GS's raw state for the device that does everything in the shader.
4. **Metal through a portability layer** (MoltenVK and Vulkan, or bgfx): rejected; another dependency between the GS and
   the Mac's GPU, and it hides the very feature (tile memory reads) this is for.

Higher level replacements (drawing the games' models from their own data instead of the GS's primitives, Biohazard
Outbreak's `docs/research/renderer.md` layers A and B) are a separate, later question: they need every effect understood. This
renderer keeps drawing what the GS is given, so every game on the runtime gets it.

## The design

### Switching it on

`--gs metal`, `$PS2_GS=metal`, or `renderer = metal` in graphics.txt. Off by default for every game; `hardware` stays OpenGL.
With no Metal device the OpenGL renderer draws instead (logged). The PC menus' renderer choice is unchanged.

### Targets and pages (shared)

Unchanged and shared: a frame or depth buffer drawn into is a target at the internal scale, keyed by base, width and class;
every 8 KB page records which target holds it newest; the CPU side's reads and writes make pages current; textures that are a
target's own buffer are sampled from it, the rest decoded from GS memory's copy on the GPU by the swizzle tables made from
`gsmemory.cpp`'s functions, cached by tile epochs and by content hash. The games' flags keep their meaning: Metal is exact
whether or not they're set (a partly masked channel costs nothing with programmable blending), so it ignores them.

### What the Metal device stores

- Colour targets: `RGBA8Unorm` at the scale, as OpenGL's (16 bit frames as their 5 bits shifted up and the alpha bit as 0x80),
  so the conversions to and from GS memory are the same arithmetic.
- Depth targets: `R32Uint` colour attachments holding the GS's depth value itself, tested and written in the fragment shader
  (ZTST, ZMSK, ZMAX, the 24 and 16 bit formats' widths). No float encoding, no reversed buffers.
- GS memory's copy: a 4 MB shared buffer (Apple silicon's memory is the GPU's), written by blits from a staging ring in
  command order and read back after the queue is idle; the conversions (target to memory, memory to target, the CPU's
  rectangles merged, texture decode) are compute kernels on it.

### The draw

One render pass a colour and depth target pair, kept open across batches until something else needs the targets (a decode, a
conversion, a read back). The fragment shader is the software GS's `ShadePixel` (`hw/gs/gsdraw.cpp`): texture function, alpha
test with AFAIL, fog, the depth test, the destination alpha test, the blend, dithering, the clamp or wrap, FBA, the frame mask
(any bits), each written by reading `[[color(0)]]` and `[[color(1)]]`. The vertex positions and texture coordinates are
HwGs's (the GS's sample point at a scale). Nothing a batch draws needs a copy of the target; only a texture that is the target
being drawn into (an effect reading its own buffer as texels elsewhere) still reads a copy made at the batch's start.

### The picture

The window, its menus overlay, FXAA and the CRT filter are drawn by OpenGL (`runtime/src/display.cpp`,
`runtime/src/ui/overlayrender.cpp`). The Metal device hands the shown picture over through an IOSurface that both see: the
picture is copied into one of three IOSurface-backed textures, and the render thread's OpenGL context (kept for this) wraps it
for the window as before (the three-slot hand-over and its fences unchanged). A `CAMetalLayer` presenter replaces the window's
OpenGL once the overlay renderer is ported (see "Not done").

## How it's checked

- `--gs-replay FILE --snapshots DIR` with each renderer (software, hardware, metal): the vblanks' picture hashes and pictures,
  compared pixel by pixel (`tools/compare_replays.py`: identical pictures, the pixels that differ and by how much). The
  software GS is the reference (the PS2's rules at 1x); OpenGL is the working picture at the scale.
- The same replays timed (`--gs-replay FILE:PASSES`, the GPU waited for at each pass's end) for speed.
- Live runs of both games, headless and windowed, `--fps`.
- The structural step (OpenGL moved behind the seam) left every recording's OpenGL picture hash the same.

## Where it stands

1. The seam: OpenGL moved behind `GpuDevice` (gldevice.cpp); every recording's OpenGL picture hash the same as before, Biohazard
   Outbreak's ten and WoC's and CTTR's (the title, the Warp Room, CTTR's title and name entry), at 1x and 3x.
2. The Metal device (metaldevice.mm, metalshaders.h): targets, GS memory and its conversions, texture decoding and caches,
   every draw, the circuits' merge, read backs. No draw is left to the software GS that OpenGL draws.
3. Every draw's features are the shader's branches on its constants, one pipeline with a depth attachment and one without, made
   when the device starts: nothing is compiled while a game plays (a variant compiled for each feature set held the render
   thread for seconds in new scenes). The command buffer is committed once it has filled for 8 ms or 2,048 draws, whether or
   not anything asks for the picture.
4. The picture handed to the window through an IOSurface (see "Not done").
5. The texture sampled as the GS samples it (4 bit bilinear weights, its level of detail and level choice, wrap and region
   modes, a target's samples) unless a filtering, upscale or replacement option is on (then the GPU's sampler).

The recordings against the software GS (the PS2's rules, 1x; `tools/compare_replays.py`), the share of pixels that differ
(nearly all by 1 or 2; a handful on polygon and sprite edges by more):

| Recording | Metal 1x | OpenGL 1x |
|---|---|---|
| Biohazard Outbreak File 1, the bar | 1.7% | 34.2% |
| File 1, the bar with zombies | 2.7% | 51.9% |
| File 1, character select | 1.6% | 63.5% |
| File 1, Outbreak's opening cutscene | 0 (identical) | 39.7% |
| File 2, title menu | 0.07% | 34.8% |
| File 2, Training Ground | 2.8% | 77.8% |
| File 2, zoo | 5.4% | 79.1% |
| File 2, partner select | 1.2% | 60.7% |
| File 2, character select | 0.8% | 60.5% |
| File 2, opening | 0.2% | 86.3% |
| File 2, network caution and NET file screens | 0.3% | 76-81% |
| WoC, the Warp Room | 4.0% | 61.3% |
| CTTR, title | 1 pixel | 4 pixels |
| CTTR, name entry | 0.55% | 16.3% |

What's left is the GPU's interpolation of colours and texture coordinates, where the GS steps its own (a pixel's value a step
either side). At 3x Metal and OpenGL differ on 37-89% of the pixels by 1.2-2.3 on average (OpenGL's blending rounds where the
GS truncates, and its sampler's weights aren't the GS's); looked at side by side they're the same pictures.

Speed, the replays at 3x without pictures (ms a vblank, median of passes 2-6, the two renderers one after the other on each
recording; M4, the machine at load 170-235 from other work, so the numbers are high but paired):

| Recording | OpenGL | Metal |
|---|---|---|
| File 1, the bar | 0.50 | 0.45 |
| File 1, the bar with zombies | 4.63 | 3.92 |
| File 1, character select | 3.68 | 2.60 |
| File 1, opening cutscene | 1.41 | 1.42 |
| File 2, title menu | 3.00 | 2.48 |
| File 2, Training Ground | 2.38 | 1.82 |
| File 2, zoo | 1.88 | 1.61 |
| File 2, zoo with zombies | 7.83 | 5.33 |
| File 2, partner select | 4.33 | 2.92 |
| File 2, character select | 3.58 | 2.52 |
| File 2, opening | 2.71 | 2.11 |
| File 2, network caution | 0.93 | 0.45 |

In play (File 2 from boot into the Training Ground's bar, `--fps`): 30 frames a second with either, headless and in a window.
Headless at load 2-5 the render thread took 65 ms of CPU a second with Metal against 87 with OpenGL; in a window the numbers
swung with the machine's load (78-179 and 117-152 in two pairs of runs). The hand-over's own cost: 0.15 ms of the render
thread's CPU a frame and a wait of about 5 ms for the GPU.

## Not done

- The window's hand-over waits for the GPU on the render thread: the picture is copied by Metal into one of three IOSurfaces,
  the render thread waits for that (about 5 ms a frame in File 2's Training Ground at 3x, almost no CPU), and its OpenGL
  context copies the surface into the slot's texture for the window. A presenter on a CAMetalLayer (display.cpp's window, the
  PC menus' overlay renderer and the FXAA and CRT filters in Metal) removes the wait and OpenGL with it.
- WoC's levels (the distance blur reads its depth through the depth test at the GS's sample point, which Metal now makes as
  OpenGL does) and CTTR's races (wrapping shadow volume sums, the glow's region modes) aren't in a recording yet: only WoC's
  title and Warp Room and CTTR's title and name entry were compared.
- Depth interpolated in float above 2^24 (as OpenGL's); exact when a primitive's depth is the same at its vertexes.
- Colours, texture coordinates and fog are the GPU's interpolation, not the GS's own stepping: the last 0-5% of pixels a step
  off the software GS at 1x. Stepping them as the GS does (its edge and span DDA's fixed point in the shader) would close it.
- Metal isn't any game's default yet: Biohazard Outbreak (Ps2GameInfo or its run.sh/app) would choose it once its picture is
  checked by eye in a window over a playthrough and the hand-over's wait is gone.
