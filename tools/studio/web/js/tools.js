// The tools on the rail: what each puts into the scene when you click, and where a click lands on the ground.
import { store, roomOf, addArea, addCamera, commit, select, AREA_TAGS, clone, emit } from "./scene.js";

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

export const FIXED = [
  { id: "item", label: "Item", icon: "item", key: "I", soon: "Adding items needs the item file to grow: next stage." },
  { id: "enemy", label: "Enemy", icon: "enemy", key: "N", soon: "Adding enemies needs the enemy file to grow: next stage." },
  { id: "spawn", label: "Spawn point", icon: "spawn", key: "S", soon: "Spawn points: next stage." },
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
  const src = roomOf(t.room).areas.find((a) => a.i === t.i);
  const half = 50;
  const a = addArea(room, t.i, {
    quad: [[x + half, z - half], [x - half, z - half], [x - half, z + half], [x + half, z + half]],
    height: [y + (src.h[0] - 0), y + (src.h[1] - 0) + 0],
    template: t.same ? undefined : { room: t.room, i: t.i },
  });
  commit("Add " + (AREA_TAGS[tool.tag]?.label || "area").toLowerCase());
  select([{ c: "areas", file: room.file, i: a.i }]);
  emit("note", `${AREA_TAGS[tool.tag]?.label || "Area"} added`);
  return a;
}
