// Scenery: the objects (meshes) of a room's model. Each stands on a bone of the skeleton; moving, turning, sizing or hiding one changes that bone only, so the file's size never changes.
import { store, clone } from "./scene.js";
import { boundsOf } from "./objimport.js";

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
    const triangles = mesh.groups.reduce((n, g) => n + g.count / 3, 0), key = [triangles, ...[0, 1, 2].map((k) => Math.round(mesh.hi[k] - mesh.lo[k]))].join(",");
    list.push({ i: mesh.mesh, key, bone: mesh.bone, parent: b[0], scale: clone(b[1]), rot: clone(b[2]), off: clone(b[3]), lo: mesh.lo, hi: mesh.hi, lc, wc: apply(w, lc), cs, inv: null });
  }
  for (const scene of [store.scene, store.orig]) {
    const r = scene.rooms.find((x) => x.file === file);
    if (r) { r.scenery = clone(list); r.objectBase = m.bones.length; r.pictures = m.tids; }     // a copy's number is the first beyond the model's own objects (one per bone after the first two)
  }
  const saved = store.history[store.at].rooms[file];
  const r = store.scene.rooms.find((x) => x.file === file);
  if (saved && r) applyEdits(r, saved);
}

// the copies made of objects come first (they are new entries), then the changes to each object
export function applyEdits(room, spec) {
  if (!room.scenery) return;
  (spec.new_objects || []).forEach((n) => {
    const like = (room.scenery || []).find((x) => x.i === n.like && !x.isNew);
    if (n.library) standIn(room, n.library);
    else if (like) { const c = duplicate(room, like, { i: nextNumber(room), silent: true }); if (n.geometry) giveGeometry(c, n.geometry); }
  });
  for (const [i, d] of Object.entries(spec.scenery || {})) {
    const s = (room.scenery || []).find((x) => x.i === +i);
    if (!s) continue;
    if (d.scale) s.scale = clone(d.scale);
    if (d.rotation) s.rot = clone(d.rotation);
    if (d.offset) s.off = clone(d.offset);
  }
}

const copies = (room) => (room.scenery || []).filter((x) => x.isNew);
const nextNumber = (room) => (room.objectBase || 0) - 2 + copies(room).length;     // objects are numbered from 0, each on the bone two after it

// a copy of an object: it stands beside it, on a bone of its own, and is written into the room's model (the file grows)
export function duplicate(room, s, o = {}) {
  const c = clone(s);
  c.i = o.i ?? nextNumber(room);
  c.bone = c.i + 2;
  c.isNew = true;
  c.like = s.isNew ? s.like : s.i;
  if (!o.silent) c.off = [c.off[0] + 150, c.off[1], c.off[2] + 150];
  room.scenery.push(c);
  return c;
}

export function removeCopy(room, s) {
  room.scenery = room.scenery.filter((x) => x !== s);
  copies(room).forEach((c, k) => { c.i = (room.objectBase || 0) - 2 + k; c.bone = c.i + 2; });   // the numbers stay in a row
}

// an object made from geometry: standing plain (no scale or turn) where its bone puts it
export function giveGeometry(s, g) {
  Object.assign(s, boundsOf(g), { geometry: g, scale: [1, 1, 1], rot: [0, 0, 0] });
}

// a new object of your own geometry; `target` is where its foot stands in the world
export function addModel(room, g, renderer, target) {
  const like = room.scenery.find((x) => !x.isNew);
  const c = duplicate(room, like, { silent: true });
  giveGeometry(c, g);
  const m = renderer.models[room.file], pw = m.world[c.parent];
  const inv = pw ? inverse3(pw) : [1, 0, 0, 0, 1, 0, 0, 0, 1], t = pw ? [pw[12], pw[13], pw[14]] : [0, 0, 0];
  const d = [target[0] - t[0], target[1] - t[1], target[2] - t[2]];
  c.off = [0, 1, 2].map((r) => inv[r * 3] * d[0] + inv[r * 3 + 1] * d[1] + inv[r * 3 + 2] * d[2]);
  return c;
}

export const newObjects = (room) => copies(room).map((c) => (c.library ? { like: c.like, library: c.library } : c.geometry ? { like: c.like, geometry: c.geometry } : { like: c.like }));

// an object of another room's model (`lib` is {room, mesh}), standing plain on a bone of its own; the file grows by it
function standIn(room, lib) {
  const src = store.renderer && store.renderer.models[lib.room];
  const mesh = src && src.meshes.find((x) => x.mesh === lib.mesh), here = store.renderer && store.renderer.models[room.file];
  const like = room.scenery.find((x) => !x.isNew);
  if (!mesh || !here || !like) throw new Error(`The Library object ${lib.mesh} of ${lib.room} can't be set up in ${room.file}: its model isn't loaded`);
  const c = duplicate(room, like, { silent: true });
  const lc = [0, 1, 2].map((k) => (mesh.lo[k] + mesh.hi[k]) / 2);
  Object.assign(c, { library: lib, lo: mesh.lo, hi: mesh.hi, lc, parent: here.bones[2][0], scale: [1, 1, 1], rot: [0, 0, 0], off: [0, 0, 0] });
  delete c.geometry;
  return c;
}

// a library object put down so that its middle stands over `target` (x, y, z) with its lowest point on the ground there
export function addLibrary(room, renderer, lib, target) {
  const c = standIn(room, lib);
  const m = renderer.models[room.file], pw = m.world[c.parent];
  const inv = pw ? inverse3(pw) : [1, 0, 0, 0, 1, 0, 0, 0, 1], t = pw ? [pw[12], pw[13], pw[14]] : [0, 0, 0];
  const d = [target[0] - t[0] - c.lc[0], target[1] - t[1] - c.lo[1], target[2] - t[2] - c.lc[2]];
  c.off = [0, 1, 2].map((r) => inv[r * 3] * d[0] + inv[r * 3 + 1] * d[1] + inv[r * 3 + 2] * d[2]);
  return c;
}

export function diff(room, orig) {
  const out = {};
  for (const s of room.scenery || []) {
    if (s.isNew) { out[s.i] = { scale: s.scale, rotation: s.rot, offset: s.off }; continue; }
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
    const key = JSON.stringify(r.scenery.map((s) => [s.scale, s.rot, s.off, s.like, s.geometry && s.geometry.material, s.library && s.library.mesh]));
    if (stamps[r.file] !== key) {
      stamps[r.file] = key;
      const changes = {}, clones = [];
      for (const s of r.scenery) {
        const bone = [s.parent, s.scale, s.rot, s.off];
        if (s.isNew) clones.push({ like: s.like, bone, geometry: s.geometry, library: s.library }); else changes[s.bone] = bone;
      }
      renderer.setBones(r.file, changes, clones);
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
  return !s.isNew && room.areas.some((a) => [0, 1, 2].includes(a.tag) && (() => {
    const xs = a.quad.map((p) => p[0]), zs = a.quad.map((p) => p[1]), sx = s.cs.map((p) => p[0]), sz = s.cs.map((p) => p[2]);
    return Math.min(...sx) < Math.max(...xs) + 200 && Math.max(...sx) > Math.min(...xs) - 200 && Math.min(...sz) < Math.max(...zs) + 200 && Math.max(...sz) > Math.min(...zs) - 200;
  })());
}

// turning and resizing about the object's own middle, not about its bone's origin (the game turns a bone about the origin, so a turned object would swing): the offset is moved
// by what keeps the middle where it was. The local matrix is the offset, then turns about z, y and x, then the scale (the order the model's viewer and the game share)
const turn = (r, v) => {
  const [a, b, c] = r;
  const [sx, cx] = [Math.sin(a), Math.cos(a)], [sy, cy] = [Math.sin(b), Math.cos(b)], [sz, cz] = [Math.sin(c), Math.cos(c)];
  let [x, y, z] = v;
  [y, z] = [y * cx - z * sx, y * sx + z * cx];          // about x
  [x, z] = [x * cy + z * sy, -x * sy + z * cy];         // about y
  [x, y] = [x * cz - y * sz, x * sz + y * cz];          // about z
  return [x, y, z];
};
const place = (s, scale, rot) => turn(rot, s.lc.map((v, k) => v * scale[k]));

export function setPose(s, { rot, scale }) {
  const before = place(s, s.scale, s.rot), nr = rot || s.rot, ns = scale || s.scale, after = place(s, ns, nr);
  s.off = s.off.map((v, k) => v + before[k] - after[k]);
  s.rot = nr;
  s.scale = ns;
}

// everything the room has taken out of it (its objects but not its doors, enemies, items, walls and ground), as the Clear this room button and a new level start from
export function emptyRoom(r) {
  for (const s of r.scenery || []) if (!s.isNew && !isDoor(r, s)) s.scale = [0.0001, 0.0001, 0.0001];
  for (const e of r.enemies) e.modes = 0;
  for (const it of r.items) if (!it.isNew) { it.kind = 65535; it.qty = 0; }
  r.walls = [];
  r.groundCleared = true;
}

// where a box of w x d (turned about its middle by `yaw`) put near (x, z) would touch the sides of the room's other objects: its side moved onto theirs when within `reach`
export function neighbourSnap(room, x, z, w, d, yaw, reach = 45) {
  const c = Math.abs(Math.cos(yaw)), sn = Math.abs(Math.sin(yaw)), hw = (w * c + d * sn) / 2, hd = (w * sn + d * c) / 2;      // half sizes of the turned box's outline
  let best = { x, z, dx: reach, dz: reach };
  for (const o of room.scenery || []) {
    if (!o.cs || o.scale[0] < 0.001) continue;
    const xs = o.cs.map((p) => p[0]), zs = o.cs.map((p) => p[2]), ys = o.cs.map((p) => p[1]);
    const x0 = Math.min(...xs), x1 = Math.max(...xs), z0 = Math.min(...zs), z1 = Math.max(...zs);
    if (x1 - x0 > 700 || z1 - z0 > 700 || Math.max(...ys) - Math.min(...ys) < 12) continue;      // not walls and floors
    if (z + hd > z0 - reach && z - hd < z1 + reach) for (const to of [x1 + hw, x0 - hw]) if (Math.abs(to - x) < best.dx) { best.x = to; best.dx = Math.abs(to - x); }
    if (x + hw > x0 - reach && x - hw < x1 + reach) for (const to of [z1 + hd, z0 - hd]) if (Math.abs(to - z) < best.dz) { best.z = to; best.dz = Math.abs(to - z); }
  }
  return [best.x, best.z];
}
