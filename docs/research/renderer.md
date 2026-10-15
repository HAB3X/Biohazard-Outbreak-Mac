# Renderer research: what is known about Outbreak's drawing, for a native Metal renderer

Written 2026-10-15. This covers Biohazard Outbreak File 2 (Japan, SLPM_656.92, disc 1.03, sha1 `5c82ff5c…` per
`config/file2.toml`) and File 1, which uses the same engine.

How to read it:
- Every claim cites where it comes from.
- **[ours]** marks a finding from my own scans of our disc files, made for this report. The scripts were throwaway, and
  each method is described so it can be repeated.
- **[guess]** marks an inference that nobody has confirmed.
- Addresses are for **JP File 2** unless marked **USA**. The USA addresses come from reo and are for different
  executables, so they cannot be reused.

## 1. Sources and their licences

| Source | What it is | Licence | Use for us |
|---|---|---|---|
| `~/Downloads/reo-main` (sp00nznet/reo, GitHub, last push 2026-10-14) | Windows static recomp of the USA discs (SLUS-20765 File 1 / SLUS-20984 File 2). It emulates the GS in software and has NBD/AMO/TIM2/AFS parsers. | MIT (`LICENSE`, "Copyright (c) 2026 sp00nz") | Free to reuse with attribution, but its parsers are heuristic (see §2) |
| `~/Downloads/PS2-Decomps/03_Horror/Resident Evil Outbreak File 2/caprado__REOF2` (caprado/REOF2, local copy = upstream HEAD `4149196`, 2026-05-26) | Decompilation of **our** SLPM_656.92, main ELF only: 4,073 functions, raw listing in `extracted/`, plus a small hand port in `src/` that boots to the menu on Windows with OpenGL | **None.** GitHub reports `license: null` | Read-only reference: facts and addresses only, never code |
| github.com/Fothsid/libthanatos (last push 2021-11-21) | C++ reader/writer for NBD/AMO/AHI models plus the "SLD" LZ codec | **GPL-3.0** | Read-only for us (GPL), per the GPL-decomp rule in memory |
| github.com/Fothsid/OBTool (2021-11-21) | Modding tool: AFS, scenario AFS, MOMO, TIM2, NBD↔FBX | **GPL-3.0** | Read-only |
| outbreak-research.github.io/Documentation (repo outbreak-research/Documentation, last push 2022-07-02) | Community wiki: data tables, room lists, animation IDs, file-extension list | **MIT** (repo `LICENSE`) | Free; thin on formats |

Fothsid is said to have a private native PC reimplementation of Outbreak that is not planned for public release
(dsogaming.com article "someone is reverse engineering resident evil outbreak file 2 on pc"). Nothing from it is
published, and it is not a source here.

## 2. Data formats involved in drawing

### 2.1 Archives: AFS / NETBIO*.DAT, MOMO, scenario AFS
- **AFS** (CRI's archive format) has the magic `"AFS\0"` and a file count, then (offset, size) pairs. An optional name
  table can follow, with one 48-byte entry per file: name[32], date (6×u16), size.
  - Sources: `reo-main/game/formats/afs_archive.h`, `REOF2/src/io/afs_reader.h`, OBTool `src/afs.h`.
  - Status: complete and agreed across all three.
- **NETBIO00.DAT** holds a nested AFS:
  - On JP File 2, outer entry 0 is a sub-AFS of 1,397 files (`REOF2/src/graphics/font.c` comments). On USA File 1, reo
    reports a nested `romdata_usa.afs` of 1,342 files (`reo-main/README.md`).
  - Room models sit at `NETBIO00.DAT > r0xx.afs > r0xxyyyy.nbd`, with a scenario list for both Files (Documentation
    `docs/game data/rooms.md`).
- **Scenario AFS**: OBTool `src/extract_scenario_afs.cpp` reads a separate TOC file (`rXXX_h.bin`, named in
  `src/commands.c`) as {textureCount, fileCount}. The first `textureCount` entries are `.sld` textures; the rest are
  other files.
- **MOMO** container: the magic is `"MOMO"`, then a count, then (offset, size) pairs (OBTool `src/extract_momo.cpp`). I
  have not found which assets use it.
- **BIN/n.DAT** code overlays use an `MWo3` header (`config/file2.toml`; `REOF2/src/io/resource_loader.c`). They are
  code, not drawing data.

### 2.2 NBD (model container)
The most complete description is libthanatos `src/OBNbd.cpp` (GPL).
- **Header:** a directory of 16-byte entries {fourcc, offset, size, count}.
  - The fourccs are TEX `0x584554`, TID `0x444954`, AMO `0x4F4D41`, AHI `0x494841` and SDW `0x574453`.
  - Chunks are aligned to 16 bytes.
- **Two layouts**, chosen by the first entry:
  - **Regular NBD** (characters and objects): TEX, model {AMO, AHI}, a dummy entry, shadow SDW, addon {AMO, AHI}, a
    dummy entry.
  - **Room NBD**: TID (texture IDs that refer into the scenario AFS's `.sld` textures), stage {AMO, AHI}, effect
    {AMO, AHI}, two dummy entries.
- **TEX chunk:** per texture, a u32 compressed size followed by the compressed TIM2 (libthanatos; reo
  `game/formats/nbd_parser.h`).
- **SDW:** per-character shadow data. Main characters have body-shaped shadows; NPCs get a simple circle (Documentation
  `docs/3D/models.md`). The SDW format itself is undocumented.
- **reo's view is incomplete:**
  - reo `nbd_parser.cpp` walks the directory generically and knows nothing of SDW or the regular/room layouts.
  - `nbd_renderer.cpp` draws only the *first* AMO, through its own orthographic/perspective camera. It is a viewer and
    does not use the game's pipeline.

### 2.3 AMO (mesh/material data) and AHI (node hierarchy)
Source: libthanatos headers `include/thanatos/OBTypes.h`, `OBMesh.h`, `OBRenderAttribs.h`, `OBNode.h`,
`OBWeightList.h`, `OBMaterial.h`, `OBTextureList.h`.
- **AMO** is a tree of typed chunks, each with a 12-byte header {type, count, size}.
  - Model `1` contains Date `0x20000`, MeshList `2`, MaterialList `9` and TextureList `0xA`.
  - Each Mesh `4` contains:
    - PrimLists `5`
    - TStripList FW `0x30000` / PW `0x40000` (the meaning of FW/PW is not documented)
    - MaterialRefList `0x50000` and TStripMaterialList `0x60000`
    - Vertex `0x70000`, Normal `0x80000`, TexCoord `0xA0000` and Color `0xB0000` lists
    - Weight `0xC0000` (per vertex, a list of {joint id, weight})
    - RenderAttribs `0xF0000`
    - JointRefList `0x100000`
  - Material kinds: Constant `0x120000`, Lambert `0x130000`, Phong `0x140000` and VertColors `0x150000`. A material
    holds ambient, diffuse and specular colours, a specular decay and its texture indices.
  - Each TextureRef has an id, width, height and tiling.
- **RenderAttribs**, per mesh. These fields are what a Metal pipeline state needs:
  - The field list: version (0x10000), material, specular, cull, scissor, light, wrap, uvScroll, disableFog, fadeCol,
    specialShader, alphaSrc/alphaDst/alphaOp, zOp, zWrite, filtering, addressMode.
  - libthanatos notes that the game forces zOp to LESS, and that "light" is used but its effect is not understood.
- **AHI**: a node list. Each node has id, parent, child and next, a transform (scale, quaternion rotation,
  translation), a mesh id and a group id. Types: Hierarchy `0xC0000000`, RootList `0x80000000`, Node / NodeMesh /
  NodeJoint `0x40000001..3`.
- **reo's AMO parser is a heuristic:**
  - `reo-main/game/formats/amo_parser.cpp` scans the whole blob for the first header matching each type tag.
  - It therefore ignores the mesh tree, multiple meshes, weights and render attributes, and keeps only the first
    material.
  - Use libthanatos's description instead.
- **Animation:** the Documentation's `docs/game data/animations.md` lists animation **IDs** only. The animation data
  format (how bone transforms per frame are stored) is **undocumented** anywhere I found.

### 2.4 Textures: TIM2 (Outbreak variant), TEX, SLD
- **TIM2** is Sony's format.
  - Outbreak repurposes header bytes 8–9 as a `globalTextureId` (OBTool `src/tim2.h`; reo `media/tim2/tim2_loader.h`).
  - Picture headers carry ready-made GS `TEX0`/`TEX1`/`TEXA`/`TEXCLUT` values.
  - Formats used: IDX4, IDX8, RGBA32 and RGB16/24. 256×256 IDX8 with a CLUT is the common case (reo README).
  - OBTool `PatchNBDTextures` rewrites the global ids, which implies the game resolves textures by global id. **[guess]**
    That id would be a natural key for a Metal texture cache.
- **Compression ("OB-LZ77" in reo, "SLD" in libthanatos `OBSld`).** The same codec is used for NBD TEX blobs and scenario
  `.sld` files.
  - It works on 16-bit words, with a 16-bit control word read MSB first.
  - A set bit means a token: a 5-bit count and an 11-bit back-offset in words. Count 0 takes the count from the next
    word; offset 0 means a zero-fill run; a token of 0/0 ends the stream.
  - A clear bit means a literal word.
  - Sources: reo `media/tim2/tim2_loader.cpp` `decompress_ob_lz77`; REOF2 `src/compression/lz77_decompress.c`, a port
    of the game's own **`0x1AEE20`**.
  - Status: complete. The Documentation's `docs/textures/index.md` calls it plain LZSS, which is imprecise.
- **CLUT**: PSMT8 CLUTs are stored in CSM1 order.
  - The executable holds the swizzle permutation bytes at **`0x216D60`**. It is used by `0x19DE50` (called from
    `0x19DA50`) **[ours: data dump + lui/addiu reference scan]**. **[guess]** This is part of the texture upload path.
  - reo un-swizzles the CLUT itself in `rearrange_clut`.

### 2.5 Fonts, text and 2D
REOF2 found these, using its own OpenGL replacements (`src/graphics/font.h`, `src/graphics/game_font.c`):
- `FONT8_8.TM2` is entry 580 of NETBIO00's sub-AFS: an 8×8 ASCII bitmap font as TIM2.
- `d20le04.bin` (Light, entry 584) and `d20go04.bin` (Gothic, entry 583) are 20×20, 2 bits per pixel, with 100 bytes
  per glyph and 7,808 glyphs, indexed in JIS X 0208 row/column order.
  - The font descriptor is at **`0x21C438`** = {1, 20, 1, 0, 0x100, ptr `0x21C2C0`, 2, 0x1E80 (= 7808)}
    **[ours: dumped]**.
  - REOF2 says, verified from the assembly, that the menu text is drawn with a black shadow at (+2, +2).
  - REOF2 cites `docs/FONT_FORMAT.md` for the glyph index derivation, but that file is not in the repo (local or
    upstream tree).
- Code that references the font table area (0x21C2C0–0x21C4A0):
  - `0x1B47F0`, `0x1B4A80` and `0x1B4D00` reference `0x21C300`.
  - `0x1B5E90` references `0x21C46F` and `0x21C47A`.
  - Source: **[ours: lui/addiu reference scan]**.
  - Nearby, `0x1B5090` (calls `0x1B5E60`, `0x1B5DD0`, `0x1B5630`, …) is called 104 times from game.bin.
  - **[guess]** The text/message drawing system is at roughly 0x1B4000–0x1B6000.
- 2D sprites, the HUD and menus are undocumented. **[guess]** They are likely GIF PATH3 or PATH2 packets of SPRITE
  primitives, since reo saw its title screen arrive as GIF sprites (`reo-main/README.md`, "flat sprites, textured
  sprites").
- Fades: REOF2 `src/game/game_frame_callback.c` / `frame_update.c` describe two fade buffers, at `0x307FC0` (fadeBuffer1)
  and fadeBuffer2. The fade trigger is `0x1BB9E0` and the fade tick is `0x1BBBA0`. Fades draw as full-screen quads.

### 2.6 Room / scene data (cameras, lights, fog)
- `.rdt`, `.lig`, `.fog` and `.sto` appear by extension only in the Documentation's `docs/game data/index.md`.
- reo `game/formats/rdt_parser.cpp` is an empty TODO; its struct fields are invented, not decoded.
- **Undocumented:** how room cameras and lights are stored and loaded. Outbreak uses fixed per-room camera angles, so
  this matters for the Metal camera.

## 3. The game's render pipeline as traced so far

### 3.1 Frame loop (JP File 2, from REOF2 `src/game/frame_update.c`, annotated from its assembly)
- **`0x19F390` executeFrameUpdate** runs these steps in order:
  1. `0x1BA360` (render)
  2. `0x18D4C0` (waits on GIF DMA, then `0x103650`)
  3. `0x1A8990` (pad/VSync)
  4. Timer 0 reset
  5. A frame counter increment (gp-0x6450)
  6. `0x18D2F0` (packet double-buffer swap and DMA kick)
  7. `0x18DBA0` (selects buffer pointers `0x290378/7C/80` by index `0x290374`, state `0x290370`)
  8. `0x1A05C0` (display update)
  9. `0x1BA310` (game logic)
- **`0x1BA360` render dispatcher** (only when gp-0x6384 ≠ 0) calls:
  1. `0x1AE8D0`
  2. `0x1B5920`, the "graphics buffer/DMA management". It touches `D2_GIF` and calls `0x196050` and `0x1033B0`.
  3. `0x1BB890`, the "scene compositor"
  4. `0x1AE910`
  - The other calls it makes are empty stubs.
- **REOF2 does not trace below this level.** Its Windows port replaces `0x1BB890` with fade-quad drawing and nothing
  else (same file).

### 3.2 Call graph below the dispatcher [ours: REOF2 `SLPM_656.92_functions.json` call_refs + my scans]
The roles in this list are **[guess]**es from shape and data references, unless a source is given.
- **`0x196050`** (3.4 KB) is the main render pass, called by `0x1B5920`, `0x1B7390` and `0x1BB890`.
  - It calls the math helpers `0x18F690`, `0x191030`, `0x191B30`, then `0x1972E0`, `0x1975E0`, `0x197760`, `0x197A10`,
    `0x198130`, `0x1981F0` and `0x1982C0`, then `0x19D440`.
  - `0x1982C0` reads a small table at `0x216760–0x2167D0`.
- **VU1 microprogram selection: `0x198AC0`** (0x700 bytes, a switch).
  - It loads entries of a **descriptor table at `0x2167E0–0x216D5F`**: 27 entries, one per microprogram (§3.3).
  - It is called only from `0x189D00`. That is called from `0x189B70` (← `0x1D96B0` ← `0x1D4DB0`/`0x1D5F60`/`0x1D61C0`)
    and from `0x18C030`, which game.bin calls directly.
  - `0x189D00` also calls `0x18A140`, `0x18B280` and `0x18BE00`.
  - **[guess]** `0x189B70`/`0x189D00`/`0x18C030` are the "submit one model/mesh batch" layer, and the cleanest
    per-model hook candidates.
- **EE-side packet builders**: each descriptor entry holds a function pointer to one of 22 distinct functions in
  `0x199B90–0x19C63C`.
  - The main ELF has no direct `jal` to any of them, which is consistent with indirect calls through the table.
  - These functions, and the `0x190080–0x199B10` range, are where REOF2's listing shows the most `ldc2`/`sdc2` (that is,
    LQC2/SQC2, VU0 macro mode).
  - **[guess]** They build the per-batch VIF1 packets: UNPACK of vertex data, matrices and lighting into VU1 memory, then
    MSCAL.
- **Texture upload**: `0x19D440` calls `0x19EE10`.
  - REOF2 says `0x19EE10` allocates texture slots by scanning VRAM-page tables at `0x288C80`/`0x285480`
    (`src/graphics/texture_slot.h`).
  - `0x19D440` is called 13 times from game.bin. **[guess]** It is "upload/bind texture".
  - `0x19DA50` → `0x19DE50` does the CLUT swizzle (§2.4).
- **Math library**: `0x18FBA0–0x193300`. Small VU0-macro functions that game.bin calls hundreds of times; for example,
  `0x18FBA0` (147 calls), `0x191780` (199) and `0x191970` (172).
  - Source: **[ours: `jal` census of BIN/2.DAT]**.
  - **[guess]** Matrix and vector operations. Camera and object matrices are built through these, so hooking them can
    reveal view and model matrices.
- **Display / GS init** (REOF2 `src/graphics/graphics_init.c`, `display_settings.c`):
  - `0x19F080` init calls `0x19F230` → `0x18DB40` (VRAM allocation) → `0x19F420` (DMA/GS packet setup) → `0x19F4E0`
    (display globals at `0x28FF70–0x290424`) → `0x1A0010` (GS registers) → `0x1A1530`.
  - REOF2 notes that screen-centre offsets `0x290314`/`0x290318` = 0x800 − width/2 and 0x800 − height/2, used by
    `0x18F780`. This is the standard GS XYOFFSET centring for 12.4 fixed-point coordinates.
- **GS privileged registers** **[ours: decoding of the actual instructions in the ELF]**:
  - `0x1A0010` writes PMODE, SMODE2, DISPFB1/2, DISPLAY1/2 and EXTWRITE (`0x1200_0000/20/70/80/90/A0/D0`).
  - `0x18D2F0` and `0x18CFF0` read and write CSR/IMR (`0x1200_1000/1010`).
  - `0x18CC10`/`0x18CFF0` write SIGLBLID (`0x1200_1040`) and VIF1_STAT (`0x1000_3C00`). `0x18CC10` writes
    `0x00800000` (FDR = 1).
  - **[guess]** The game reads GS memory back to EE (a local→host transfer) for some effect or screenshot. A native
    renderer must find out what consumes that readback.
- **Low-level SDK**:
  - `0x1033B0` (DMA send; calls `0x102F28`/`0x103020`)
  - `0x1030A8`/`0x103188` (D_CTRL)
  - `0x102960`/`0x102C90` (VIF0/VIF1 register setup; **[guess]** VIF reset)
  - `0x112CB8`, `0x113130`, `0x113630` (GIF and VIF1 DMA channel registers)
  - `0x1123B0` (GIF_CTRL)
  - Source: **[ours: hardware-register access scan of REOF2 `extracted/`]**.
- **Render state**: `0x1AEFD0` takes a state ID (0x0D/0x5F/0x60/0x63/0x6C) and is called 134 times from game.bin.
  - REOF2 (`src/graphics/render_state_manager.h`) calls it a blend/texture state switch.
  - But REOF2 `src/game/game_init.c` says the `0x1A0980` it calls stores a "timerInitValue" to `0x28FFC8`.
  - The two descriptions conflict, so its real meaning is **open**.
- **Scene loading**: `0x1BC1B0` calls `0x1DD9C0(cfg @0x313890/94/98)`, then `0x1C69D0` (REOF2 `src/game/scene_init.c`).

### 3.3 VU1 microcode [ours: scan of the executable, 0x1DDB80–0x1EDF00]
- **Count:** the region holds **29** VIF `MPG` uploads, each a DMA `ret` tag (`0x6000_00xx`) followed by `MPG` codes.
  This disagrees with the earlier note of 16 programs.
  - Every one loads at VU micro address 0. They are not resident together: one is uploaded per draw type.
  - 27 of them are referenced by the descriptor table. The two at `0x1EB540` and `0x1EC180` are referenced from nowhere
    in the ELF or the overlays, so they may be dead.
- **Descriptor entry layout** (0x30 bytes, sometimes preceded by a 0x10-byte extra block). The meaning of each field is a
  **[guess]**:

| Offset | Value | Notes |
|---|---|---|
| +0 | Packet address | |
| +0xC | Microcode byte size | Checked against the instruction count: 408×8 = 0xCC0 |
| +0x10 | EE builder function | |
| +0x14 | Small int | Values 0–4 seen |
| +0x18 | Small int | Values 0–8 seen |
| +0x1C | Flag | |
| +0x20 | Int | 0xA00–0x1100; maybe a VU buffer size or batch limit |
| +0x24 | Float | 128.0 or 255.0; likely a colour or alpha scale (128 = 1.0 on the GS) |
| +0x28 | Optional pointer | To a {0x14..0x18, 0x20, 3} block |

- **Signature counts.** I decoded each upper/lower instruction's opcode field.
  - **Every program** has `DIV`, `MULq` and `FTOI4`: a perspective divide, then conversion to GS 12.4 fixed point.
    The VU1 programs do the full transform and projection.
  - **Every program** has 2–5 `XGKICK`s: output goes to the GS by PATH1. One `XTOP` per program means standard
    double-buffered input.
  - **No program uses `CLIP`.** **[guess]** There is no per-triangle clip-flag culling. The game probably relies on GS
    scissoring and the ±2048 guard band, plus coarse culling on the EE.
  - `ERSQRT`/`ERLENG` (EFU unit, square root and length) appear in 13 programs. **[guess]** They are lighting or normal
    normalisation, or point-light distance.
  - `OPMULA`/`OPMSUB` (cross product) appear in progs at `0x1E11E0`, `0x1E1880` and `0x1EB540`. **[guess]** Normals,
    environment mapping or billboards.
  - Programs at `0x1DEE50`, `0x1E2AF0`, `0x1E3C10`, `0x1E7300`, `0x1E8C90` and `0x1EB540–0x1ECC20` have 14–18 `ILW`s
    and 90–143 `LQ`s. **[guess]** Matrix palettes indexed by integer joint IDs, which would mean **skinning**. AMO
    carries weights and joint references (§2.3), so skinning on VU1 is plausible.
  - Programs at `0x1DDB80`, `0x1E47D0`, `0x1E5740`, `0x1E65A0`, `0x1E7E30`, `0x1E8C90` and `0x1EA400` each have `ITOF0` and about 23 `LQI`/`SQI`. **[guess]** Particle or
    sprite expanders that work on integer input.
- **Not established by anyone:** the VU memory layout of any program (where matrices, lights, vertex streams and GIF
  tags sit), and exactly which effect each program renders. No disassembly of these programs exists.
  - reo has a VU interpreter only (`runtime/vu/vu_interpreter.h`) and no analysis of the programs.

### 3.4 What reo found (USA File 1; addresses do not apply to JP)
Sources: `reo-main/README.md`, `recomp/overrides/reo_override.cpp` and `reo_hw_bridge.cpp`.
- **Display-list state block** at `0x29F5D0–0x29FE00`:
  - Double-buffered 512 KB packet buffers
  - Write/end/base pointers at `0x29FDB8/BC/C0`
  - GIF tag processor `sub_0018D470`
  - DMA channel table `0x29FD74`
- **Scene renderer `sub_001D3DC0`** is described as 193 KB, running every frame: scene lookup → render target → scene
  builder → finalisation.
  - In JP File 2 the same chain appears as the `0x290370`-based state above.
  - **[guess]** USA `0x18D470` corresponds to JP `0x18D2F0`, which dispatches on DMA tag ID through a jump table at
    `0x22A3F0` (REOF2 `extracted/func_0018d2f0.c`).
- **reo's state of play:**
  - reo never drew a 3D game scene through the game's own path. It reached the title screen and GS dump replays.
  - Scene data was injected from PCSX2 memory snapshots (`reo_override.cpp`, `loadBin` of `pcsx2_*.bin`).
  - Many SDK and render functions were stubbed to return 0 or 1.
  - It is useful as a map of the USA GS and DMA plumbing, not as proof of how the pipeline works.

## 4. Gaps (nobody has documented these)
1. **The VU1 programs.** The memory layout and the input packet format of each of the 27 used programs, and what each one
   renders (rooms, skinned characters, effects, shadows, sprites).
2. **The EE packet builders** (`0x199B90–0x19C63C`), and the in-memory structures they read: model instances, mesh
   batches, bone palettes, lights.
3. **Where the camera, view and projection matrices live** at runtime. The camera inputs are REOF2's `0x2A0F40` source
   buffer and `0x1A17E0`, but that is input handling, not the render camera.
4. **The animation data format** and the code that evaluates it. Only animation IDs are known.
5. **SDW shadows, room data** (`.rdt`, `.lig`, `.fog`, `.sto`), and how rooms pick their camera.
6. **The 2D/HUD/menu drawing functions**, and the mapping between text drawing and glyphs in the game's own code.
7. **The GS→EE readback**: what consumes it.
8. **The renderer's code inside game.bin (BIN/2.DAT, 0x543040–0x715B80).**
   - REOF2 covers the main ELF only.
   - game.bin makes about 5,360 `jal`s into 374 distinct main-ELF functions **[ours]**.
   - The VU program management, the math library and the DMA code all live in the **main ELF**: no overlay references the
     VU packets or the descriptor table **[ours]**.
9. **REOF2's `extracted/` listing is lossy for this work.**
   - It has no 64-bit or 128-bit loads and stores: zero `sd`, `sq` or `lq` in all 4,080 files, though the ELF has them
     (for example the GS `sd`s in `0x1A0010`).
   - It decodes R5900 COP2/MMI ops under generic MIPS/MSA names (`ldc2`, `aver_u.h`, `addu.qb`).
   - Packet and GS code must be read from our own disassembly or recompiled output, not from REOF2.

## 5. Interception plan for a native Metal renderer

The three possible layers, from top to bottom:
- **(A) per model/batch**: around `0x189B70`/`0x189D00`/`0x18C030` and the builder callbacks.
- **(B) per VIF1 packet / VU program invocation**: the descriptor table plus the DMA chain kicked through `0x1033B0` /
  `0x18D2F0`.
- **(C) per GS primitive**: run VU1 (interpreted or recompiled) and translate the XGKICK and GIF output.

| | A: per-model draw calls | B: per VIF packet (HLE of each VU program) | C: per GS primitive |
|---|---|---|---|
| Hook points | Model-submit functions (§3.2) | The 27-entry descriptor table and its builder callbacks, or the VIF1 DMA chain | GIF packets after VU1 |
| What Metal receives | The game's own meshes, in float, with materials (AMO RenderAttribs map straight onto pipeline state) and bone matrices | UNPACKed float vertex data plus matrices in VU memory, with a known program ID per batch | Projected 12.4 fixed-point vertices, GS TEX0/ALPHA/TEST state, VRAM addresses |
| Resolution, widescreen, 60 fps | Best | Good: re-project in a shader with our own matrices | Limited: post-projection, so widescreen needs hacks, and the guard band and 4096 range carry over |
| Reverse-engineering cost | Highest: in-memory model, instance and animation structs, lights, every effect type, 2D separately | Medium: 27 programs × (input layout + algorithm), and finite, because the table closes the set | Lowest for coverage; a VU1 interpreter or recompiler already exists in principle |
| Risk | Draws that bypass the layer (effects, particles, 2D) go missing silently | Programs we have not reimplemented, so we need a "fall back to C for this program" path | It is GS emulation: VRAM aliasing, CLUT and texture caching, readback and framebuffer effects. This is what the user wants to avoid |
| 2D, text, fades | Not covered: needs its own hooks (font area 0x1B4000–0x1B6000 **[guess]**, fades `0x1BB9E0`/`0x1BBBA0`) | Not covered unless the 2D goes through VIF; reo saw the title screen arrive as GIF sprites | Covered automatically |

**Suggested approach, grounded in the findings above.** Points 1 and 3 are **[guess]**es about what will work best.

1. **Make B the main 3D layer.** It has one choke point in the main ELF (`0x198AC0` + `0x2167E0` + the builder
   callbacks), and a closed, enumerable set of programs. The vertex data is still in float space, so Metal can project
   with its own matrices at any resolution or aspect ratio.
   - Reimplement each VU program as a Metal vertex or compute function, after documenting its VU memory layout from the
     builder that feeds it.
   - Start with the skinned-character and room programs, which have the most `ILW`/`LQ`s (§3.3).
2. **Keep C as a per-program fallback and as the oracle.** For programs not yet reimplemented, run the recompiled or
   interpreted VU1 and rasterise the GIF output. Compare Metal output against it, and against PCSX2 GS dumps (reo's
   `gs_dump_replay` shows the technique). Then no draw is silently lost while B grows, which follows the fail-loudly
   rule.
3. **Use A selectively for metadata, not drawing.** Hook the model-submit layer and the math library (`0x18FBA0…`) to
   capture per-object identity, view and projection matrices and RenderAttribs. Then B can attach Metal pipeline state
   and depth or ordering hints.
4. **Handle 2D, text and fades at the function level.** Identify the draw functions in `0x1B4000–0x1B6000` and the
   fade buffers. Glyph data is fully known (§2.5), so text can be drawn crisply at any resolution.
5. **Textures:** decode TIM2 once at load time. Key the cache by `globalTextureId`, falling back to the TID/AFS index for
   rooms. Hook `0x19D440`/`0x19EE10` instead of tracking GS VRAM.
6. **First steps:**
   - Disassemble one builder (`0x19A770`, the first table entry) and its program `0x1DDB80` end to end.
   - Dump a VU1 memory snapshot from PCSX2 in a room, to confirm the input layout.
   - Find the consumer of the `0x18CC10` GS→EE readback before removing GS emulation.

## 6. How the [ours] findings were made (to reproduce)
- **VU programs:** walk `SLPM_656.92` from VA 0x1DDB80 to 0x1EDF00 (file offset = VA − 0x100000 + 0x200). Find VIF codes
  with `cmd == 0x4A` (MPG) at odd word positions, then classify each 64-bit instruction:
  - Upper ops: bits 5..0 ≥ 0x3C select the special table (`(bits10..6 << 2) | bits1..0`, CLIP = 0x7F).
  - Lower ops: bits 31..25 = 0x40 with the same special-table rule (XGKICK = 0x6C, ERSQRT = 0x79, …).
- **Table and reference scans:**
  - Search for 32-bit words equal to the packet addresses, which found the table at `0x2167E0`.
  - Track `lui` + `addiu`/`lw`/`sw` across ELF text and the overlays to find code references.
  - Use REOF2's `SLPM_656.92_functions.json` `call_refs` for the callers.
  - Take a `jal` census of BIN/2.DAT, 0x543040–0x715B80.
- **Hardware registers:** the same `lui` tracking, matched on 0x1000xxxx and 0x1200xxxx addresses, over both REOF2
  `extracted/` and the raw ELF.
