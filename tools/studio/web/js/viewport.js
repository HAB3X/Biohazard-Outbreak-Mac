// The viewport: draws the models and the markers, and turns the pointer into navigation, selection, moving and placing.
import { Camera } from "./camera.js";
import { Renderer } from "./gl.js";
import * as overlay from "./overlay.js";
import { store, on, emit, objectOf, roomOf, select, commit, clone, translate, centre, refEq, describe } from "./scene.js";
import { PLACE, FIXED, THINGS, place, ground, snapTo } from "./tools.js";
import { api } from "./api.js";

export class Viewport {
  constructor() {
    this.view = document.getElementById("view");
    this.gl = document.getElementById("gl");
    this.ov = document.getElementById("ov");
    this.ctx = this.ov.getContext("2d");
    this.renderer = new Renderer(this.gl);
    this.camera = new Camera();
    this.hover = null;
    this.drag = null;
    this.cursor = null;       // the pointer in the view, for the ghost of what a tool will place
    this.space = false;
    this.pending = false;
    this.renderer.onTexture = () => this.redraw();
    new ResizeObserver(() => this.resize()).observe(this.view);
    this.bind();
    this.resize();
    on("selection", () => this.redraw());
    on("changed", () => this.redraw());
    on("room", () => this.redraw());
    on("mode", () => { this.camera.setMode(store.mode === "3d" ? "orbit" : "plan"); this.redraw(); });
    on("tool", () => { this.updateCursor(); this.redraw(); });
  }

  // ---- loading the models in the background, the room being worked in first
  async loadModels(progress) {
    const rooms = [...store.scene.rooms].sort((a, b) => (a.file === store.room ? -1 : b.file === store.room ? 1 : 0));
    let done = 0;
    for (const r of rooms) {
      const file = r.file.replace(".rdt", ".nbd");
      try {
        if (!store.models[r.file]) {
          const json = await api.model(file);
          if (!json.error) { store.models[r.file] = true; this.renderer.add(r.file, json); }
        }
      } catch (e) { /* a room without a model is drawn without one */ }
      progress && progress(++done, rooms.length);
      this.redraw();
    }
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
    return store.scene.rooms.map((r) => r.file);
  }

  draw() {
    if (!store.scene) return;
    const rooms = this.visibleRooms();
    const dim = {};
    for (const f of rooms) dim[f] = f === store.room ? 1 : 0.5;
    if (store.show.models) this.renderer.render(this.camera, rooms, { textured: store.show.textures, dim });
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

  frameRoom(file, animate) {
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
    this.ov.style.cursor = t === "pan" ? "grab" : [...PLACE, ...THINGS].some((p) => p.id === t) ? "crosshair" : "default";
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
    if (pan) { this.drag = { ...start, type: e.altKey && this.camera.mode === "orbit" ? "orbit" : "pan" }; this.ov.style.cursor = "grabbing"; return; }
    if (e.button !== 0) return;
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
      if (changed || [...PLACE, ...THINGS].some((p) => p.id === store.tool)) this.redraw();
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
    if (d.type === "place" && !d.moved) {
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

  dragHandle(x, y) {
    const d = this.drag, o = objectOf(d.ref), h = d.handle;
    if (!o) return;
    const y0 = h.p[1], p = this.camera.onPlane(x, y, y0);
    if (!p) return;
    const sx = snapTo(p[0]), sz = snapTo(p[1]);
    const id = h.id;
    if (id.startsWith("corner")) { const n = +id.slice(6); o.quad = o.quad.map((q, i) => (i === n ? [sx, sz] : q)); }
    else if (id === "target") o.target = [sx, o.target[1], sz];
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
