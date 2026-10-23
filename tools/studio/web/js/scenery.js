// Scenery: the objects (meshes) of a room's model. Each stands on a bone of the skeleton; moving, turning, sizing or hiding one changes that bone only, so the file's size never changes.
import { store, clone } from "./scene.js";

const apply = (w, p) => [w[0] * p[0] + w[4] * p[1] + w[8] * p[2] + w[12], w[1] * p[0] + w[5] * p[1] + w[9] * p[2] + w[13], w[2] * p[0] + w[6] * p[1] + w[10] * p[2] + w[14]];
const corners = (lo, hi) => [0, 1, 2, 3, 4, 5, 6, 7].map((k) => [k & 1 ? hi[0] : lo[0], k & 2 ? hi[1] : lo[1], k & 4 ? hi[2] : lo[2]]);
const SHELL = 3500;    // an object wider than this is the room's own floor and walls, not something to move

// the inverse of the 3 x 3 part of a world matrix (column-major), row-major result
function inverse3(w) {
  const [a, b, c, d, e, f, g, h, i] = [w[0], w[4], w[8], w[1], w[5], w[9], w[2], w[6], w[10]];
  const det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (Math.abs(det) < 1e-12) return null;
  const k = 1 / det;
  return [(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k, (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k, (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k];
}

// the objects of a room, from its loaded model; the page's scene and its pristine copy both get them
export function attach(renderer, file) {
  const m = renderer.models[file];
  if (!m) return;
  const list = [];
  for (const mesh of m.meshes) {
    const w = m.world[mesh.bone], b = m.bones[mesh.bone];
    if (!w || !b) continue;
    const cs = corners(mesh.lo, mesh.hi).map((p) => apply(w, p));
    const lo = [0, 1, 2].map((k) => Math.min(...cs.map((p) => p[k]))), hi = [0, 1, 2].map((k) => Math.max(...cs.map((p) => p[k])));
    if (hi[0] - lo[0] > SHELL || hi[2] - lo[2] > SHELL || mesh.hide) continue;
    const lc = [0, 1, 2].map((k) => (mesh.lo[k] + mesh.hi[k]) / 2);
    list.push({ i: mesh.mesh, bone: mesh.bone, parent: b[0], scale: clone(b[1]), rot: clone(b[2]), off: clone(b[3]), lo: mesh.lo, hi: mesh.hi, lc, wc: apply(w, lc), cs, inv: null });
  }
  for (const scene of [store.scene, store.orig]) {
    const r = scene.rooms.find((x) => x.file === file);
    if (r) r.scenery = clone(list);
  }
  const saved = (store.history[store.at].rooms[file] || {}).scenery;
  const r = store.scene.rooms.find((x) => x.file === file);
  if (saved && r) applyEdits(r, saved);
}

export function applyEdits(room, scenery) {
  for (const [i, d] of Object.entries(scenery || {})) {
    const s = (room.scenery || []).find((x) => x.i === +i);
    if (!s) continue;
    if (d.scale) s.scale = clone(d.scale);
    if (d.rotation) s.rot = clone(d.rotation);
    if (d.offset) s.off = clone(d.offset);
  }
}

export function diff(room, orig) {
  const out = {};
  for (const s of room.scenery || []) {
    const o = (orig.scenery || []).find((x) => x.i === s.i);
    if (!o) continue;
    const d = {};
    if (JSON.stringify(s.scale) !== JSON.stringify(o.scale)) d.scale = s.scale;
    if (JSON.stringify(s.rot) !== JSON.stringify(o.rot)) d.rotation = s.rot;
    if (JSON.stringify(s.off) !== JSON.stringify(o.off)) d.offset = s.off;
    if (Object.keys(d).length) out[s.i] = { scale: s.scale, rotation: s.rot, offset: s.off };
  }
  return out;
}

// moving one by a distance in the world: the bone's offset is in its parent's space
export function move(s, dx, dy, dz) {
  s.wc = [s.wc[0] + dx, s.wc[1] + dy, s.wc[2] + dz];
  const k = s.inv || [1, 0, 0, 0, 1, 0, 0, 0, 1];
  s.off = [s.off[0] + k[0] * dx + k[1] * dy + k[2] * dz, s.off[1] + k[3] * dx + k[4] * dy + k[5] * dz, s.off[2] + k[6] * dx + k[7] * dy + k[8] * dz];
}

// before each frame: the renderer takes the bones as the scene has them, and the objects' places on the screen's world follow
const stamps = {};
export function sync(renderer, scene) {
  for (const r of scene.rooms) {
    if (!r.scenery || !r.scenery.length) continue;
    const m = renderer.models[r.file];
    if (!m) continue;
    const key = JSON.stringify(r.scenery.map((s) => [s.scale, s.rot, s.off]));
    if (stamps[r.file] !== key) {
      stamps[r.file] = key;
      const changes = {};
      for (const s of r.scenery) changes[s.bone] = [s.parent, s.scale, s.rot, s.off];
      renderer.setBones(r.file, changes);
    }
    for (const s of r.scenery) {
      const w = m.world[s.bone], pw = m.world[s.parent];
      if (!w) continue;
      s.cs = corners(s.lo, s.hi).map((p) => apply(w, p));
      s.wc = apply(w, s.lc);
      s.inv = pw ? inverse3(pw) : null;
    }
  }
}

export const hidden = (s) => Math.abs(s.scale[0]) < 0.001;

// an object standing where a door, exit or inner door is belongs to it: hiding it crashed the game (measured), so the panel won't
export function isDoor(room, s) {
  return room.areas.some((a) => [0, 1, 2].includes(a.tag) && (() => {
    const xs = a.quad.map((p) => p[0]), zs = a.quad.map((p) => p[1]), sx = s.cs.map((p) => p[0]), sz = s.cs.map((p) => p[2]);
    return Math.min(...sx) < Math.max(...xs) + 200 && Math.max(...sx) > Math.min(...xs) - 200 && Math.min(...sz) < Math.max(...zs) + 200 && Math.max(...sz) > Math.min(...zs) - 200;
  })());
}
