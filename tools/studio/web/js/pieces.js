// Ready-made pieces: a level started from nothing, a door that opens on a spot, a group of enemies. Each is made of the same parts the Add menu and the room's rules make by hand.
import { store, commit, select, setTool, addEnemy, addFloor, addItem, objectOf } from "./scene.js";
import { emptyRoom } from "./scenery.js";
import { placeArea } from "./tools.js";
import { toast } from "./ui.js";

// the game's door the user has picked, or null
function pickedDoor() {
  const s = store.selection[0], o = s && s.c === "areas" ? objectOf(s) : null;
  return o && o.tag === 0 ? o : null;
}

// the lowest flag no rule of the room uses yet (the room's rules use flags 1 to 32)
function freeFlag(room) {
  const used = new Set((room.rules || []).flatMap((r) => [r.flag, r.when && r.when.flag]).filter(Boolean));
  for (let n = 1; n <= 32; n++) if (!used.has(n)) return n;
  throw new Error("All 32 flags of this room are in use");
}

// the middle of the room's doors and exits (some rooms have no player start record), at the height they stand at
function roomMiddle(room) {
  const ways = room.areas.filter((a) => a.tag === 0 || a.tag === 1);
  if (!ways.length) return null;
  const mid = (a) => [a.quad.reduce((s, p) => s + p[0], 0) / a.quad.length, a.quad.reduce((s, p) => s + p[1], 0) / a.quad.length];
  return [ways.reduce((s, a) => s + mid(a)[0], 0) / ways.length, Math.min(...ways.map((a) => a.h[0])), ways.reduce((s, a) => s + mid(a)[1], 0) / ways.length];
}

// takes everything out of the room and lays a floor round where the player starts, so building can begin at once
export function startLevelHere(room) {
  const c = room.spawns[0] ? room.spawns[0].pos : roomMiddle(room);
  if (!c) { toast("Nowhere to build around", "err", "This room has no player start and no doors to take the middle of."); return; }
  emptyRoom(room);
  addFloor(room, [c[0] - 1500, c[2] - 1500], [c[0] + 1500, c[2] + 1500], c[1] + 100);
  commit("Start a level here");
  toast("An empty room with a floor", "ok", "Add walls, props from the Library, enemies and items. The doors still lead where they did.");
}

// a door locked until someone steps on a spot: a script zone there sets a flag (and it stays set, measured), the door is locked while the flag is off and unlocked once it is on
export function beginDoorOnSpot() {
  const door = pickedDoor();
  if (!door) { toast("Pick a door first", "err", "Click the door you want locked, then choose this again."); return; }
  store.recipe = {
    label: "Click the spot that opens the door",
    run(room, at) {
      const zone = placeArea(room, 12, at[0], at[1], at[2], 120);
      if (!zone) { toast("This scenario has no script zone to copy", "err"); return null; }
      const flag = freeFlag(room);
      room.rules = room.rules || [];
      room.rules.push({ when: { zone: zone.fields[1] }, act: "flag", flag, value: 1 });
      room.rules.push({ when: { flag, on: false }, act: "lock", id: door.fields[0], message: 201 }, { when: { flag, on: true }, act: "unlock", id: door.fields[0] });
      commit("Door that opens on a spot");
      select([{ c: "areas", file: room.file, i: zone.i }]);
      toast("Door and spot linked", "ok", `Door ${door.fields[0]} stays locked until someone stands on the spot. Both rules are under This room.`);
      return zone;
    },
  };
  setTool("recipe");
}

// four enemies in a ring, each looking in at the middle
export function beginEnemyGroup() {
  store.recipe = {
    label: "Click where the group stands",
    run(room, at) {
      const n = 4, radius = 220;
      for (let k = 0; k < n; k++) {
        const a = (k / n) * Math.PI * 2;
        addEnemy(room, { position: [at[0] + Math.sin(a) * radius, at[1], at[2] + Math.cos(a) * radius], facing: Math.round(((a + Math.PI) / (Math.PI * 2)) * 65536) % 65536 });
      }
      commit("Group of enemies");
      return true;
    },
  };
  setTool("recipe");
}

// a door locked until an item is taken: the item and its pickup trigger stand where you click; taking the item sets the game's flag of the trigger's number (measured), and the door is
// locked every frame and unlocked as soon as that flag is on (the unlock runs after the lock in the same frame)
export function beginKeyDoor() {
  const door = pickedDoor();
  if (!door) { toast("Pick a door first", "err", "Click the door you want locked, then choose this again."); return; }
  store.recipe = {
    label: "Click where the item lies",
    run(room, at) {
      const numbers = store.scene.rooms.flatMap((r) => r.areas.filter((a) => a.tag === 3).map((a) => a.fields[0]));
      const n = Math.max(0, ...numbers) + 1;
      const trigger = placeArea(room, 3, at[0], at[1], at[2], 80, { 12: n });
      if (!trigger) { toast("This scenario has no pickup trigger to copy", "err"); return null; }
      const item = addItem(room, { position: [at[0], at[1] + 25, at[2]] });
      room.rules = room.rules || [];
      room.rules.push({ when: null, act: "lock", id: door.fields[0], message: 201 }, { when: { item: n }, act: "unlock", id: door.fields[0] });
      commit("Door that opens when an item is taken");
      select([{ c: "items", file: room.file, i: item.i }]);
      toast("Item and door linked", "ok", `Door ${door.fields[0]} stays locked until the item is taken. Change what the item is in its panel.`);
      return item;
    },
  };
  setTool("recipe");
}
