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

`--gs metal`, `$PS2_GS=metal`, or `renderer = metal` in graphics.txt; the default for a game whose `Ps2GameInfo::gsRenderer` is
"metal" (Biohazard Outbreak's both discs, at 4x: their `gsScale`), OpenGL for the others. With no Apple GPU, or when Metal's
presenter can't open the window, OpenGL draws instead; a Metal renderer that can't start leaves the software GS drawing under
Metal's presenter (all logged). The PC menus have no renderer choice.

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

### The pipelines

Every draw's features are the fragment shader's branches on the draw's constants, so one general pipeline (with a depth
attachment and without) draws anything and nothing is compiled while a game plays: a variant compiled on the render thread for
each feature set held it for seconds in each new scene. The branches cost: the zoo with zombies at 4x took 8.2 ms of the GPU a
vblank. So each draw's features (the texture function, TCC, fog, the alpha and destination alpha tests, the blend's terms, the
frame's and depth buffer's kinds, the sampling's levels and filters: `SpecialisedFeatures` in metaldevice.mm, the shader's
function constants read by `FEATURE`) also get a pipeline specialised for them, the branches compiled away. It's made in the
background the first time the features are drawn (the function specialised, then the pipeline, both with completion
handlers), the general pipeline drawing until it's ready: 12-24 of them in a recording, the zoo with zombies 2.3 ms at 3x and
4.8 at 4x where the general one took 4.2 and 8.2, every recording's pictures the same (hashes) at 1x and 3x.

### The picture

The window's drawing is a presenter's (the runtime's `presenter.h`): Metal's for the Metal renderer (`presentmetal.mm`),
OpenGL's for the others (`presentgl.cpp`). The renderer copies the shown picture into its slot's texture (display.cpp's three
slots) and commits the copy without waiting; the presenter draws it into the window's CAMetalLayer's drawable (plain, FXAA or
the CRT filter, letterboxed), with the PC menus' overlay over it (`ui/overlaymetal.mm`), and presents. Both use one device and
one command queue (`hw/hw/metalshared.h`): a queue's command buffers run in the order they're committed, so the drawing of a
slot comes after the copy into it, and a copy into a slot drawn before comes after that drawing, with no fences and no thread
waiting for the GPU. At most three frames are in flight (a semaphore; the software GS's pictures' textures are used in turn).
The window's shaders and the overlay's are OpenGL's in Metal's language, line for line; the overlay is one list of draws
(`ui/overlayrender.cpp`) both presenters' devices draw. The window's picture with the menus (`--ui`'s shots) is the same
drawing into a texture, read back.

## How it's checked

- `--gs-replay FILE --snapshots DIR` with each renderer (software, hardware, metal): the vblanks' picture hashes and pictures,
  compared pixel by pixel (`tools/compare_replays.py`: identical pictures, the pixels that differ and by how much). The
  software GS is the reference (the PS2's rules at 1x); OpenGL is the working picture at the scale.
- The same replays timed (`--gs-replay FILE:PASSES`): each pass waits for the GPU at its end and says the GPU's busy time
  (Metal's command buffers' GPU start to end, overlaps counted once). `--fps` says the GPU's ms a second too. Other processes
  using the GPU at the same time stretch both (a command buffer waits for theirs).
- `TWIN_HW_STATS=1`: the encoders begun, draws (general or specialised) and copies a picture.
- Live runs of both games, headless and windowed, `--fps`; `MTL_DEBUG_LAYER=1` for Metal's API validation.
- The structural step (OpenGL moved behind the seam) left every recording's OpenGL picture hash the same.

## Where it stands

1. The seam: OpenGL moved behind `GpuDevice` (gldevice.cpp); every recording's OpenGL picture hash the same as before, Biohazard
   Outbreak's ten and WoC's and CTTR's (the title, the Warp Room, CTTR's title and name entry), at 1x and 3x.
2. The Metal device (metaldevice.mm, metalshaders.h): targets, GS memory and its conversions, texture decoding and caches,
   every draw, the circuits' merge, read backs. No draw is left to the software GS that OpenGL draws.
3. Every draw's features are the shader's branches on its constants in the general pipelines, made when the device starts, and
   a pipeline specialised for each draw's features is made in the background ("The pipelines"). The command buffer is
   committed once it has filled for 8 ms or 2,048 draws, whether or not anything asks for the picture.
4. The window drawn by Metal: a CAMetalLayer presenter with the PC menus' overlay, FXAA and the CRT filter; the render thread
   doesn't wait for the GPU to hand the picture over (the IOSurface hand-over waited about 5 ms a frame).
5. The texture sampled as the GS samples it (4 bit bilinear weights, its level of detail and level choice, wrap and region
   modes, a target's samples) unless a filtering, upscale or replacement option is on (then the GPU's sampler).
6. Biohazard Outbreak's default (both discs, at 4x).

The recordings against the software GS (the PS2's rules, 1x; `tools/compare_replays.py`, a picture every 0.1 s), the share of
pixels that differ (nearly all by 1 or 2; a handful on polygon and sprite edges by more); Metal at 4x read at the GS's sample
points (each 4 x 4 block's first sample) differs as little:

| Recording | Metal 1x | Metal 4x at the sample points | OpenGL 1x |
|---|---|---|---|
| Biohazard Outbreak File 1, the bar | 1.7% | 4.3% | 34.2% |
| File 1, the bar with zombies | 2.7% | 2.8% | 51.9% |
| File 1, character select | 1.6% | 1.6% | 63.5% |
| File 1, Outbreak's opening cutscene | 0 (identical) | 0 (identical) | 39.7% |
| File 2, title menu | 0.07% | 0.08% | 34.8% |
| File 2, Training Ground | 2.8% | 2.8% | 77.8% |
| File 2, zoo | 5.4% | 5.7% | 79.1% |
| File 2, zoo with zombies | 7.3% | 7.3% | 84.1% |
| File 2, partner select | 1.2% | 1.2% | 60.7% |
| File 2, character select | 0.8% | 0.8% | 60.5% |
| File 2, opening | 0.2% | 0.2% | 86.3% |
| File 2, network caution and NET file screens | 0.3% | 0.3% | 76-81% |
| File 2, network setup screens | 0 (identical) | 0 (identical) | 0 (identical) |

What's left is the GPU's interpolation of colours and texture coordinates, where the software GS steps its own (scattered
pixels a step either side, over surfaces). At 4x Metal and OpenGL differ on 37-89% of the pixels, nearly all by 1 or 2 (OpenGL's
blending rounds where the GS truncates, its sampler's weights aren't the GS's, and its games' flags trade exactness for speed:
OpenGL at 4x read at the sample points differs from the software GS by up to 158 in the zoo); looked at side by side they're the
same pictures.

Speed, the replays without pictures on an M4 (ms a vblank, the median of passes 2-12; the GPU's busy time, and the wall time
with the GPU waited for at each pass's end). Other agents' games shared the GPU and the CPUs throughout (load 4-11), so these
vary run to run: the quietest run's, with the range over three more runs under heavier load where it differed much:

| Recording | Metal 3x GPU | Metal 4x GPU | Metal 5x GPU | Metal 4x render thread CPU |
|---|---|---|---|---|
| File 2, zoo with zombies | 2.5 (to 4.1) | 4.2 (to 6.6) | 8.5 (to 10.4) | 3.6-5.8 |
| File 1, the bar with zombies | 1.3 | 1.9 | 2.5 | 2.8-3.1 |
| File 1, character select | 1.0 | 1.4 | 2.4 | 1.9-2.3 |
| File 2, character select | 1.0 | 1.5 | 2.2 | 1.9-2.2 |
| File 2, partner select | 1.1 | 1.7 | 3.0 | 2.1-3.0 |
| File 2, Training Ground | 1.7 | 3.1 | 4.9 | 1.3-1.6 |
| File 2, zoo | 0.7 | 1.1 | 2.2 | 1.1-1.4 |
| File 2, title menu | 0.9 | 1.3 | 1.9 | 1.5-2.3 |
| File 2, opening | 1.0 | 2.0 | 3.2 | 0.8-1.2 |

A game frame is two vblanks at 30 frames a second: the zoo with zombies at 4x is 8-13 ms of the GPU's 33 a frame on the M4.
OpenGL's replays at 3x and 4x took 2-8 ms a vblank of wall time against Metal's 2-6 under the same load.

In play (Metal at 4x, both discs, headless and in a window, `--fps`): 30 frames a second from boot through the menus, the
selects, the network screens to obsrv's login page and into the first scenarios (File 1's bar, File 2's Training Ground), but
for loads and the PC menus' pause. File 2's Training Ground in a window, the same place at each scale: the GPU 190, 271 and 294
ms a second at 3x, 4x and 5x (shared with the other agents' games), the render thread 64 and the EE 54 ms a second at each.

## Not done

- The colours', texture coordinates' and fog's stepping as the software GS steps them (its triangles' planes in double
  precision at each run's start, then single precision steps along the run): not cheap in a fragment shader (the run's start
  found from the edges, double precision emulated) and only seen at 1x as single steps on scattered pixels; at the default 4x
  there's no reference to be exact against.
- Depth interpolated in float above 2^24 (as OpenGL's); exact when a primitive's depth is the same at its vertexes. Every
  Biohazard Outbreak primitive with a depth above 2^24 in the recordings (Z32: up to 0xFFFFFFFE in the zoo, the partner select
  and the opening) has the same depth at its vertexes, so none of its pictures changes; WoC's and CTTR's weren't looked at.
- WoC's levels and CTTR's races aren't in a recording yet: only WoC's title and Warp Room and CTTR's title and name entry were
  compared. They keep OpenGL as their default.
- The specialised pipelines are made again each run (Metal's own shader cache keeps the compiled code between runs; a binary
  archive would keep the pipelines too).
