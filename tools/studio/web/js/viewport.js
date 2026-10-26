// The viewport: draws the models and the markers, and turns the pointer into navigation, selection, moving and placing.
import { Camera } from "./camera.js";
import { Renderer, modelKey } from "./gl.js";
import * as overlay from "./overlay.js";
import { store, on, emit, setMode, setRoom, objectOf, roomOf, select, commit, clone, translate, centre, refEq, describe, addWall, addFloor } from "./scene.js";
import { PLACE, FIXED, THINGS, place, ground, snapTo } from "./tools.js";
import { api } from "./api.js";
import { attach, sync, setPose, neighbourSnap } from "./scenery.js";

const GAME_ASPECT = 8 / 7, GAME_LENS = 16 / 9;      // the shape of the game's picture as the runtime saves it, and of the lens (fitted to a snapshot)

export class Viewport {
  constructor() {
    this.view = document.getElementById("view");
    this.gl = document.getElementById("gl");
    this.ov = document.getElementById("ov");
    this.ctx = this.ov.getContext("2d");
    this.renderer = store.renderer = new Renderer(this.gl);
    this.camera = new Camera();
    this.hover = null;
    this.drag = null;
    this.cursor = null;       // the pointer in the view, for the ghost of what a tool will place
    this.space = false;
    this.pending = false;
    this.renderer.onTexture = () => this.redraw();
    overlay.setFloorLookup((file, x, z, y) => this.standY(file, x, z, y));
    this.characterFiles = {};     // "kind:class" -> the model file an enemy is drawn with, null when there is none, undefined while it is fetched
    overlay.setCharacterLookup((n) => this.characterFor(n));
    new ResizeObserver(() => this.resize()).observe(this.view);
    this.bind();
    this.resize();
    on("selection", () => this.redraw());
    on("changed", () => this.redraw());
    on("room", () => this.redraw());
    this.camIndex = {};       // room file -> the number of the camera being looked through in the game view
    on("mode", () => this.applyMode());
    on("room", () => { if (store.mode === "game") this.useGameCamera(); else if (store.mode === "3d" && !this.followingEye) this.enterFree(); });
    on("selection", () => { if (store.mode === "game") this.followSelection(); });
    on("tool", () => { this.wallStart = null; if (store.tool === "wall" || store.tool === "floor") store.show.walls = true; this.updateCursor(); this.redraw(); });
  }

  // ---- the free camera: W A S D move, Q and E go down and up, the wheel moves forward and back, dragging the empty picture turns the view, shift is fast
  flyKey(e, down) {
    if ((store.mode !== "3d" && store.mode !== "game") || e.metaKey || e.ctrlKey) return false;
    const k = e.key.toLowerCase();
    if (!"wasdqe".includes(k) || k.length !== 1) return false;
    if (store.mode === "game") { if (!down) return false; setMode("3d"); }      // moving in the game view lets go of the game's camera: the free camera starts from it
    this.keys = this.keys || new Set();
    if (down) this.keys.add(k); else this.keys.delete(k);
    this.fast = e.shiftKey;
    if (down && !this.flying) { this.flying = true; this.last = performance.now(); requestAnimationFrame((t) => this.flyTick(t)); }
    return true;
  }

  flyTick(now) {
    const dt = Math.min(0.05, (now - this.last) / 1000);
    this.last = now;
    if (!this.keys || !this.keys.size || store.mode !== "3d") { this.flying = false; return; }
    // a room is about 1000 across and a person 170 tall: under half a room a second to look around in, Shift four times that to cross the level
    if (this.camera.fly(this.keys, dt, this.fast ? 1800 : 450)) { this.followEye(); this.redraw(); emit("camera"); }
    requestAnimationFrame((t) => this.flyTick(t));
  }

  // in Build the room you work in is the one you fly into: the room whose floor is nearest below the eye (the rooms share one world).
  // The camera stays where it is (a room picked from the list takes you to it instead)
  followEye() {
    if (store.mode !== "3d") return;
    const eye = this.camera.eye();
    // where rooms overlap (the bar and the street around it) the room you are in stays yours while it has floor under you, so the view doesn't flip between them
    const own = this.floorAt(store.room, eye[0], eye[2], eye[1]);
    if (own !== null && eye[1] - own < 1500) return;
    let best = null, highest = -Infinity;
    for (const r of store.scene.rooms) {
      const h = this.floorAt(r.file, eye[0], eye[2], eye[1]);
      if (h !== null && h > highest && eye[1] - h < 1500) { highest = h; best = r.file; }
    }
    if (!best || best === store.room) return;
    this.followingEye = true;
    try { setRoom(best); } finally { this.followingEye = false; }
  }

  // the free camera starts where one of the room's cameras stands, looking where it looks
  enterFree() {
    this.useGameCamera();
    const g = this.camera.game;
    this.camera.setMode("orbit");
    if (g) this.camera.startFree(g.eye, g.target); else this.frameRoom(store.room);
  }

  // ---- the game view: the room as one of its own cameras shows it
  applyMode() {
    if (store.mode === "game") this.useGameCamera();
    else if (store.mode === "3d") { if (this.camera.mode !== "orbit") this.enterFree(); }
    else { if (this.camera.mode !== "plan") { this.camera.setMode("plan"); this.overview(store.room); } }
    emit("camera");
    this.redraw();
  }

  // the fog record of the room's camera the view is through (the game view's own, the free camera's nearest by where it stands), or null
  cameraFog() {
    const r = roomOf(store.room);
    if (!r || !r.cameras.length || this.camera.mode === "plan") return null;
    const k = this.camera.mode === "game" ? this.camIndex[r.file] : this.cameraFor(r, this.camera.eye());
    const c = r.cameras[k ?? 0];
    if (c && c.fog) return c.fog;
    const any = r.cameras.find((x) => x.fog);          // some cameras have no record of their own (the bar's last four): the room's first one's light
    return any ? any.fog : null;
  }

  cameraFor(room, p) {
    // the camera whose box holds the point, else the one nearest it
    const inside = (c) => {
      const q = c.box.slice(0, 4).map((b) => [b[0], b[2]]);
      let hit = false;
      for (let i = 0, j = q.length - 1; i < q.length; j = i++) if ((q[i][1] > p[2]) !== (q[j][1] > p[2]) && p[0] < (q[j][0] - q[i][0]) * (p[2] - q[i][1]) / (q[j][1] - q[i][1]) + q[i][0]) hit = !hit;
      return hit;
    };
    let k = -1;
    room.cameras.forEach((c, i) => { if (inside(c)) k = i; });       // the last one whose box holds the point (measured: the first room of file 2's zoo starts in camera 3, boxes 2 and 3 both hold the start)
    if (k >= 0) return k;
    let best = 0, bd = 1e18;
    room.cameras.forEach((c, i) => { const d = Math.hypot(c.eye[0] - p[0], c.eye[2] - p[2]); if (d < bd) { bd = d; best = i; } });
    return best;
  }

  useGameCamera(index) {
    const r = roomOf(store.room);
    if (!r || !r.cameras.length) { this.camera.setMode("plan"); return; }
    let k = index !== undefined ? index : this.camIndex[r.file];
    if (k === undefined || !r.cameras[k]) k = r.spawns[0] ? this.cameraFor(r, r.spawns[0].pos) : 0;
    this.camIndex[r.file] = k;
    const c = r.cameras[k];
    this.camera.setGame({ eye: c.eye, target: c.target, fov: c.fov[0], aspect: GAME_ASPECT, lens: GAME_LENS });
    emit("camera");
    this.redraw();
  }

  stepCamera(by) {
    const r = roomOf(store.room);
    if (!r) return;
    this.useGameCamera((this.camIndex[r.file] + by + r.cameras.length) % r.cameras.length);
  }

  // in the game view, what you pick should be in the picture: the camera that covers it is taken when the current one doesn't show it
  followSelection() {
    const ref = store.selection[0], r = roomOf(store.room);
    if (!ref || store.selection.length !== 1 || !r) return;
    const o = objectOf(ref);
    if (!o) return;
    const c = centre(ref.c, o), s = this.camera.project(c), rc = this.camera.rect;
    const seen = s[2] > -0.9 && s[0] > rc.x && s[0] < rc.x + rc.w && s[1] > rc.y && s[1] < rc.y + rc.h;
    if (!seen) this.useGameCamera(this.cameraFor(r, c));
  }

  hiddenSets() {
    const out = {};
    for (const r of store.scene.rooms) out[r.file] = new Set(r.hidden || []);
    return out;
  }

  // ---- loading the models in the background, the room being worked in first
  async loadModels(progress) {
    const rooms = [...store.scene.rooms].sort((a, b) => (a.file === store.room ? -1 : b.file === store.room ? 1 : 0));
    let done = 0;
    const later = [];       // rooms with Library objects in their saved edits: those objects come from other rooms' models, so they are set up once every model is in
    for (const r of rooms) {
      const file = r.file.replace(".rdt", ".nbd");
      try {
        if (!store.models[r.file]) {
          const json = await api.model(file);
          if (!json.error) {
            store.models[r.file] = true; this.renderer.add(r.file, json);
            const saved = store.history[store.at].rooms[r.file];
            if (saved && (saved.new_objects || []).some((n) => n.library)) later.push(r.file); else attach(this.renderer, r.file);
          }
        }
      } catch (e) { /* a room without a model is drawn without one */ }
      progress && progress(++done, rooms.length);
      this.redraw();
    }
    // the models of other levels your saved Library objects come from
    for (const file of later) {
      for (const n of store.history[store.at].rooms[file].new_objects || []) if (n.library && n.library.game) await this.loadLevelModel(n.library);
    }
    for (const file of later) attach(this.renderer, file);
    this.redraw();
  }

  // a room model of another level (a Library source: {room, game, scenario}) or RE4 asset, kept among the renderer's models under modelKey; fetched once
  async loadLevelModel(lib) {
    const key = modelKey(lib);
    if (this.renderer.models[key]) return true;
    const json = lib.game === "re4" ? await api.re4Model(lib.id || lib.room) : await api.model(lib.room.replace(".rdt", ".nbd"), lib.game, lib.scenario);
    if (json.error) { console.warn(`${key}: ${json.error}`); return false; }
    this.renderer.add(key, json);
    return true;
  }

  // the pictures you replaced are painted onto the models as they change (and put back when undone)
  syncPictures() {
    this.pictures = this.pictures || {};
    for (const index of new Set([...Object.keys(this.pictures), ...Object.keys(store.textures)])) {
      const want = store.textures[index] || null;
      if ((this.pictures[index] || null) === want) continue;
      this.renderer.setPicture(+index, want);
      if (want) this.pictures[index] = want; else delete this.pictures[index];
    }
  }

  // the model an enemy is drawn with (bh_characters: E<kind>_<class>.NBD), fetched the first time it is asked for
  characterFor(n) {
    const key = `${n.id}:${n.cls}`;
    if (!(key in this.characterFiles)) {
      this.characterFiles[key] = undefined;
      api.character(n.id, n.cls).then((r) => {
        if (r.file && !this.renderer.characters[r.file]) this.renderer.addCharacter(r.file, r.model);
        if (!r.file) console.warn(`enemy kind ${n.id} class ${n.cls}: ${r.reason}`);
        this.characterFiles[key] = r.file;
        this.redraw();
      }).catch((e) => { console.error(`enemy kind ${n.id} class ${n.cls}: its model couldn't be had`, e); this.characterFiles[key] = null; this.redraw(); });
    }
    return this.characterFiles[key];
  }

  // the room's enemies as models standing on the floor, turned their way (in the 3D views; the plan marks them)
  enemyModels() {
    const r = roomOf(store.room), out = [];
    if (!r || store.mode === "plan" || !store.show.enemies) return out;
    for (const n of r.enemies) {
      if (n.modes === 0 && !store.selection.some((s) => s.c === "enemies" && s.file === r.file && s.i === n.i)) continue;
      const file = this.characterFor(n);
      if (!file) continue;
      const a = n.facing / 65536 * Math.PI * 2, c = Math.cos(a), s = Math.sin(a), y = this.standY(r.file, n.pos[0], n.pos[2], n.pos[1]);
      out.push({ file, at: [c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, n.pos[0], y, n.pos[2], 1] });      // the model faces +z: turned so it looks along (sin a, cos a), as the overlay's facing line
    }
    return out;
  }

  // where something at (x, z) stands: the floor nearest below its own height `y` (an enemy's record has the height it stands at: 500 upstairs, 1800 on
  // the roof), or `y` itself where the room has no floor under it; with no height known, the highest floor up to 600 (0 where there is none)
  standY(file, x, z, y) {
    const h = this.floorAt(file, x, z, y === undefined ? 600 : y + 30);
    return h === null ? (y ?? 0) : h;
  }

  floorAt(file, x, z, below) {
    const m = this.renderer.models[file];
    return m ? m.floors.heightAt(x, z, below) : null;
  }

  // ---- drawing
  resize() {
    const w = this.view.clientWidth, h = this.view.clientHeight, dpr = window.devicePixelRatio || 1;
    this.renderer.resize(w, h, dpr);
    this.ov.width = Math.floor(w * dpr); this.ov.height = Math.floor(h * dpr);
    this.camera.resize(w, h);
    this.dpr = dpr;
    this.redraw();
  }

  redraw() {
    if (this.pending) return;
    this.pending = true;
    requestAnimationFrame(() => { this.pending = false; this.draw(); });
  }

  visibleRooms() {
    // the game holds one room at a time, and each room's model carries its own copies of the buildings around it and flat pictures for the view out of its windows:
    // drawn together, neighbouring rooms' copies fight over the same places and the pictures hang in the air. So the 3D views draw the room you are in (flying through
    // a door takes you into the next one, followEye), and the plan from above shows them all
    return store.mode === "plan" ? store.scene.rooms.map((r) => r.file) : [store.room];
  }

  // Studio's own picture of what the camera sees (the game's frame only, no markers), as a PNG data URL: for setting it side by side with the game's own picture
  capture() {
    this.draw();
    const gl = this.renderer.gl, r = this.camera.rect, k = this.dpr;
    const x = Math.round(r.x * k), y = Math.round((this.camera.h - r.y - r.h) * k), w = Math.round(r.w * k), h = Math.round(r.h * k);
    const px = new Uint8Array(w * h * 4);
    gl.readPixels(x, y, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
    const canvas = document.createElement("canvas");
    canvas.width = w; canvas.height = h;
    const ctx = canvas.getContext("2d"), image = ctx.createImageData(w, h);
    for (let row = 0; row < h; row++) image.data.set(px.subarray(row * w * 4, (row + 1) * w * 4), (h - 1 - row) * w * 4);
    ctx.putImageData(image, 0, 0);
    return canvas.toDataURL("image/png");
  }

  draw() {
    if (!store.scene) return;
    sync(this.renderer, store.scene);
    this.syncPictures();
    const rooms = this.visibleRooms();
    const dim = {};
    for (const f of rooms) dim[f] = f === store.room ? 1 : 0.5;
    if (store.show.models) this.renderer.render(this.camera, rooms, { textured: store.show.textures, dim, hidden: this.hiddenSets(), winding: store.winding, fog: this.cameraFog(), characters: this.enemyModels() });
    else this.renderer.render(this.camera, [], {});
    overlay.draw(this.ctx, this.camera, this.dpr, rooms, this.hover);
    this.drawGhost();
    this.drawGrid();
    emit("camera");
  }

  drawGrid() {
    if (!store.show.grid || this.camera.mode !== "plan") return;
    const c = this.camera, ctx = this.ctx;
    let step = 100;
    while (step / c.scale < 28) step *= 5;
    const tl = c.onPlane(0, 0, 0), br = c.onPlane(c.w, c.h, 0);
    if (!tl || !br) return;
    ctx.save();
    ctx.strokeStyle = "rgba(255,255,255,.035)"; ctx.lineWidth = 1;
    ctx.beginPath();
    for (let x = Math.floor(tl[0] / step) * step; x <= br[0]; x += step) { const s = c.project([x, 0, 0]); ctx.moveTo(Math.round(s[0]) + .5, 0); ctx.lineTo(Math.round(s[0]) + .5, c.h); }
    for (let z = Math.floor(tl[1] / step) * step; z <= br[1]; z += step) { const s = c.project([0, 0, z]); ctx.moveTo(0, Math.round(s[1]) + .5); ctx.lineTo(c.w, Math.round(s[1]) + .5); }
    ctx.stroke();
    ctx.restore();
  }

  drawGhost() {
    if (store.tool === "floor" && this.wallStart && this.cursor) {
      const g = ground(this.camera, this.cursor[0], this.cursor[1], this.renderer);
      if (g) {
        const c = [[this.wallStart[0], this.wallStart[1]], [snapTo(g[0]), this.wallStart[1]], [snapTo(g[0]), snapTo(g[2])], [this.wallStart[0], snapTo(g[2])]].map((p) => this.camera.project([p[0], g[1], p[1]])), ctx = this.ctx;
        ctx.save(); ctx.strokeStyle = "#fff"; ctx.fillStyle = "rgba(255,255,255,.12)"; ctx.setLineDash([5, 4]); ctx.lineWidth = 1.5; ctx.beginPath(); c.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1]))); ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.restore();
      }
    }
    if (store.tool === "wall" && this.wallStart && this.cursor) {
      const g = ground(this.camera, this.cursor[0], this.cursor[1], this.renderer);
      if (g) {
        const a = this.camera.project([this.wallStart[0], g[1], this.wallStart[1]]), b = this.camera.project([snapTo(g[0]), g[1], snapTo(g[2])]), ctx = this.ctx;
        ctx.save(); ctx.strokeStyle = "#fff"; ctx.setLineDash([5, 4]); ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke(); ctx.restore();
      }
    }
    if (store.tool === "library" && store.libraryPick && this.cursor && !this.drag) { this.drawLibraryGhost(); return; }
    const tool = [...PLACE, ...THINGS].find((t) => t.id === store.tool);
    if (!tool || !this.cursor || this.drag) return;
    const at = ground(this.camera, this.cursor[0], this.cursor[1], this.renderer);
    if (!at) return;
    const ctx = this.ctx, x = snapTo(at[0]), z = snapTo(at[2]);
    const pts = (tool.id === "camera" || tool.id === "item" || tool.id === "enemy") ? null : [[x + 50, z - 50], [x - 50, z - 50], [x - 50, z + 50], [x + 50, z + 50]].map((p) => this.camera.project([p[0], at[1], p[1]]));
    ctx.save();
    ctx.strokeStyle = "#fff"; ctx.fillStyle = "rgba(255,255,255,.16)"; ctx.setLineDash([5, 4]); ctx.lineWidth = 1.5;
    if (pts) { ctx.beginPath(); pts.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1]))); ctx.closePath(); ctx.fill(); ctx.stroke(); }
    else { const s = this.camera.project([x, at[1] + 220, z]); ctx.beginPath(); ctx.arc(s[0], s[1], 8, 0, 7); ctx.fill(); ctx.stroke(); }
    ctx.restore();
    this.groundAt = [x, at[1], z];
  }

  // the Library object about to be put down: its box turned as it will be (R turns it), with its picture standing in it, where the click would put it
  drawLibraryGhost() {
    const pick = store.libraryPick, src = this.renderer.models[modelKey(pick)], mesh = src && src.meshes.find((x) => x.mesh === pick.mesh);
    const at = ground(this.camera, this.cursor[0], this.cursor[1], this.renderer);
    if (!mesh || !at) return;
    let x = snapTo(at[0]), z = snapTo(at[2]);
    const c = Math.cos(store.libraryRot || 0), s = Math.sin(store.libraryRot || 0), lc = [(mesh.lo[0] + mesh.hi[0]) / 2, (mesh.lo[2] + mesh.hi[2]) / 2];
    const room = roomOf(store.room);
    if (room) [x, z] = neighbourSnap(room, x, z, mesh.hi[0] - mesh.lo[0], mesh.hi[2] - mesh.lo[2], store.libraryRot || 0);     // its sides meet the sides of what stands near
    const corner = (i) => {
      const dx = (i & 1 ? mesh.hi[0] : mesh.lo[0]) - lc[0], dz = (i & 4 ? mesh.hi[2] : mesh.lo[2]) - lc[1], y = (i & 2 ? mesh.hi[1] : mesh.lo[1]) - mesh.lo[1];
      return this.camera.project([x + dx * c + dz * s, at[1] + y, z - dx * s + dz * c]);
    };
    const p = [0, 1, 2, 3, 4, 5, 6, 7].map(corner), ctx = this.ctx;
    if (p.some((q) => q[2] < -1)) return;
    if (!this.ghostImg || this.ghostImg.key !== `${modelKey(pick)}:${pick.mesh}`) {
      const r = this.renderer.thumbnail(modelKey(pick), pick.mesh, 128, true);
      if (r) { const img = new Image(); img.src = r.url; this.ghostImg = { key: `${modelKey(pick)}:${pick.mesh}`, img }; }
    }
    ctx.save();
    ctx.strokeStyle = "rgba(255,255,255,.85)"; ctx.setLineDash([4, 3]); ctx.lineWidth = 1.2;
    ctx.beginPath();
    for (const [a, b] of [[0, 1], [1, 3], [3, 2], [2, 0], [4, 5], [5, 7], [7, 6], [6, 4], [0, 4], [1, 5], [2, 6], [3, 7]]) { ctx.moveTo(p[a][0], p[a][1]); ctx.lineTo(p[b][0], p[b][1]); }
    ctx.stroke();
    const xs = p.map((q) => q[0]), ys = p.map((q) => q[1]), w = Math.max(...xs) - Math.min(...xs), h = Math.max(...ys) - Math.min(...ys), side = Math.max(w, h);
    if (this.ghostImg && this.ghostImg.img.complete && side > 6) { ctx.globalAlpha = 0.8; ctx.drawImage(this.ghostImg.img, (Math.min(...xs) + Math.max(...xs)) / 2 - side / 2, (Math.min(...ys) + Math.max(...ys)) / 2 - side / 2, side, side); }
    ctx.restore();
    this.groundAt = [x, at[1], z];
  }

  // the view that suits the mode: the room's camera for the game view and Build, the whole room from above for the plan
  frameRoom(file, animate) {
    if (store.mode === "game") { if (file === store.room) this.useGameCamera(); return; }
    if (store.mode === "3d") { if (file === store.room) this.enterFree(); return; }
    this.overview(file, animate);
  }

  // the whole room in the pane, from where the view is looking
  overview(file, animate) {
    const m = this.renderer.models[file], r = roomOf(file);
    let box = m && m.bounds[0] < 1e8 ? m.bounds.slice() : null;
    // the model can hold far backdrops: frame the room's own areas and cameras instead when they are known
    if (r) {
      const pts = [];
      r.areas.forEach((a) => a.quad.forEach((p) => { if (Math.abs(p[0]) < 1e7) pts.push([p[0], p[1]]); }));
      r.cameras.forEach((c) => { if (c.eye[0] > -1e7) pts.push([c.eye[0], c.eye[2]]); });
      if (pts.length) {
        const xs = pts.map((p) => p[0]), zs = pts.map((p) => p[1]);
        box = [Math.min(...xs) - 400, box ? box[1] : 0, Math.min(...zs) - 400, Math.max(...xs) + 400, box ? box[4] : 300, Math.max(...zs) + 400];
      }
    }
    if (box) this.camera.frame(box);
    this.redraw();
  }

  frameAll() {
    const all = [];
    for (const r of store.scene.rooms) r.areas.forEach((a) => a.quad.forEach((p) => { if (Math.abs(p[0]) < 1e7) all.push(p); }));
    if (!all.length) return;
    const xs = all.map((p) => p[0]), zs = all.map((p) => p[1]);
    this.camera.frame([Math.min(...xs), 0, Math.min(...zs), Math.max(...xs), 300, Math.max(...zs)], 1.1);
    this.redraw();
  }

  // ---- the pointer
  updateCursor() {
    const t = store.tool;
    this.ov.style.cursor = t === "pan" ? "grab" : (t === "wall" || t === "floor" || [...PLACE, ...THINGS].some((p) => p.id === t)) ? "crosshair" : "default";
  }

  local(e) { const r = this.ov.getBoundingClientRect(); return [e.clientX - r.left, e.clientY - r.top]; }

  bind() {
    const ov = this.ov;
    ov.addEventListener("contextmenu", (e) => e.preventDefault());
    ov.addEventListener("wheel", (e) => {
      e.preventDefault();
      const [x, y] = this.local(e);
      const k = Math.exp(e.deltaY * (e.ctrlKey ? 0.01 : 0.0016));
      this.camera.zoom(k, x, y);
      this.followEye();
      this.redraw();
    }, { passive: false });
    ov.addEventListener("pointerdown", (e) => this.down(e));
    ov.addEventListener("pointermove", (e) => this.move(e));
    ov.addEventListener("pointerup", (e) => this.up(e));
    ov.addEventListener("pointerleave", () => { this.cursor = null; if (!this.drag) { this.hover = null; this.redraw(); } });
    ov.addEventListener("dblclick", (e) => {
      const [x, y] = this.local(e);
      const ref = overlay.pick(x, y);
      if (ref) { const o = objectOf(ref); if (o) this.focus(ref); }
    });
  }

  focus(ref) {
    const o = objectOf(ref);
    if (!o) return;
    const c = centre(ref.c, o);
    this.camera.target = [c[0], c[1], c[2]];
    this.redraw();
  }

  down(e) {
    this.ov.setPointerCapture(e.pointerId);
    const [x, y] = this.local(e);
    const start = { x, y, button: e.button, shift: e.shiftKey, moved: false };
    const tool = store.tool;
    const pan = e.button === 1 || e.button === 2 || this.space || tool === "pan";
    if (pan && store.mode === "game" && e.button === 2) setMode("3d");           // right-drag in the game view turns into looking around freely
    if (pan) { this.drag = { ...start, type: this.camera.mode === "orbit" && (e.button === 2 || e.altKey) ? "orbit" : "pan" }; this.ov.style.cursor = "grabbing"; return; }
    if (e.button !== 0) return;
    if (tool === "wall" || tool === "floor") { this.drag = { ...start, type: "wall" }; return; }
    const placing = [...PLACE, ...THINGS].find((p) => p.id === tool);
    if (placing) { this.drag = { ...start, type: "place", tool: placing }; return; }
    const fixed = FIXED.find((p) => p.id === tool);
    if (fixed) { emit("note", fixed.soon, "warn"); return; }
    // select: a handle of the selection, then a thing, then empty space
    const handle = overlay.pickHandle(x, y);
    if (handle && store.selection.length === 1) {
      const ref = store.selection[0], o = objectOf(ref);
      this.drag = { ...start, type: "handle", handle, ref, orig: clone(o) };
      return;
    }
    const ref = overlay.pick(x, y);
    if (ref) {
      const additive = e.shiftKey;
      if (!store.selection.some((s) => refEq(s, ref))) select([ref], additive);
      else if (additive) select(store.selection.filter((s) => !refEq(s, ref)));
      const g = ground(this.camera, x, y, this.renderer);
      this.drag = { ...start, type: "move", refs: store.selection.map((s) => ({ ref: s, orig: clone(objectOf(s)) })), origin: g, plane: ref };
      return;
    }
    this.drag = { ...start, type: this.camera.mode === "orbit" ? "orbit" : "pan", clear: true };
  }

  move(e) {
    const [x, y] = this.local(e);
    this.cursor = [x, y];
    const d = this.drag;
    if (!d) {
      const ref = overlay.pick(x, y);
      const changed = !(ref === this.hover || refEq(ref, this.hover));
      this.hover = ref;
      if (store.tool === "select") this.ov.style.cursor = overlay.pickHandle(x, y) ? "grab" : ref ? "pointer" : "default";
      if (changed || store.tool === "wall" || store.tool === "floor" || [...PLACE, ...THINGS].some((p) => p.id === store.tool)) this.redraw();
      emit("pointer", this.worldAt(x, y));
      return;
    }
    const dx = x - d.x, dy = y - d.y;
    if (!d.moved && Math.hypot(dx, dy) < 3) return;
    d.moved = true;
    if (d.type === "pan") { this.camera.pan(dx, dy); d.x = x; d.y = y; this.redraw(); }
    else if (d.type === "orbit") { this.camera.orbit(dx, dy); d.x = x; d.y = y; this.redraw(); }
    else if (d.type === "move") this.dragMove(x, y, e.altKey);
    else if (d.type === "handle") this.dragHandle(x, y);
    emit("pointer", this.worldAt(x, y));
  }

  up(e) {
    const d = this.drag;
    this.drag = null;
    this.updateCursor();
    if (!d) return;
    if (d.type === "wall") {
      const g = ground(this.camera, d.x, d.y, this.renderer);
      if (g) {
        const p = [snapTo(g[0]), snapTo(g[2])];
        if (!this.wallStart) this.wallStart = p;
        else if (store.tool === "floor") {
          if (Math.abs(p[0] - this.wallStart[0]) > 20 && Math.abs(p[1] - this.wallStart[1]) > 20) {
            const f = addFloor(roomOf(store.room), this.wallStart, p, Math.round(g[1]) + 100);
            commit("Add floor");
            select([{ c: "floors", file: store.room, i: f.i }]);
          }
          this.wallStart = null;
        } else if (Math.hypot(p[0] - this.wallStart[0], p[1] - this.wallStart[1]) > 5) {
          const w = addWall(roomOf(store.room), this.wallStart, p, g[1]);
          commit("Add wall");
          select([{ c: "walls", file: store.room, i: w.i }]);
          this.wallStart = p;
        }
      }
    } else if (d.type === "place" && !d.moved) {
      const at = this.groundAt || ground(this.camera, d.x, d.y, this.renderer);
      if (at) place(d.tool, at);
    } else if (d.type === "move" && d.moved) commit(`Move ${d.refs.length > 1 ? d.refs.length + " things" : describe(d.plane.c, objectOf(d.plane) || {}).label.toLowerCase()}`);
    else if (d.type === "handle" && d.moved) commit("Reshape");
    else if (!d.moved && d.clear) select([]);
    this.redraw();
  }

  worldAt(x, y) { const p = this.camera.onPlane(x, y, 0); return p ? [Math.round(p[0]), Math.round(p[1])] : null; }

  dragMove(x, y, free) {
    const d = this.drag;
    const g = ground(this.camera, x, y, this.renderer);
    if (!g || !d.origin) return;
    let dx = g[0] - d.origin[0], dz = g[2] - d.origin[2];
    for (const item of d.refs) {
      const o = objectOf(item.ref);
      if (!o) continue;
      Object.assign(o, clone(item.orig));
      const c0 = centre(item.ref.c, item.orig);
      let ddx = dx, ddz = dz;
      if (store.snap > 1 && !free) { ddx = snapTo(c0[0] + dx) - c0[0]; ddz = snapTo(c0[2] + dz) - c0[2]; }
      if (item.ref.c === "cameras") {
        // a camera's body is its eye: the target and the box stay
        o.eye = [item.orig.eye[0] + ddx, item.orig.eye[1], item.orig.eye[2] + ddz];
      } else translate(item.ref.c, o, ddx, 0, ddz);
    }
    emit("live");
    this.redraw();
  }

  // sliding along one of the room's axes: the point of the axis line nearest the pointer's ray
  dragAxis(x, y) {
    const d = this.drag, o = objectOf(d.ref), a = d.handle.axis, p0 = d.handle.from;
    const { o: ro, d: rd } = this.camera.ray(x, y);
    const w = [ro[0] - p0[0], ro[1] - p0[1], ro[2] - p0[2]], b = rd[0] * a[0] + rd[1] * a[1] + rd[2] * a[2], dd = rd[0] * w[0] + rd[1] * w[1] + rd[2] * w[2], e = w[0] * a[0] + w[1] * a[1] + w[2] * a[2], den = 1 - b * b;
    if (Math.abs(den) < 1e-4) return;
    const u = (e - b * dd) / den;
    if (d.u0 === undefined) d.u0 = u;
    const step = store.snap > 1 ? store.snap : 1, along = Math.round((u - d.u0) / step) * step;
    Object.assign(o, clone(d.orig));
    translate(d.ref.c, o, along * a[0], 0, along * a[2]);
    emit("live"); this.redraw();
  }

  // raising and lowering: the point of the thing's vertical line nearest the pointer's ray
  dragLift(x, y) {
    const d = this.drag, o = objectOf(d.ref);
    const { o: ro, d: rd } = this.camera.ray(x, y), p0 = d.handle.p;
    const w = [ro[0] - p0[0], ro[1] - p0[1], ro[2] - p0[2]], b = rd[1], dd = rd[0] * w[0] + rd[1] * w[1] + rd[2] * w[2], e = w[1], den = 1 - b * b;
    if (Math.abs(den) < 1e-4) return;
    const u = (e - b * dd) / den;                  // how far up the line the nearest point is, from the handle's own height
    if (d.u0 === undefined) d.u0 = u;
    const dy = Math.round((u - d.u0) / 10) * 10;
    Object.assign(o, clone(d.orig));
    if (d.ref.c === "items" || d.ref.c === "enemies") o.pos = [d.orig.pos[0], d.orig.pos[1] + dy, d.orig.pos[2]];
    else if (d.ref.c === "areas") o.h = [d.orig.h[0] + dy, d.orig.h[1] + dy];
    else if (d.ref.c === "floors") o.y = d.orig.y + dy;
    else translate(d.ref.c, o, 0, dy, 0);
    emit("live"); this.redraw();
  }

  // turning and resizing an object: its distance and bearing from its middle, on the ground under it
  dragShape(x, y) {
    const d = this.drag, o = objectOf(d.ref), c = d.orig.wc;
    const p = this.camera.onPlane(x, y, d.handle.p[1]);
    if (!p) return;
    const a = Math.atan2(p[0] - c[0], p[1] - c[2]), r = Math.hypot(p[0] - c[0], p[1] - c[2]);
    if (d.a0 === undefined) { const h = d.handle.p; d.a0 = Math.atan2(h[0] - c[0], h[2] - c[2]); d.r0 = Math.max(30, Math.hypot(h[0] - c[0], h[2] - c[2])); }     // measured from where the handle stands, so a first move far away isn't taken as the start
    Object.assign(o, clone(d.orig));
    if (d.handle.id === "turn") {
      let yaw = d.orig.rot[1] + (a - d.a0);
      if (store.snap > 1) yaw = Math.round(yaw / (Math.PI / 12)) * (Math.PI / 12);
      setPose(o, { rot: [d.orig.rot[0], yaw, d.orig.rot[2]] });
    } else {
      const k = Math.max(0.1, Math.min(10, r / d.r0));
      setPose(o, { scale: d.orig.scale.map((s) => s * k) });
    }
    emit("live"); this.redraw();
  }

  dragHandle(x, y) {
    const d = this.drag, o = objectOf(d.ref), h = d.handle;
    if (!o) return;
    const id = h.id;
    if (id === "lift") return this.dragLift(x, y);
    if (h.kind === "axis") return this.dragAxis(x, y);
    if (id === "turn" || id === "size") return this.dragShape(x, y);
    const y0 = h.p[1], p = this.camera.onPlane(x, y, y0);
    if (!p) return;
    const sx = snapTo(p[0]), sz = snapTo(p[1]);
    if (id.startsWith("corner")) { const n = +id.slice(6); o.quad = o.quad.map((q, i) => (i === n ? [sx, sz] : q)); }
    else if (id === "target") o.target = [sx, o.target[1], sz];
    else if (id === "wa") o.a = [sx, sz];
    else if (id === "wb") o.b = [sx, sz];
    else if (id === "fa") o.min = [sx, sz];
    else if (id === "fb") o.max = [sx, sz];
    else if (id.startsWith("box")) { const n = +id.slice(3); o.box = o.box.map((q, i) => (i === n || i === n + 4 ? [sx, q[1], sz] : q)); }
    else if (id.startsWith("zone")) { const n = +id.slice(4); o.box = o.box.map((q, i) => (i === n || i === n + 4 ? [sx, q[1], sz] : q)); }
    else if (id === "facing") {
      const a = Math.atan2(p[0] - o.pos[0], p[1] - o.pos[2]);
      let f = Math.round(((a / (Math.PI * 2)) * 65536 + 65536) % 65536);
      if (store.snap > 1) f = Math.round(f / 1024) * 1024 % 65536;
      o.facing = f;
    }
    emit("live");
    this.redraw();
  }
}
