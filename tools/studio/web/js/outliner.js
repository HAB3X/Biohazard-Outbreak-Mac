// The side panel: the scenario's rooms, and under them what is in the room you are in. Click a room to go there, a thing to select it.
import { store, on, emit, select, setRoom, roomOf, describe, refEq, AREA_TAGS, startRoomId } from "./scene.js";
import { el } from "./ui.js";

const host = () => document.getElementById("side");
let query = "";
const css = (name) => getComputedStyle(document.documentElement).getPropertyValue("--" + name).trim();
const roomName = (r) => r.name || `Room ${r.id}`;

// friendly groups: doors and exits together, the other areas as "Triggers"
const GROUPS = [
  { title: "Start spots", c: "spawns", pick: () => true },
  { title: "Doors and exits", c: "areas", pick: (o) => o.tag === 0 || o.tag === 1 },
  { title: "Items", c: "items", pick: () => true },
  { title: "Enemies", c: "enemies", pick: () => true },
  { title: "Cameras", c: "cameras", pick: () => true },
  { title: "Triggers", c: "areas", pick: (o) => o.tag !== 0 && o.tag !== 1 },
];

function label(c, o) {
  if (c === "areas") {
    if (o.tag === 0) return `Door to room ${o.to}`;
    if (o.tag === 1) return `Exit to room ${o.to}`;
    return `${describe(c, o).label}${o.fields[0] ? " " + o.fields[0] : ""}`;
  }
  if (c === "spawns") return o.i === 0 ? "Player" : `Partner ${o.i}`;
  if (c === "enemies") return `Enemy ${o.id}`;
  if (c === "items") return `Item ${o.kind.toString(16).toUpperCase()} × ${o.qty}`;
  return `${describe(c, o).label} ${o.i}`;
}

export function render() {
  const h = host();
  h.innerHTML = "";
  h.append(el("div", { class: "side-title" }, "Rooms"));
  const search = el("div", { class: "search" });
  const input = el("input", { placeholder: "Find a room", value: query });
  input.addEventListener("input", () => { query = input.value.trim().toLowerCase(); renderRooms(); });
  search.append(input);
  h.append(search, el("div", { id: "rooms" }), el("div", { class: "things", id: "things" }));
  renderRooms(); renderThings();
}

function renderRooms() {
  const box = document.getElementById("rooms");
  if (!box) return;
  box.innerHTML = "";
  for (const r of store.scene.rooms) {
    const text = `${r.id} ${roomName(r)}`.toLowerCase();
    if (query && !text.includes(query)) continue;
    const row = el("div", { class: "room" + (r.file === store.room ? " cur" : "") }, el("span", { class: "n" }, String(r.id)), el("span", { class: "t", title: roomName(r) }, roomName(r)), startRoomId() === r.id ? el("span", { class: "start", title: "The game begins here" }, "start") : null);
    row.addEventListener("click", () => { setRoom(r.file); emit("frame", r.file); });
    box.append(row);
  }
}

function renderThings() {
  const box = document.getElementById("things");
  if (!box) return;
  box.innerHTML = "";
  const r = roomOf(store.room);
  if (!r) return;
  box.append(el("h6", {}, `In room ${r.id}`));
  for (const g of GROUPS) {
    const list = r[g.c].filter(g.pick);
    if (!list.length) continue;
    box.append(el("h6", {}, `${g.title} · ${list.length}`));
    for (const o of list.slice(0, 30)) {
      const ref = { c: g.c, file: r.file, i: o.i }, d = describe(g.c, o);
      const leaf = el("div", { class: "leaf" + (store.selection.some((s) => refEq(s, ref)) ? " sel" : "") }, el("i", { style: `background:${css(d.color)}` }), label(g.c, o), o.isNew ? el("em", {}, "new") : null);
      leaf.addEventListener("click", () => { select([ref]); emit("focus", ref); });
      box.append(leaf);
    }
    if (list.length > 30) box.append(el("div", { class: "quiet", style: "padding:2px 8px" }, `and ${list.length - 30} more`));
  }
}

on("selection", () => store.scene && renderThings());
on("room", () => { if (store.scene) { renderRooms(); renderThings(); } });
on("changed", () => { if (store.scene) { renderRooms(); renderThings(); } });
