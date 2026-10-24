// The Library: every object of every room's model in the scenario, as a scrolling grid of pictures. Pick one, then click in the room to put copies down.
import { store, setTool, on } from "./scene.js";
import { el } from "./ui.js";

const SHELL = 3500;      // wider than this is a room's own floor and walls
const SIZES = [["all", "Any size"], ["small", "Small"], ["medium", "Furniture"], ["tall", "Tall"], ["large", "Large"]];
let panel = null, grid = null, count = null, viewport = null;
let state = { size: "all", where: "here", text: "" };
let catalogue = [], built = "";

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
      const key = [tris, ...[0, 1, 2].map((k) => Math.round(hi[k] - lo[k]))].join(",");
      const hit = seen.get(key);
      if (hit) { hit.rooms.add(file); continue; }
      const it = { room: file, mesh: mesh.mesh, size: sizeOf(lo, hi), dims: [0, 1, 2].map((k) => Math.round(hi[k] - lo[k])), tris, rooms: new Set([file]), name: `object ${mesh.mesh} of ${file.replace(".rdt", "")}` };
      seen.set(key, it); out.push(it);
    }
  }
  return out;
}

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

function shown() {
  const t = state.text.trim().toLowerCase();
  return catalogue.filter((it) => (state.size === "all" || it.size === state.size) && (state.where === "all" || it.rooms.has(store.room)) && (!t || it.name.includes(t) || it.size.includes(t)));
}

function fill() {
  grid.innerHTML = "";
  const list = shown();
  count.textContent = `${list.length} object${list.length === 1 ? "" : "s"}`;
  if (!list.length) grid.append(el("div", { class: "libempty" }, state.where === "here" ? "Nothing of this size in this room. Try “Whole scenario”." : "Nothing matches."));
  for (const it of list) {
    const card = el("button", { class: "libcard", title: `${it.name}\n${it.dims.join(" × ")} · ${it.tris} triangles${it.rooms.size > 1 ? ` · in ${it.rooms.size} rooms` : ""}` });
    card._item = it;
    card.append(el("img", { alt: "" }), el("span", {}, it.size === "tall" ? "Tall" : it.size === "large" ? "Large" : it.size === "small" ? "Small" : "Furniture"));
    if (store.libraryPick && store.libraryPick.room === it.room && store.libraryPick.mesh === it.mesh) card.classList.add("on");
    card.addEventListener("click", () => {
      store.libraryPick = { room: it.room, mesh: it.mesh, name: it.name };
      setTool("library");
      fill();
    });
    grid.append(card);
    watcher.observe(card);
  }
}

function open() {
  if (!panel) {
    const chips = el("div", { class: "libchips" });
    const mk = (label, active, f) => { const b = el("button", { class: "chip" + (active ? " on" : "") }, label); b.addEventListener("click", () => { f(); [...b.parentElement.children].forEach((c) => c.classList.toggle("on", c === b)); fill(); }); return b; };
    chips.append(...SIZES.map(([id, label]) => mk(label, id === state.size, () => (state.size = id))));
    const where = el("div", { class: "libchips" });
    where.append(mk("This room", true, () => (state.where = "here")), mk("Whole scenario", false, () => (state.where = "all")));
    const search = el("input", { class: "libsearch", placeholder: "Filter…  (try “tall” or “small”)" });
    search.addEventListener("input", () => { state.text = search.value; fill(); });
    const head = el("div", { class: "libhead" }, el("b", {}, "Library"), count = el("span", { class: "libcount" }), (() => { const x = el("button", { class: "libclose", title: "Close  (B)" }, "×"); x.addEventListener("click", close); return x; })());
    grid = el("div", { class: "libgrid" });
    panel = el("div", { class: "float", id: "library" }, head, search, where, chips, grid);
    document.getElementById("view").append(panel);
  }
  const key = stamp(viewport.renderer);
  if (key !== built) { catalogue = build(viewport.renderer); built = key; }
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
