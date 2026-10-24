// The Library: every object of every room's model in the scenario, as a scrolling grid of pictures. Pick one, then click in the room to put copies down.
import { store, setTool, on } from "./scene.js";
import { el } from "./ui.js";

const SHELL = 3500;      // wider than this is a room's own floor and walls
const KINDS = [["all", "All"], ["recent", "Recent"], ["furniture", "Furniture"], ["boxes", "Boxes"], ["bottles", "Bottles"], ["containers", "Containers"], ["decor", "Decor"], ["other", "Other"]];
// what a name says the object is, for the filter chips
const KIND_WORDS = [["furniture", /chair|armchair|bench|table|stool|shelf|cabinet|post|rail|staircase|sink|basin|urinal|tap|beer/i], ["boxes", /box|crate|carton|stack|cube|slab|poster/i], ["bottles", /bottle|decanter|vase|jar|glass/i], ["containers", /barrel|bin|bucket|basket|drum|tank|sack|tube|roller/i], ["decor", /statue|figure|picture|lamp|magazine|coat|cloth|helmet|paper|mop|rod|pipe|hook|handle|baluster|corrugated|panel|plank|sheet/i]];
const kindOf = (name) => (KIND_WORDS.find(([, re]) => re.test(name)) || ["other"])[0];
let panel = null, grid = null, count = null, viewport = null, names = null, note = null;
let state = { kind: "all", where: "here", text: "", unnamed: false };
let catalogue = [], built = "";

// the last objects picked, newest first (kept in this browser only: a convenience, the page works without it)
const RECENT = "studio-library-recent";
function recent() { try { return JSON.parse(localStorage.getItem(RECENT) || "[]"); } catch (e) { return []; } }
function remember(it) { try { localStorage.setItem(RECENT, JSON.stringify([signature(it), ...recent().filter((k) => k !== signature(it))].slice(0, 12))); } catch (e) { /* no storage: nothing remembered */ } }

// the names given to shapes by looking at them (web/library_names.json, keyed by game and shape); a shape without one shows by its look and size
async function loadNames() {
  if (names) return;
  const res = await fetch("/library_names.json");
  if (!res.ok) throw new Error("library_names.json: " + res.statusText);
  names = await res.json();
}

// the size class of an object from its box (tall: taller than it is wide and big enough to matter)
function sizeOf(lo, hi) {
  const w = Math.max(hi[0] - lo[0], hi[2] - lo[2]), h = hi[1] - lo[1], big = Math.max(w, h);
  if (h > 150 && h > w * 1.4) return "tall";
  if (big < 60) return "small";
  if (big < 250) return "medium";
  return "large";
}

// the objects of every loaded model; ones alike in shape and size (the same chair in two rooms) show once, counted
function build(renderer) {
  const seen = new Map(), out = [];
  const order = [store.room, ...store.scene.rooms.map((r) => r.file).filter((f) => f !== store.room)];
  for (const file of order) {
    const m = renderer.models[file];
    if (!m) continue;
    for (const mesh of m.meshes) {
      const lo = mesh.lo, hi = mesh.hi;
      if (mesh.hide || hi[0] - lo[0] > SHELL || hi[2] - lo[2] > SHELL) continue;
      const tris = mesh.groups.reduce((n, g) => n + g.count / 3, 0);
      if (tris < 4) continue;
      const d = [0, 1, 2].map((k) => hi[k] - lo[k]), most = Math.max(...d), least = Math.min(...d);
      // a prop stands on its own: not a light's glow or a shadow (see-through), a flat decal, a sliver or a piece of wall
      const raw = m.raw.meshes.find((x) => x.mesh === mesh.mesh);
      const prop = raw && !mesh.blend && !mesh.additive && most <= 500 && most >= 12 && least >= 6 && tris >= 12 && most / least <= 10 && inOnePiece(raw);
      const key = [tris, ...[0, 1, 2].map((k) => Math.round(hi[k] - lo[k]))].join(",");
      const hit = seen.get(key);
      if (hit) { hit.rooms.add(file); continue; }
      const it = { room: file, mesh: mesh.mesh, prop, size: sizeOf(lo, hi), dims: [0, 1, 2].map((k) => Math.round(hi[k] - lo[k])), tris, rooms: new Set([file]) };
      it.name = names[`${store.game}:${key}`] || null;
      it.kind = kindOf(it.name || "");
      seen.set(key, it); out.push(it);
    }
  }
  return out;
}

const b64 = (s, T) => { const bin = atob(s), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return new T(u.buffer); };

// one solid thing, not scattered pieces (a room's decals, trims and glints are one object of many separate bits): most of its triangles are joined into one piece
function inOnePiece(raw) {
  const pos = b64(raw.p, Float32Array), id = new Map(), parent = [];
  const find = (a) => { while (parent[a] !== a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
  const vertex = [];
  for (let i = 0; i < pos.length; i += 3) {
    const key = `${Math.round(pos[i])},${Math.round(pos[i + 1])},${Math.round(pos[i + 2])}`;
    if (!id.has(key)) { id.set(key, parent.length); parent.push(parent.length); }
    vertex.push(id.get(key));
  }
  const tris = [];
  for (const g of raw.groups) { const idx = b64(g.i, Uint32Array); for (let t = 0; t + 2 < idx.length; t += 3) { tris.push([vertex[idx[t]], vertex[idx[t + 1]], vertex[idx[t + 2]]]); parent[find(vertex[idx[t]])] = find(vertex[idx[t + 1]]); parent[find(vertex[idx[t + 1]])] = find(vertex[idx[t + 2]]); } }
  const count = new Map();
  for (const t of tris) count.set(find(t[0]), (count.get(find(t[0])) || 0) + 1);
  return Math.max(0, ...count.values()) >= tris.length * 0.7;
}

// every object of the scenario once (for the page's console and for naming them)
export async function allObjects(vp) { await loadNames(); viewport = vp; if (stamp(vp.renderer) !== built) { catalogue = build(vp.renderer); built = stamp(vp.renderer); } return catalogue; }
export const signature = (it) => [it.tris, ...it.dims].join(",");

const stamp = (renderer) => store.room + "|" + Object.keys(renderer.models).length;

// pictures are drawn one at a time as the cards come into view, and again if the game's pictures were still arriving
const watcher = new IntersectionObserver((entries) => entries.forEach((e) => { if (e.isIntersecting) { watcher.unobserve(e.target); paint(e.target, 0); } }), { root: null, rootMargin: "200px" });
function paint(card, tries) {
  const it = card._item, r = viewport.renderer.thumbnail(it.room, it.mesh, 128);
  if (!r) return;
  card.querySelector("img").src = r.url;
  viewport.redraw();
  if (!r.ready && tries < 8) setTimeout(() => paint(card, tries + 1), 500);
}

const label = (it) => it.name || `Unnamed ${it.size === "tall" ? "tall" : it.size} shape`;

function shown() {
  const t = state.text.trim().toLowerCase();
  return catalogue.filter((it) => (it.name || (state.unnamed && it.prop)) && (state.kind !== "recent" || recent().includes(signature(it))) && (state.kind === "all" || state.kind === "recent" || it.kind === state.kind) && (state.where === "all" || it.rooms.has(store.room)) && (!t || (it.name || "").toLowerCase().includes(t)));
}

function fill() {
  grid.innerHTML = "";
  const list = shown();
  count.textContent = `${list.length} object${list.length === 1 ? "" : "s"}`;
  if (!list.length) grid.append(el("div", { class: "libempty" }, state.where === "here" ? "Nothing like that in this room. Try “Whole scenario”." : "Nothing matches."));
  for (const it of list) {
    const card = el("button", { class: "libcard", title: `${label(it)}\n${it.dims.join(" × ")} · ${it.tris} triangles${it.rooms.size > 1 ? ` · in ${it.rooms.size} rooms` : ""}` });
    card._item = it;
    card.append(el("img", { alt: "" }), el("span", {}, label(it)));
    if (store.libraryPick && store.libraryPick.room === it.room && store.libraryPick.mesh === it.mesh) card.classList.add("on");
    card.addEventListener("click", () => {
      remember(it);
      store.libraryPick = { room: it.room, mesh: it.mesh, name: label(it) };
      setTool("library");
      panel.classList.remove("on");       // out of the way so the room can be clicked; the Library button brings it back
      document.getElementById("libbtn")?.classList.add("on");
    });
    grid.append(card);
    watcher.observe(card);
  }
}

async function open() {
  await loadNames();
  if (!panel) {
    const chips = el("div", { class: "libchips" });
    const mk = (label, active, f) => { const b = el("button", { class: "chip" + (active ? " on" : "") }, label); b.addEventListener("click", () => { f(); [...b.parentElement.children].forEach((c) => c.classList.toggle("on", c === b)); fill(); }); return b; };
    chips.append(...KINDS.map(([id, label]) => mk(label, id === state.kind, () => (state.kind = id))));
    const where = el("div", { class: "libchips" });
    where.append(mk("This room", true, () => (state.where = "here")), mk("Whole scenario", false, () => (state.where = "all")));
    const rare = el("button", { class: "chip", title: "Also list shapes nobody has named yet (rough pieces of rooms)" }, "Unnamed shapes");
    rare.addEventListener("click", () => { state.unnamed = !state.unnamed; rare.classList.toggle("on", state.unnamed); fill(); });
    where.append(rare);
    const search = el("input", { class: "libsearch", placeholder: "Search…  (bottle, box, chair)" });
    search.addEventListener("input", () => { state.text = search.value; fill(); });
    const head = el("div", { class: "libhead" }, el("b", {}, "Library"), count = el("span", { class: "libcount" }), (() => { const x = el("button", { class: "libclose", title: "Close  (B)" }, "×"); x.addEventListener("click", close); return x; })());
    grid = el("div", { class: "libgrid" });
    note = el("div", { class: "libnote" });
    panel = el("div", { class: "float", id: "library" }, head, search, where, chips, note, grid);
    document.getElementById("view").append(panel);
  }
  const key = stamp(viewport.renderer);
  if (key !== built) { catalogue = build(viewport.renderer); built = key; }
  // a game whose shapes nobody has named yet: they all show, each by its picture
  const unnamedGame = !catalogue.some((it) => it.name);
  if (unnamedGame) state.unnamed = true;
  note.textContent = unnamedGame ? "These shapes aren't named yet: go by the pictures." : "";
  panel.classList.add("on");
  document.getElementById("libbtn")?.classList.add("on");
  fill();
}

export function close() {
  if (panel) panel.classList.remove("on");
  document.getElementById("libbtn")?.classList.remove("on");
  if (store.tool === "library") setTool("select");
}

export function toggle(vp) {
  viewport = vp;
  if (panel && panel.classList.contains("on")) close(); else open();
}

on("room", () => { if (panel && panel.classList.contains("on")) open(); });
