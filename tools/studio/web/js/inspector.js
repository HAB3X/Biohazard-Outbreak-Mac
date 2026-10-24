// The inspector: forms for what is selected, in words and sliders rather than field numbers.
import { store, on, emit, objectOf, roomOf, commit, startRoomId, describe, centre, translate, select, clone, AREA_TAGS } from "./scene.js";
import { icon } from "./icons.js";
import { section_ as rulesSection } from "./rules.js";
import { isDoor, duplicate, removeCopy } from "./scenery.js";
import { el, number, vec3, select as selectBox, chips, dial, colour, field, section, range, hex, unhex, toast } from "./ui.js";

const host = () => document.getElementById("inspector");
let quiet = false;       // set while this panel is the one that changed the scene (it is not redrawn under the pointer)

const roomName = (r) => r.name || `Room ${r.id}`;
const roomOptions = () => store.scene.rooms.map((r) => ({ value: r.id, label: roomName(r) }));
const bitsToF = (i) => { const b = new DataView(new ArrayBuffer(4)); b.setInt32(0, i | 0); return b.getFloat32(0); };
const fToBits = (f) => { const b = new DataView(new ArrayBuffer(4)); b.setFloat32(0, f); return b.getInt32(0); };

// items: named by what the game's own tables say about their number
export function itemKind(kind) {
  if (kind === 0) return "Nothing / random";
  if (kind >= 0xC8 && kind <= 0xD2) return "Ammunition box";
  if (kind >= 0xFA && kind <= 0x106) return "Ammunition";
  if (kind >= 0x12C && kind <= 0x13D) return "Healing item";
  if (kind >= 0x190 && kind <= 0x1FF) return "Weapon";
  if (kind >= 0x64 && kind <= 0x6F) return "Weapon / tool";
  if (kind >= 0x135 && kind < 0x150) return "File / document";
  return "Item";
}

// hit points: the tiers the game reads (measured: docs/research/rdt-format.md)
const HP = { 1: [600, 650, 700, 750, 780, 800, 820, 850], 32: [5800, 5800, 5800, 5800, 7200, 7200, 7200, 7200] };
export const hpText = (id, tier) => (tier >= 8 ? "invulnerable" : HP[id] ? `${HP[id][tier]} HP` : `tier ${tier}`);

const MODES = [{ value: 0, label: "Mode 1", tip: "the first of the four play modes (the first of File 2's single play)" }, { value: 1, label: "Mode 2", tip: "File 1's normal difficulty" }, { value: 2, label: "Mode 3" }, { value: 3, label: "Mode 4" }];

function header(ref, o) {
  const d = describe(ref.c, o), r = roomOf(ref.file);
  const color = getComputedStyle(document.documentElement).getPropertyValue("--" + d.color).trim();
  const acts = el("div", { class: "acts" });
  const mk = (ic, tip, fn, disabled) => { const b = el("button", { class: "btn icon", title: tip, disabled: disabled || false, html: icon(ic, 16) }); b.addEventListener("click", fn); return b; };
  acts.append(mk("target", "Show it in the view", () => emit("focus", ref)));
  if (ref.c === "scenery") acts.append(mk("copy", "Make a copy beside it", () => { const c = duplicate(r, o); commit("Copy object"); select([{ c: "scenery", file: ref.file, i: c.i }]); }));
  if (o.isNew || ref.c === "walls" || ref.c === "floors" || ref.c === "enemies") acts.append(mk("trash", ref.c === "enemies" && !o.isNew ? "Remove from the game" : "Delete", () => remove(ref)));
  return el("div", { class: "head" }, el("span", { class: "dot", style: `background:${color}` }), el("div", {}, el("h2", {}, d.label), el("small", {}, `${roomName(r)}${o.isNew ? " · new" : ""}`)), acts);
}
function remove(ref) {
  const r = roomOf(ref.file), list = r[ref.c];
  const k = list.findIndex((x) => x.i === ref.i);
  if (k >= 0 && ref.c === "scenery" && list[k].isNew) { removeCopy(r, list[k]); select([]); commit("Delete object"); return; }
  if (k >= 0 && ref.c === "enemies" && !list[k].isNew) { list[k].modes = 0; select([]); commit("Remove enemy"); return; }   // never appears in any play mode
  if (k < 0 || (!list[k].isNew && ref.c !== "walls" && ref.c !== "floors")) return;
  list.splice(k, 1);
  select([]);
  commit("Delete");
}

export function render() {
  const h = host();
  h.innerHTML = "";
  const sel = store.selection;
  if (sel.length === 0) return h.append(roomPanel());
  if (sel.length > 1) {
    h.append(el("div", { class: "head" }, el("div", {}, el("h2", {}, `${sel.length} things selected`), el("small", {}, "Drag any of them to move them together"))));
    h.append(section("", el("div", { class: "note" }, "Click one thing to edit it.")));
    return;
  }
  const ref = sel[0], o = objectOf(ref);
  if (!o) return h.append(roomPanel());
  h.append(header(ref, o));
  const f = FORMS[ref.c];
  if (f) f(ref, o).filter(Boolean).forEach((s) => h.append(s));
}

function roomPanel() {
  const r = roomOf(store.room);
  const box = el("div", {});
  if (!r) return box;
  box.append(el("div", { class: "head" }, el("div", {}, el("h2", {}, roomName(r)), el("small", {}, `Room ${r.id}`))));
  const n = (k) => r[k].length;
  box.append(section("In this room", el("div", { class: "kv" },
    el("span", {}, "Doors and exits"), el("b", {}, r.areas.filter((a) => a.tag === 0 || a.tag === 1).length),
    el("span", {}, "Items"), el("b", {}, n("items")),
    el("span", {}, "Enemies"), el("b", {}, n("enemies")),
    el("span", {}, "Cameras"), el("b", {}, n("cameras")))));
  if (store.startDefault !== null) {
    const here = startRoomId() === r.id;
    box.append(section("Start", here ? el("div", { class: "note" }, "The game begins in this room.")
      : el("button", { class: "ghost", onclick: () => { store.startRoom = r.id; commit("Choose the start room"); render(); } }, "Begin the game here")));
  }
  box.append(rulesSection(r, render));
  if ((r.pictures || []).length) box.append(section("Pictures in this room", pictures(r, shownPicture, (m) => { shownPicture = m; render(); })));
  box.append(el("div", { class: "empty", style: "padding:28px 22px;text-align:left" },
    el("p", {}, "Click something in the picture to edit it."),
    el("p", {}, "Use ", el("b", { style: "color:var(--dim)" }, "Add"), " at the bottom to put new things down."),
    el("p", {}, "Press ", el("kbd", {}, "⌘Z"), " if you change your mind.")));
  return box;
}

// a field that changes the object and settles into a history step
function set(ref, label, apply) {
  return {
    live: (v) => { const o = objectOf(ref); if (o) { apply(o, v); emit("live"); } },
    done: () => { quiet = true; commit(label); quiet = false; },
  };
}

function positionSection(ref, o, label = "Position") {
  const c = centre(ref.c, o);
  const { live, done } = set(ref, "Move", (obj, v) => { const cur = centre(ref.c, obj); translate(ref.c, obj, v[0] - cur[0], v[1] - cur[1], v[2] - cur[2]); });
  return section(label, field("Centre", vec3(c, live, done, { step: store.snap > 1 ? store.snap : 1 })));
}

let shownPicture = 0;
// the room's pictures as small squares to pick from
function pictures(room, current, pick) {
  const box = el("div", { class: "pictures" });
  const seen = new Set(), now = (room.pictures || [])[current];
  (room.pictures || []).forEach((tex, m) => {
    if (seen.has(tex)) return;           // a picture used by several materials is shown once
    seen.add(tex);
    const b = el("button", { type: "button", class: tex === now ? "on" : "", title: `Picture ${tex}` }, el("img", { src: store.textures[tex] || "/api/tex?index=" + tex, loading: "lazy" }));
    b.addEventListener("click", () => pick(m));
    box.append(b);
  });
  const tex = (room.pictures || [])[current];
  const own = el("button", { type: "button", class: "ghost" }, "Paint it with my own image…");
  own.addEventListener("click", () => {
    const file = el("input", { type: "file", accept: "image/*" });
    file.addEventListener("change", () => {
      if (!file.files[0]) return;
      const img = new Image();
      img.onload = () => {
        const c = document.createElement("canvas"), k = Math.min(1, 256 / Math.max(img.width, img.height));
        c.width = Math.max(1, Math.round(img.width * k)); c.height = Math.max(1, Math.round(img.height * k));
        c.getContext("2d").drawImage(img, 0, 0, c.width, c.height);
        store.textures[tex] = c.toDataURL("image/png");
        commit("Paint picture");
        render();
      };
      img.src = URL.createObjectURL(file.files[0]);
    });
    file.click();
  });
  const wrap = el("div", {}, box, el("div", { class: "note", style: "margin:8px 0 6px" }, "A picture is shared by everything in the scenario that uses it."), own);
  if (store.textures[tex]) {
    const back = el("button", { type: "button", class: "ghost", style: "margin-left:8px" }, "Put the original back");
    back.addEventListener("click", () => { delete store.textures[tex]; commit("Restore picture"); render(); });
    wrap.append(back);
  }
  return wrap;
}

const FORMS = {
  scenery(ref, o) {
    const orig = o.geometry ? { scale: [1, 1, 1] } : store.orig.rooms.find((r) => r.file === ref.file).scenery.find((x) => x.i === (o.isNew ? o.like : o.i));
    const factor = o.scale[0] / (orig.scale[0] || 1);
    const hid = o.scale[0] < 0.001;
    const deg = (r) => Math.round(r * 180 / Math.PI * 10) / 10;
    const turn = set(ref, "Turn object", (obj, v) => { obj.rot = v.map((d) => d * Math.PI / 180); });
    const size = set(ref, "Resize object", (obj, v) => { obj.scale = orig.scale.map((s) => s * v / 100); });
    const stretch = set(ref, "Stretch object", (obj, v) => { obj.scale = orig.scale.map((s, k) => s * v[k] / 100); });
    const show = set(ref, "Show or hide object", (obj, v) => { obj.scale = v ? clone(orig.scale) : [0.0001, 0.0001, 0.0001]; });
    const pct = hid ? [100, 100, 100] : o.scale.map((s, k) => Math.round(s / (orig.scale[k] || 1) * 100));
    const room = roomOf(ref.file);
    const picture = o.geometry ? section("Picture", pictures(room, o.geometry.material, (m) => { o.geometry.material = m; commit("Change picture"); render(); })) : null;
    return [
      positionSection(ref, o, "Where it stands"),
      picture,
      section("Turn", field("Degrees", vec3(o.rot.map(deg), turn.live, turn.done, { step: 5 }))),
      section("Size",
        field("All together", range(Math.round((hid ? 1 : factor) * 100), 10, 300, 5, size.live, size.done)),
        field("Each way", vec3(pct, stretch.live, stretch.done, { step: 5, min: 1 }), "Percent of its own size: stretch a block into a wall or a floor"),
        isDoor(roomOf(ref.file), o) ? el("div", { class: "note" }, "This is part of a door, so it can't be hidden.")
          : field("Shown", chips([{ value: 1, label: "Shown" }, { value: 0, label: "Hidden" }], (v) => (v === 1) !== hid, (v) => { show.live(v); show.done(); render(); }))),
      section("", el("div", { class: "note" }, "This is one of the room's own objects, and the room has a fixed number of them: hide the ones you don't want and use them as building blocks. Changes are written into the room's model.")),
    ];
  },
  areas(ref, o) {
    const out = [];
    const d = describe("areas", o);
    const fld = (k, label, ctrl, hint) => field(label, ctrl, hint);
    const setField = (k, label) => set(ref, label, (obj, v) => { obj.fields[k] = v; if (k === 1 && (obj.tag === 0 || obj.tag === 1)) obj.to = v; if (k === 0 && obj.tag === 0) obj.door = v; });
    if (o.tag === 0 || o.tag === 1) {
      const to = setField(1, "Change destination");
      const rooms = roomOptions().filter((r) => r.value !== undefined);
      const dest = store.scene.rooms.some((r) => r.id === o.fields[1]);
      const kids = [];
      if (!dest) kids.push(el("div", { class: "bad-box" }, `This leads to room ${o.fields[1]}, which isn't in this scenario.`));
      kids.push(field("Leads to", selectBox(o.fields[1], rooms, (v) => { to.live(v); to.done(); render(); })));
      if (o.tag === 0) {
        const num = setField(0, "Change door number");
        kids.push(field("Door number", number(o.fields[0], num.live, num.done, { step: 1, min: 0 }), "A new door must reuse a number the room already has (it shares that door's model)."));
        const other = setField(2, "Change connection");
        kids.push(field("Opens onto", number(o.fields[2], other.live, other.done, { step: 1, min: 0 }), "Which door of the other room you arrive at."));
        const face = setField(6, "Turn arrival");
        kids.push(field("Arrive facing", dial(o.fields[6] || 0, face.live, face.done)));
        const lock = setField(10, "Change lock text");
        kids.push(field("Lock message", number(o.fields[10], lock.live, lock.done, { step: 1, min: 0 }), "0 = unlocked. Otherwise the stage message shown when it's locked."));
      }
      out.push(section(o.tag === 0 ? "Door" : "Exit", ...kids));
    }
    if (o.tag === 5 || o.tag === 11 || o.tag === 3 || o.tag === 4) {
      const idc = setField(0, "Change id");
      out.push(section(d.label, field("Id", number(o.fields[0], idc.live, idc.done, { step: 1, min: 0 }), "The room's script switches this area on and off and gives it its text by this id."),
        o.tag === 5 ? messageForId(ref, o.fields[0]) : null));
    }
    out.push(positionSection(ref, o, "Place"));
    const { live, done } = set(ref, "Change height", (obj, v) => { obj.h = v; });
    out.push(section("Height", el("div", { class: "vec" }, number(o.h[0], (v) => live([v, o.h[1]]), (v) => { o.h = [v, o.h[1]]; done(); }, { axis: "y", step: 5, scrub: false }), number(o.h[1], (v) => live([o.h[0], v]), (v) => { o.h = [o.h[0], v]; done(); }, { axis: "y", step: 5, scrub: false })),
      el("div", { class: "note", style: "margin-top:8px" }, "The area works between these two heights (from, to).")));
    out.push(advancedFields(ref, o));
    return out;
  },

  cameras(ref, o) {
    const out = [];
    const eye = set(ref, "Move camera", (obj, v) => { obj.eye = v; });
    const tgt = set(ref, "Aim camera", (obj, v) => { obj.target = v; });
    out.push(section("View", field("Position", vec3(o.eye, eye.live, eye.done, { step: 10 })), field("Looks at", vec3(o.target, tgt.live, tgt.done, { step: 10 }))));
    const fv = set(ref, "Change lens", (obj, v) => { obj.fov = v; });
    out.push(section("Lens", field("Field of view", number(o.fov[0], (v) => fv.live([v, o.fov[1]]), (v) => { o.fov = [v, o.fov[1]]; fv.done(); }, { step: 1, min: 5, max: 140, unit: "°" })),
      field("Vertical", number(o.fov[1], (v) => fv.live([o.fov[0], v]), (v) => { o.fov = [o.fov[0], v]; fv.done(); }, { step: 1, min: 5, max: 140, unit: "°" }))));
    if (o.fog) out.push(fogSection(ref, o));
    out.push(section("Switch box", el("div", { class: "note" }, "The camera takes over when the player is inside its box (the dotted shape on the plan). Select the camera and drag the box's grey corners.")));
    return out;
  },

  lights(ref, o) {
    const pos = set(ref, "Move light", (obj, v) => { obj.pos = v; });
    const col = set(ref, "Light colour", (obj, v) => { obj.col = v; });
    const col2 = set(ref, "Light colour", (obj, v) => { obj.col2 = v; });
    const range_ = set(ref, "Light range", (obj, v) => { obj.range = v; });
    const att = set(ref, "Light falloff", (obj, v) => { obj.att = v; });
    const toHex = (c) => hex(c[0] * 255, c[1] * 255, c[2] * 255);
    return [
      section("Light", field("Position", vec3(o.pos, pos.live, pos.done, { step: 10 })),
        field("Colour", colour(toHex(o.col), (h) => col.live(unhex(h).map((v) => v / 255)), col.done)),
        field("Second colour", colour(toHex(o.col2), (h) => col2.live(unhex(h).map((v) => v / 255)), col2.done)),
        field("Reach", number(o.range, range_.live, range_.done, { step: 10, min: 0 })),
        field("Falloff", number(o.att, att.live, att.done, { step: 0.001, min: 0 }))),
      section("", el("div", { class: "note" }, "Character lights only light the characters, not the room's surroundings.")),
    ];
  },

  zones(ref, o) {
    const snd = set(ref, "Change floor sound", (obj, v) => { obj.sound = v; });
    return [section("Sound zone", field("Floor sound", number(o.sound, snd.live, snd.done, { step: 1, min: 0 }), "What footsteps sound like inside the box.")), positionSection(ref, o)];
  },

  points(ref, o) {
    const k = set(ref, "Change sound", (obj, v) => { obj.kind = v; });
    const p = set(ref, "Move sound", (obj, v) => { obj.pos = v; });
    return [section("Sound source", field("Kind", number(o.kind, k.live, k.done, { step: 1, min: 0 })), field("Position", vec3(o.pos, p.live, p.done, { step: 10 })))];
  },

  nodes(ref, o) {
    const p = set(ref, "Move node", (obj, v) => { obj.pos = v; });
    return [section("AI node", field("Position", vec3(o.pos, p.live, p.done, { step: 10 })), el("div", { class: "note" }, "The partners' navigation: nodes link by the numbers in their words (not edited here).")) ];
  },

  items(ref, o) {
    const k = set(ref, "Change item", (obj, v) => { obj.kind = v; });
    const q = set(ref, "Change quantity", (obj, v) => { obj.qty = v; });
    const p = set(ref, "Move item", (obj, v) => { obj.pos = v; });
    return [
      section("Item", field("Kind", number(o.kind, k.live, k.done, { step: 1, min: 0 }), `${itemKind(o.kind)} · 0x${o.kind.toString(16).toUpperCase()}`),
        field("Quantity", number(o.qty, q.live, q.done, { step: 1, min: 0, max: 255 }), "Rounds for ammunition, uses for others."),
        field("Position", vec3(o.pos, p.live, p.done, { step: 10 }))),
      section("", el("div", { class: "note" }, "Item numbers: herbs 300–317, ammunition 250–262, weapons 400+, files 309+. The kind and quantity are written to every difficulty's table.")),
    ];
  },

  floors(ref, o) {
    const h = set(ref, "Change floor height", (obj, v) => { obj.y = v; });
    const a = set(ref, "Resize floor", (obj, v) => { obj.min = [v[0], v[2]]; });
    const b = set(ref, "Resize floor", (obj, v) => { obj.max = [v[0], v[2]]; });
    return [
      section("Floor", field("Height", number(o.y, h.live, h.done, { step: 10 }), "The surface people stand on, in the room's own height"),
        field("One corner", vec3([o.min[0], o.y, o.min[1]], a.live, a.done, { step: store.snap > 1 ? store.snap : 1 })),
        field("Opposite corner", vec3([o.max[0], o.y, o.max[1]], b.live, b.done, { step: store.snap > 1 ? store.snap : 1 }))),
      section("", el("div", { class: "note" }, "The old ground is cut out under this rectangle and a flat floor put in. It changes where people can stand, not what is drawn: put a block from Objects under it. Press Delete to remove it.")),
    ];
  },
  walls(ref, o) {
    const a = set(ref, "Move wall end", (obj, v) => { obj.a = [v[0], v[2]]; });
    const b = set(ref, "Move wall end", (obj, v) => { obj.b = [v[0], v[2]]; });
    const y = set(ref, "Change wall height", (obj, v) => { obj.y = v; });
    const flip = el("button", { class: "btn line" }, "Switch the solid side");
    flip.addEventListener("click", () => { o.n = [-o.n[0], -o.n[1]]; commit("Flip wall"); });
    return [section("Wall",
      field("Starts at", vec3([o.a[0], o.y[0], o.a[1]], a.live, a.done, { step: 10 })),
      field("Ends at", vec3([o.b[0], o.y[0], o.b[1]], b.live, b.done, { step: 10 })),
      field("Reaches", el("div", { class: "vec", style: "grid-template-columns:1fr 1fr" }, number(o.y[0], (v) => y.live([v, o.y[1]]), (v) => { o.y = [v, o.y[1]]; y.done(); }, { axis: "y", step: 10, scrub: false }), number(o.y[1], (v) => y.live([o.y[0], v]), (v) => { o.y = [o.y[0], v]; y.done(); }, { axis: "y", step: 10, scrub: false })), "From and to, in height."),
      flip, el("div", { class: "note", style: "margin-top:10px" }, "Characters are stopped from the side the little tick points to. Press Delete to remove the wall."))];
  },

  spawns(ref, o) {
    const p = set(ref, "Move start", (obj, v) => { obj.pos = [v[0], obj.pos[1], v[2]]; });
    const f = set(ref, "Turn start", (obj, v) => { obj.facing = v; });
    return [section(ref.i === 0 ? "Player start" : `Partner ${ref.i} start`,
      field("Position", vec3(o.pos, p.live, p.done, { step: 10 })),
      field("Facing", dial(o.facing, f.live, f.done)),
      el("div", { class: "note" }, "Where the characters stand when the scenario starts in this room. The first is the player; the others are the partners.")) ];
  },

  enemies(ref, o) {
    const p = set(ref, "Move enemy", (obj, v) => { obj.pos = v; });
    const f = set(ref, "Turn enemy", (obj, v) => { obj.facing = v; });
    const hp = set(ref, "Change health", (obj, v) => { obj.hp = v; });
    const id = set(ref, "Change enemy kind", (obj, v) => { obj.id = v; });
    const cl = set(ref, "Change enemy class", (obj, v) => { obj.cls = v; });
    const md = set(ref, "Change modes", (obj, v) => { obj.modes = v; });
    const hpLabel = el("span", { class: "note" }, hpText(o.id, o.hp));
    const slider = range(Math.min(o.hp, 8), 0, 8, 1, (v) => { hp.live(v); hpLabel.textContent = hpText(o.id, v); }, hp.done);
    return [
      section("Enemy", field("Kind (id)", number(o.id, id.live, id.done, { step: 1, min: 0, max: 255 }), "1 is the common zombie in both games; 32 the first scenario's big one."),
        field("Class", number(o.cls, cl.live, cl.done, { step: 1, min: 0, max: 255 }), "Picks its model and animations."),
        field("Facing", dial(o.facing, f.live, f.done)),
        field("Position", vec3(o.pos, p.live, p.done, { step: 10 }))),
      section("Health", field("Tier", slider), el("div", { style: "margin:-2px 0 4px 106px" }, hpLabel),
        el("div", { class: "note" }, "Health is a step in the kind's table; 8 makes it unkillable.")),
      section("Appears in", chips(MODES, (m) => (o.modes >> m) & 1, (m) => { md.live(o.modes ^ (1 << m)); md.done(); }),
        el("div", { class: "note", style: "margin-top:8px" }, "Each play mode has its own set; untick a mode to leave the enemy out of it.")),
    ];
  },
};

function advancedFields(ref, o) {
  const box = el("div", { style: "display:none" });
  const grid = el("div", {});
  o.fields.forEach((v, k) => {
    const s = set(ref, "Change field", (obj, val) => { obj.fields[k] = val; });
    grid.append(field(`Word ${12 + k}`, number(v, s.live, s.done, { step: 1 })));
  });
  box.append(grid);
  const toggle = el("button", { class: "btn ghost", style: "margin:-4px 0 6px -8px" }, "Show the raw fields");
  toggle.addEventListener("click", () => { box.style.display = box.style.display === "none" ? "block" : "none"; toggle.textContent = box.style.display === "none" ? "Show the raw fields" : "Hide the raw fields"; });
  return section("Advanced", toggle, box);
}

// the text an examine spot shows: the room's script gives it, by this id
function messageForId(ref, id) {
  const r = roomOf(ref.file);
  const ins = (r.ins || []).filter((i) => i.op === 0x55 && i.args[0] === id);
  if (!ins.length) return el("div", { class: "note" }, "No script line sets a message for this id.");
  const box = el("div", {});
  for (const i of ins) {
    const text = (store.messages || [])[i.args[1] - 1] || "";
    const w = i.at + 2;
    const s = set(ref, "Change message", (obj, v) => { r.script[w] = v; });
    box.append(field("Message #", number(r.script[w], (v) => { s.live(v); t.textContent = (store.messages || [])[v - 1] || "(no such message)"; }, s.done, { step: 1, min: 1 })));
    var t = el("div", { class: "note", style: "margin:-4px 0 8px 106px;color:var(--text)" }, text || "(no text)");
    box.append(t);
  }
  return box;
}

function fogSection(ref, o) {
  const fog = o.fog;
  const argbHex = (v) => hex((v >> 16) & 255, (v >> 8) & 255, v & 255);
  const toArgb = (h) => { const [r, g, b] = unhex(h); return (0xFF000000 | (r << 16) | (g << 8) | b) | 0; };
  const sc = set(ref, "Change room colour", (obj, v) => { obj.fog[4] = v; });
  const am = set(ref, "Change ambient colour", (obj, v) => { obj.fog[3] = v; });
  const near = set(ref, "Change fog start", (obj, v) => { obj.fog[1] = fToBits(v); });
  const far = set(ref, "Change fog end", (obj, v) => { obj.fog[2] = fToBits(v); });
  return section("Atmosphere",
    field("Room colour", colour(argbHex(fog[4]), (h) => sc.live(toArgb(h)), sc.done)),
    field("Ambient", colour(argbHex(fog[3]), (h) => am.live(toArgb(h)), am.done)),
    field("Fog from", number(bitsToF(fog[1]), near.live, near.done, { step: 100 })),
    field("Fog to", number(bitsToF(fog[2]), far.live, far.done, { step: 100 })));
}

on("selection", render);
on("room", () => { if (!store.selection.length) render(); });
on("changed", () => { if (!quiet) render(); });
