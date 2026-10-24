// The tools on the rail: what each puts into the scene when you click, and where a click lands on the ground.
import { addLibrary, setPose } from "./scenery.js";
import { store, roomOf, addArea, addCamera, addItem, addEnemy, commit, select, AREA_TAGS, clone, emit } from "./scene.js";

// A template for an area of each tag: the first one in the room being worked in, else in any room of the scenario (a new area is a copy of a record of the game's own).
export function templateFor(tag) {
  const here = roomOf(store.room);
  const inRoom = here && here.areas.find((a) => a.tag === tag && !a.isNew);
  if (inRoom) return { room: here.file, i: inRoom.i, same: true };
  for (const r of store.scene.rooms) {
    const a = r.areas.find((x) => x.tag === tag && !x.isNew);
    if (a) return { room: r.file, i: a.i, same: r.file === store.room };
  }
  return null;
}

export const PLACE = [
  { id: "door", label: "Door", icon: "door", key: "D", tag: 0, hint: "Click on the floor to put a door. It leads to the room you pick in the panel." },
  { id: "exit", label: "Exit", icon: "exit", key: "E", tag: 1, hint: "Click to put an exit: stepping in it takes you to another room." },
  { id: "examine", label: "Examine spot", icon: "message", key: "M", tag: 5, hint: "Click to put a spot the player can look at (it shows a message)." },
  { id: "pickup", label: "Pickup trigger", icon: "item", key: "P", tag: 3, hint: "Click to put an item pickup trigger." },
  { id: "ladder", label: "Ladder", icon: "ladder", key: "L", tag: 6, hint: "Click to put a ladder (three stacked areas are made)." },
  { id: "zone", label: "Script zone", icon: "zone", key: "Z", tag: 12, hint: "Click to put a zone a script can wait on." },
  { id: "camera", label: "Camera", icon: "camera", key: "C", hint: "Click to put a camera. It follows the player or stays put, as the one it copies does." },
];

export const FIXED = [];
export const WALL = { id: "wall", label: "Wall", hint: "Click where it starts, then where it ends; keep clicking to chain walls. The little tick shows the open side." };
export const FLOOR = { id: "floor", label: "Floor", hint: "Click one corner, then the opposite corner: a flat floor is laid there, which you can raise in the panel. Walk-on surfaces only: the picture is not changed." };
export const THINGS = [
  { id: "item", label: "Item", icon: "item", key: "I", hint: "Click to put an item on the floor. Pick what it is and how many in the panel." },
  { id: "enemy", label: "Enemy", icon: "enemy", key: "N", hint: "Click to put an enemy. It faces the way you set in the panel; pick its health there too." },
  { id: "library", label: "Library object", icon: "cube", key: "", hint: "Click to put the object you picked from the Library. Click again for another; pick a different one in the Library." },
  { id: "spawn", label: "Player start", icon: "spawn", key: "S", hint: "Click to move the player's start spot in this room (the partners follow the other start spots)." },
];

export const snapTo = (v, step = store.snap) => (step > 1 ? Math.round(v / step) * step : Math.round(v));

// the ground under a screen point: the room's floor where the model has one
export function ground(camera, sx, sy, renderer) {
  const y0 = (renderer.models[store.room] && renderer.models[store.room].bounds[1] > -1e8) ? 0 : 0;
  let p = camera.onPlane(sx, sy, y0);
  if (!p) return null;
  const model = renderer.models[store.room];
  let y = y0;
  if (model) {
    // look for the floor nearest above the plane, twice (the first guess is where the ray meets height 0)
    for (let n = 0; n < 2; n++) {
      const h = model.floors.heightAt(p[0], p[1], y + 6000);
      if (h === null) break;
      y = h;
      const q = camera.onPlane(sx, sy, y);
      if (q) p = q;
    }
  }
  return [p[0], y, p[1]];
}

export function place(tool, at) {
  const room = roomOf(store.room);
  if (!room) return null;
  const x = snapTo(at[0]), z = snapTo(at[2]), y = Math.round(at[1]);
  if (tool.id === "spawn") {
    const sp = room.spawns[0];
    if (!sp) { emit("note", "This room has no start spots to move.", "warn"); return null; }
    sp.pos = [x, y, z];
    commit("Move player start");
    select([{ c: "spawns", file: room.file, i: sp.i }]);
    return sp;
  }
  if (tool.id === "library") {
    if (!store.libraryPick) return null;
    const c = addLibrary(room, store.renderer, store.libraryPick, [x, y, z]);
    if (!c) { emit("note", "That object isn't loaded yet.", "warn"); return null; }
    if (store.libraryRot) setPose(c, { rot: [0, store.libraryRot, 0] });
    commit("Add " + (store.libraryPick.name || "object"));
    select([{ c: "scenery", file: room.file, i: c.i }]);
    return c;
  }
  if (tool.id === "item") {
    const o = addItem(room, { position: [x, y, z] });
    commit("Add item");
    select([{ c: "items", file: room.file, i: o.i }]);
    return o;
  }
  if (tool.id === "enemy") {
    const o = addEnemy(room, { position: [x, y, z], variant: 0 });
    commit("Add enemy");
    select([{ c: "enemies", file: room.file, i: o.i }]);
    return o;
  }
  if (tool.id === "camera") {
    const like = room.cameras.find((c) => !c.isNew) || room.cameras[0];
    if (!like) return null;
    const eye = [x, y + 220, z];
    const dx = like.target[0] - like.eye[0], dz = like.target[2] - like.eye[2], len = Math.hypot(dx, dz) || 1;
    const c = addCamera(room, like.i, { eye, target: [x + dx / len * 1500, y, z + dz / len * 1500], fov: like.fov, box: like.box.map((p) => [p[0] - like.eye[0] + x, p[1] - like.eye[1] + eye[1], p[2] - like.eye[2] + z]) });
    commit("Add camera");
    select([{ c: "cameras", file: room.file, i: c.i }]);
    return c;
  }
  const t = templateFor(tool.tag);
  if (!t) return null;
  // a script zone is found by its number with the first field 0 (measured in both games); each new one in the room gets a number of its own
  const zoneFields = tool.tag === 12 ? { 12: 0, 13: 1 + Math.max(0, ...room.areas.filter((a) => a.tag === 12 && a.fields[0] === 0).map((a) => a.fields[1])) } : undefined;
  const src = roomOf(t.room).areas.find((a) => a.i === t.i);
  const half = 50;
  const a = addArea(room, t.i, {
    quad: [[x + half, z - half], [x - half, z - half], [x - half, z + half], [x + half, z + half]],
    height: [y + (src.h[0] - 0), y + (src.h[1] - 0) + 0],
    template: t.same ? undefined : { room: t.room, i: t.i },
    fields: zoneFields,
  });
  commit("Add " + (AREA_TAGS[tool.tag]?.label || "area").toLowerCase());
  select([{ c: "areas", file: room.file, i: a.i }]);
  emit("note", `${AREA_TAGS[tool.tag]?.label || "Area"} added`);
  return a;
}
