// The scene: the scenario's rooms and what is in them, the edits made to it, undo and redo. Everything the editor changes goes through `commit`.
// The edits are the JSON tools/bh_room_patch.py reads; the scene after any number of undos is the pristine scene with the edits applied, so history is a list of edits.
import { compile } from "./rules.js";
import * as sceneryOf from "./scenery.js";

export const clone = (o) => JSON.parse(JSON.stringify(o));
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// ---- the kinds of thing in a room: what the editor calls them, what they look like and where they live in the scene
export const AREA_TAGS = {
  0: { key: "door", label: "Door", color: "door", icon: "door" },
  1: { key: "exit", label: "Exit", color: "exit", icon: "exit" },
  2: { key: "roomdoor", label: "Inner door", color: "roomdoor", icon: "door" },
  3: { key: "pickup", label: "Pickup trigger", color: "item", icon: "item" },
  4: { key: "lock", label: "Lock", color: "item", icon: "lock" },
  5: { key: "examine", label: "Examine spot", color: "message", icon: "message" },
  6: { key: "ladder", label: "Ladder", color: "ladder", icon: "ladder" },
  7: { key: "glint", label: "Item glint", color: "item", icon: "item" },
  10: { key: "tag10", label: "Trigger (10)", color: "marker", icon: "zone" },
  11: { key: "message", label: "Message", color: "message", icon: "message" },
  12: { key: "scriptzone", label: "Script zone", color: "zone", icon: "zone" },
  13: { key: "spot", label: "Marked spot", color: "marker", icon: "marker" },
};
export const COLLECTIONS = {
  areas: { label: "Areas", icon: "zone" },
  cameras: { key: "camera", label: "Camera", color: "camera", icon: "camera" },
  lights: { key: "light", label: "Light", color: "light", icon: "light" },
  zones: { key: "zone", label: "Sound zone", color: "zone", icon: "zone" },
  points: { key: "sound", label: "Sound source", color: "zone", icon: "zone" },
  nodes: { key: "node", label: "AI node", color: "node", icon: "node" },
  walls: { key: "wall", label: "Wall", color: "wall", icon: "wall" },
  spawns: { key: "spawn", label: "Start spot", color: "spawn", icon: "spawn" },
  items: { key: "item", label: "Item", color: "item", icon: "item" },
  enemies: { key: "enemy", label: "Enemy", color: "enemy", icon: "enemy" },
  floors: { key: "floor", label: "Floor", color: "wall", icon: "grid" },
  scenery: { key: "scenery", label: "Object", color: "accent", icon: "cube" },
};

export function describe(coll, obj) {
  if (coll === "areas") return AREA_TAGS[obj.tag] || { key: "area", label: `Area (tag ${obj.tag})`, color: "marker", icon: "zone" };
  return COLLECTIONS[coll];
}

// ---- positions: every kind of thing has a place, and can be moved as a whole
export function centre(coll, o) {
  switch (coll) {
    case "areas": {
      const q = o.quad;
      return [(q[0][0] + q[1][0] + q[2][0] + q[3][0]) / 4, (o.h[0] + o.h[1]) / 2, (q[0][1] + q[1][1] + q[2][1] + q[3][1]) / 4];
    }
    case "floors": return [(o.min[0] + o.max[0]) / 2, o.y, (o.min[1] + o.max[1]) / 2];
    case "walls": return [(o.a[0] + o.b[0]) / 2, (o.y[0] + o.y[1]) / 2, (o.a[1] + o.b[1]) / 2];
    case "cameras": return o.eye;
    case "scenery": return o.wc;
    case "zones": {
      const b = o.box;
      return [(b[0][0] + b[1][0] + b[2][0] + b[3][0]) / 4, (b[0][1] + b[4][1]) / 2, (b[0][2] + b[1][2] + b[2][2] + b[3][2]) / 4];
    }
    default: return o.pos;
  }
}

export function translate(coll, o, dx, dy, dz) {
  const add = (p) => [p[0] + dx, p[1] + dy, p[2] + dz];
  switch (coll) {
    case "areas":
      o.quad = o.quad.map((p) => [p[0] + dx, p[1] + dz]);
      o.h = [o.h[0] + dy, o.h[1] + dy];
      break;
    case "floors":
      o.min = [o.min[0] + dx, o.min[1] + dz]; o.max = [o.max[0] + dx, o.max[1] + dz]; o.y += dy;
      break;
    case "walls":
      o.a = [o.a[0] + dx, o.a[1] + dz]; o.b = [o.b[0] + dx, o.b[1] + dz];
      break;
    case "cameras":
      o.eye = add(o.eye);
      o.target = add(o.target);
      o.box = o.box.map(add);
      break;
    case "zones":
      o.box = o.box.map(add);
      break;
    case "scenery":
      sceneryOf.move(o, dx, dy, dz);
      break;
    default:
      o.pos = add(o.pos);
  }
}

// ---- diffs: the scene against the pristine one, as the edits the patch builder reads
const moved = (a, b) => !same(a, b);

export function diffs(scene, orig) {
  const edits = {};
  scene.rooms.forEach((r, ri) => {
    const o = orig.rooms[ri];
    const out = {};
    const newAreas = r.areas.filter((a) => a.isNew).map((a) => {
      const f = {};
      a.fields.forEach((v, k) => { f[12 + k] = v; });
      return { like: a.like, quad: a.quad, height: a.h, fields: f };
    });
    const newCameras = r.cameras.filter((c) => c.isNew).map((c) => ({ like: c.like, eye: c.eye, target: c.target, fov: c.fov, box: c.box }));
    const newItems = r.items.filter((x) => x.isNew).map((x) => ({ like: x.like, position: x.pos, kind: x.kind, quantity: x.qty }));
    const newEnemies = r.enemies.filter((x) => x.isNew).map((x) => ({ like: x.like, variant: x.variant, position: x.pos, facing: x.facing, id: x.id, class: x.cls, modes: x.modes, hp_tier: x.hp }));
    if (newItems.length) out.new_items = newItems;
    if (newEnemies.length) out.new_enemies = newEnemies;
    if (newAreas.length) out.new_areas = newAreas;
    if (newCameras.length) out.new_cameras = newCameras;
    const put = (key, i, d) => { if (Object.keys(d).length) (out[key] = out[key] || {})[i] = d; };
    for (const c of r.cameras) {
      if (c.isNew) continue;
      const oc = o.cameras.find((x) => x.i === c.i), d = {};
      if (moved(c.eye, oc.eye)) d.eye = c.eye;
      if (moved(c.target, oc.target)) d.target = c.target;
      if (moved(c.fov, oc.fov)) d.fov = c.fov;
      if (moved(c.box, oc.box)) d.box = c.box;
      put("cameras", c.i, d);
      if (c.fog && oc.fog) {
        const f = {};
        c.fog.forEach((v, k) => { if (v !== oc.fog[k]) f[k] = v; });
        if (Object.keys(f).length) put("fog", c.i, { ints: f });
      }
    }
    for (const a of r.areas) {
      if (a.isNew) continue;
      const oa = o.areas.find((x) => x.i === a.i), d = {};
      if (moved(a.quad, oa.quad)) d.quad = a.quad;
      if (moved(a.h, oa.h)) d.height = a.h;
      const f = {};
      a.fields.forEach((v, k) => { if (v !== oa.fields[k]) f[12 + k] = v; });
      if (Object.keys(f).length) d.fields = f;
      put("areas", a.i, d);
    }
    for (const z of r.zones) {
      const oz = o.zones.find((x) => x.i === z.i), d = {};
      if (moved(z.box, oz.box)) d.box = z.box;
      if (z.sound !== oz.sound) d.sound = z.sound;
      put("sound_zones", z.i, d);
    }
    { const sc = sceneryOf.diff(r, o); if (Object.keys(sc).length) out.scenery = sc; const made = sceneryOf.newObjects(r); if (made.length) out.new_objects = made; }
    { const sw = {}; r.script.forEach((v, k) => { if (v !== o.script[k]) sw[k] = v; }); if (Object.keys(sw).length) out.script_words = sw; }
    for (const n of r.nodes) {
      const on = o.nodes.find((x) => x.i === n.i), d = {};
      if (moved(n.pos, on.pos)) d.position = n.pos;
      if (n.kind !== on.kind) d.kind = n.kind;
      if (n.id !== on.id) d.id = n.id;
      const wv = {}; n.words.forEach((v, k) => { if (v !== on.words[k]) wv[k] = v; });
      if (Object.keys(wv).length) d.words = wv;
      put("nodes", n.i, d);
    }
    for (const n of r.items) {
      if (n.isNew) continue;
      const on = o.items.find((x) => x.i === n.i), d = {};
      if (moved(n.pos, on.pos)) d.position = n.pos;
      if (n.kind !== on.kind) d.kind = n.kind;
      if (n.qty !== on.qty) d.quantity = n.qty;
      put("items", n.i, d);
    }
    for (const n of r.enemies) {
      if (n.isNew) continue;
      const on = o.enemies.find((x) => x.i === n.i), d = {};
      if (moved(n.pos, on.pos)) d.position = n.pos;
      if (n.facing !== on.facing) d.facing = n.facing;
      if (n.id !== on.id) d.id = n.id;
      if (n.cls !== on.cls) d.class = n.cls;
      if (n.modes !== on.modes) d.modes = n.modes;
      if (n.hp !== on.hp) d.hp_tier = n.hp;
      put("enemies", n.i, d);
    }
    for (const sp of r.spawns) {
      const os = o.spawns.find((x) => x.i === sp.i), d = {};
      if (sp.pos[0] !== os.pos[0]) d.x = sp.pos[0];
      if (sp.pos[2] !== os.pos[2]) d.z = sp.pos[2];
      if (sp.facing !== os.facing) d.facing = sp.facing;
      put("start_spots", sp.i, d);
    }
    for (const l of r.lights) {
      const ol = o.lights.find((x) => x.i === l.i), d = {};
      if (moved(l.pos, ol.pos)) d.position = l.pos;
      if (l.att !== ol.att) d.attenuation = l.att;
      if (l.range !== ol.range) d.range = l.range;
      if (moved(l.col, ol.col)) d.colour = l.col;
      if (moved(l.col2, ol.col2)) d.colour2 = l.col2;
      put("lights", l.i, d);
    }
    for (const p of r.points) {
      const op = o.points.find((x) => x.i === p.i), d = {};
      if (moved(p.pos, op.pos)) d.position = p.pos;
      if (p.kind !== op.kind) d.kind = p.kind;
      put("sound_points", p.i, d);
    }
    if (r.rules && r.rules.length) { out.rules = r.rules; out.script_prepend = compile(r.rules); out.script_thread = 1; }
    const strip = (w) => JSON.stringify(w.map(({ a, b, y, flags, n }) => ({ a, b, y, flags, n })));
    if (strip(r.walls) !== strip(o.walls)) out.collision = { wall_segments: JSON.parse(strip(r.walls)) };
    if (r.groundCleared) out.collision = { ...out.collision, clear_ground: true };
    if (r.floors.length) out.collision = { ...out.collision, ground_boxes: r.floors.map((f) => ({ min: f.min, max: f.max, y: -f.y })) };   // the ground file's heights are the player's, negated (measured)
    if (Object.keys(out).length) edits[r.file] = out;
  });
  const out = { rooms: edits };
  if (Object.keys(store.textures).length) out.textures = store.textures;
  if (store.startRoom !== null && store.startRoom !== store.startDefault) out.start_room = store.startRoom;
  return out;
}

// ---- applying edits to a scene (the pristine one, or what the page loaded)
export function addArea(room, like, spec = {}, scene = store.scene) {
  // a copy of a record of the game's own: one of this room's, or (a template) one of another room's, since a room may have no area of the kind wanted
  const tpl = spec.template || (like && typeof like === "object" ? like : null);
  const srcRoom = tpl ? scene.rooms.find((r) => r.file === tpl.room) || room : room;
  const index = tpl ? tpl.i : like;
  const base = srcRoom.areas.find((a) => a.i === index) || room.areas[0];
  const a = clone(base);
  a.i = Math.max(-1, ...room.areas.map((x) => x.i)) + 1;
  a.isNew = true;
  a.like = tpl ? { room: srcRoom.file, i: index } : index;
  if (spec.quad) a.quad = clone(spec.quad);
  if (spec.height) a.h = clone(spec.height);
  Object.entries(spec.fields || {}).forEach(([k, v]) => { a.fields[+k - 12] = v; });
  delete a.arrival; delete a.to; delete a.door; delete a.other; delete a.facing;
  if (a.tag === 0 || a.tag === 1) a.to = a.fields[1];
  if (a.tag === 0) a.door = a.fields[0];
  room.areas.push(a);
  return a;
}

export function addCamera(room, like, spec = {}) {
  const base = room.cameras.find((c) => c.i === like) || room.cameras[0];
  const c = clone(base);
  c.i = Math.max(-1, ...room.cameras.map((x) => x.i)) + 1;
  c.isNew = true;
  c.like = like;
  for (const k of ["eye", "target", "fov", "box"]) if (spec[k]) c[k] = clone(spec[k]);
  room.cameras.push(c);
  return c;
}

export function addItem(room, spec = {}) {
  const like = spec.like || (room.items.find((x) => !x.isNew) || { i: 2 }).i;
  const base = room.items.find((x) => x.i === like) || room.items[0];
  const all = store.scene ? store.scene.rooms : [room];
  const next = Math.max(0, ...all.flatMap((r) => r.items.map((x) => x.i))) + 1;
  const o = { i: next, kind: spec.kind ?? 300, qty: spec.quantity ?? 1, pos: clone(spec.position || [0, 0, 0]), isNew: true, like };
  room.items.push(o);
  return o;
}

export function addEnemy(room, spec = {}) {
  const all = store.scene ? store.scene.rooms : [room];
  const pool = all.flatMap((r) => r.enemies).filter((x) => !x.isNew && x.modes === 15 && x.chars === 15);
  const base = pool.find((x) => x.i === spec.like) || pool[0] || room.enemies[0];
  const next = Math.max(0, ...all.flatMap((r) => r.enemies.map((x) => x.i))) + 1;
  const o = { i: next, mode: 0, modes: spec.modes ?? 15, chars: 15, hp: spec.hp_tier ?? (base ? base.hp : 3), id: spec.id ?? (base ? base.id : 1), cls: spec.class ?? (base ? base.cls : 8),
    facing: spec.facing ?? 0, pos: clone(spec.position || [0, 0, 0]), isNew: true, like: base ? base.i : 0, variant: spec.variant ?? 0 };
  room.enemies.push(o);
  return o;
}

export function applyEdits(scene, edits) {
  for (const [file, spec] of Object.entries(edits.rooms || {})) {
    const r = scene.rooms.find((x) => x.file === file);
    if (!r) continue;
    for (const [i, d] of Object.entries(spec.sound_zones || {})) { const z = r.zones.find((x) => x.i === +i); if (z) { if (d.box) z.box = d.box; if (d.sound !== undefined) z.sound = d.sound; } }
    for (const [i, d] of Object.entries(spec.enemies || {})) {
      const n = r.enemies.find((x) => x.i === +i);
      if (!n) continue;
      if (d.position) n.pos = d.position;
      if (d.facing !== undefined) n.facing = d.facing;
      if (d.id !== undefined) n.id = d.id;
      if (d.class !== undefined) n.cls = d.class;
      if (d.modes !== undefined) n.modes = d.modes;
      if (d.hp_tier !== undefined) n.hp = d.hp_tier;
    }
    for (const [i, d] of Object.entries(spec.items || {})) { const n = r.items.find((x) => x.i === +i); if (n) { if (d.position) n.pos = d.position; if (d.kind !== undefined) n.kind = d.kind; if (d.quantity !== undefined) n.qty = d.quantity; } }
    for (const [i, d] of Object.entries(spec.nodes || {})) { const n = r.nodes.find((x) => x.i === +i); if (n) { if (d.position) n.pos = d.position; if (d.kind !== undefined) n.kind = d.kind; if (d.id !== undefined) n.id = d.id; for (const [k, v] of Object.entries(d.words || {})) n.words[+k] = v; } }
    for (const [i, v] of Object.entries(spec.script_words || {})) r.script[+i] = v;
    sceneryOf.applyEdits(r, spec);
    if (spec.rules) r.rules = clone(spec.rules);
    for (const [i, d] of Object.entries(spec.start_spots || {})) { const sp = r.spawns.find((x) => x.i === +i); if (sp) { if (d.x !== undefined) sp.pos = [d.x, sp.pos[1], sp.pos[2]]; if (d.z !== undefined) sp.pos = [sp.pos[0], sp.pos[1], d.z]; if (d.facing !== undefined) sp.facing = d.facing; } }
    for (const [i, d] of Object.entries(spec.lights || {})) { const l = r.lights.find((x) => x.i === +i); if (l) { if (d.position) l.pos = d.position; if (d.attenuation !== undefined) l.att = d.attenuation; if (d.range !== undefined) l.range = d.range; if (d.colour) l.col = d.colour; if (d.colour2) l.col2 = d.colour2; } }
    for (const [i, d] of Object.entries(spec.sound_points || {})) { const p = r.points.find((x) => x.i === +i); if (p) { if (d.position) p.pos = d.position; if (d.kind !== undefined) p.kind = d.kind; } }
    if (spec.collision && spec.collision.clear_ground) r.groundCleared = true;
    if (spec.collision && spec.collision.ground_boxes) r.floors = spec.collision.ground_boxes.map((b, i) => ({ i, min: clone(b.min), max: clone(b.max), y: -b.y }));
    if (spec.collision && spec.collision.wall_segments) r.walls = spec.collision.wall_segments.map((w, i) => ({ ...clone(w), i }));
    for (const n of spec.new_items || []) addItem(r, n);
    for (const n of spec.new_enemies || []) addEnemy(r, n);
    for (const n of spec.new_areas || []) addArea(r, n.like, n, scene);
    for (const n of spec.new_cameras || []) addCamera(r, n.like, n);
    for (const [i, d] of Object.entries(spec.cameras || {})) { const c = r.cameras.find((x) => x.i === +i); if (c) Object.assign(c, d); }
    for (const [i, d] of Object.entries(spec.fog || {})) { const c = r.cameras.find((x) => x.i === +i); if (c && c.fog) for (const [k, v] of Object.entries(d.ints || {})) c.fog[+k] = v; }
    for (const [i, d] of Object.entries(spec.areas || {})) {
      const a = r.areas.find((x) => x.i === +i);
      if (!a) continue;
      if (d.quad) a.quad = d.quad;
      if (d.height) a.h = d.height;
      for (const [k, v] of Object.entries(d.fields || {})) a.fields[+k - 12] = v;
      if (a.tag === 0 || a.tag === 1) a.to = a.fields[1];
      if (a.tag === 0) a.door = a.fields[0];
    }
  }
}

// ---- the store: one object the whole page shares
const listeners = {};
export const on = (event, fn) => { (listeners[event] = listeners[event] || []).push(fn); return fn; };
export const emit = (event, ...args) => (listeners[event] || []).forEach((fn) => fn(...args));

export const store = {
  game: "", scenario: 0, scenarios: [], ops: {},
  scene: null, orig: null,
  startDefault: null, startRoom: null,   // the room the characters begin in: the scenario's own, and the one you chose (null: not changed)
  winding: "ccw",      // the faces the game draws (measured against its own pictures of the bar: with "ccw" culled backs, camera 0 sees down the hallway as the game does, not into the shell around it)
  survey: { have: new Set(), supported: false },
  real: { on: false, key: "", room: null, urls: {}, last: null, busy: false },
  textures: {},               // the room's pictures you replaced: the picture's number -> a data: URL of your image
  models: {},                 // room file -> the model's JSON, once loaded
  tool: "select",
  room: null,                 // the room being worked in (its file)
  selection: [],              // [{c: collection, file, i}]
  mode: "3d",                 // 3d (build: the free camera), game (through the room's own cameras) or plan
  snap: 10,
  show: { models: true, textures: true, areas: true, triggers: false, cameras: false, lights: false, zones: false, nodes: false, items: true, enemies: true, labels: true, names: false, grid: true },
  history: [],                // the edits after each step, the first being "nothing"
  at: 0,
  dirty: false,
  busy: "",
  problems: [],
};

export const roomOf = (file) => store.scene.rooms.find((r) => r.file === file);
export const objectOf = (ref) => { const r = roomOf(ref.file); return r ? r[ref.c].find((o) => o.i === ref.i) : null; };
export const refEq = (a, b) => a && b && a.c === b.c && a.file === b.file && a.i === b.i;

export function loadScene(data, saved) {
  store.game = data.game; store.scenario = data.scenario; store.scenarios = data.scenarios; store.ops = data.ops;
  data.rooms.forEach((r) => { r.floors = r.floors || []; });
  store.orig = clone({ rooms: data.rooms });
  store.scene = clone({ rooms: data.rooms });
  store.history = [{ rooms: {} }];
  store.at = 0;
  store.selection = [];
  store.room = store.scene.rooms[0] ? store.scene.rooms[0].file : null;
  store.startDefault = data.start_room ?? null;
  store.startRoom = saved && saved.edits && saved.edits.start_room !== undefined ? saved.edits.start_room : null;
  store.textures = clone((saved && saved.edits && saved.edits.textures) || {});
  if (saved && saved.edits && (Object.keys(saved.edits.rooms || {}).length || Object.keys(store.textures).length || store.startRoom !== null)) {
    applyEdits(store.scene, saved.edits);
    store.history = [{ rooms: {} }, clone(saved.edits)];
    store.at = 1;
  }
  store.dirty = false;
}

export const currentEdits = () => diffs(store.scene, store.orig);

// Called after a change is complete (the end of a drag, a field edited): records a step when the scene differs from the last one.
export function commit(label) {
  const edits = currentEdits();
  if (same(edits, store.history[store.at])) return false;
  store.history = store.history.slice(0, store.at + 1);
  store.history.push({ ...clone(edits), label });
  store.at = store.history.length - 1;
  store.dirty = true;
  emit("changed", { label });
  return true;
}

function restore(edits) {
  store.scene = clone(store.orig);
  store.textures = clone(edits.textures || {});
  store.startRoom = edits.start_room !== undefined ? edits.start_room : null;
  applyEdits(store.scene, edits);
  store.selection = store.selection.filter((s) => objectOf(s));
  store.dirty = true;
  emit("changed", { restored: true });
}

// back (or forward) to any step of the history at once
export function jumpTo(i) { if (i >= 0 && i < store.history.length && i !== store.at) { store.at = i; restore(store.history[i]); } }

export function undo() { if (store.at > 0) { store.at--; restore(store.history[store.at]); } }
export function redo() { if (store.at < store.history.length - 1) { store.at++; restore(store.history[store.at]); } }
export const canUndo = () => store.at > 0;
export const canRedo = () => store.at < store.history.length - 1;

export function select(refs, additive = false) {
  store.selection = additive ? [...store.selection, ...refs.filter((r) => !store.selection.some((s) => refEq(s, r)))] : refs;
  if (refs[0] && refs[0].file !== store.room) store.room = refs[0].file;
  emit("selection");
}
export function setRoom(file) { store.room = file; emit("room"); }
export function setTool(tool) { store.tool = tool; emit("tool"); }
export function setMode(mode) { store.mode = mode; emit("mode"); }

// a flat floor over a rectangle of the room's ground at a height (up): the old ground is cut out where it lies
export function addFloor(room, a, b, y) {
  const next = Math.max(-1, ...room.floors.map((f) => f.i)) + 1;
  const f = { i: next, min: [Math.min(a[0], b[0]), Math.min(a[1], b[1])], max: [Math.max(a[0], b[0]), Math.max(a[1], b[1])], y, isNew: true };
  room.floors.push(f);
  return f;
}

// a new wall between two points on the ground, solid on the side its normal points to (the segment's left by default)
export function addWall(room, a, b, y) {
  const dx = b[0] - a[0], dz = b[1] - a[1], len = Math.hypot(dx, dz) || 1;
  const like = room.walls[0];
  // the open side faces the middle of the room (where its doors and things are), so a new wall keeps people in
  const pts = room.areas.filter((x) => !x.isNew).map((x) => centre("areas", x)).filter((p) => Math.abs(p[0]) < 1e6);
  const cx = pts.length ? pts.reduce((t, p) => t + p[0], 0) / pts.length : a[0], cz = pts.length ? pts.reduce((t, p) => t + p[2], 0) / pts.length : a[1];
  let toward = [-dz / len, dx / len];
  if (toward[0] * (cx - (a[0] + b[0]) / 2) + toward[1] * (cz - (a[1] + b[1]) / 2) < 0) toward = [-toward[0], -toward[1]];
  const next = Math.max(-1, ...room.walls.map((w) => w.i)) + 1;
  const w = { i: next, a: [a[0], a[1]], b: [b[0], b[1]], y: like ? clone(like.y) : [y - 100, y + 800], flags: like ? like.flags : 0x2F0000, n: toward, isNew: true };
  room.walls.push(w);
  return w;
}

export const startRoomId = () => (store.startRoom !== null ? store.startRoom : store.startDefault);
