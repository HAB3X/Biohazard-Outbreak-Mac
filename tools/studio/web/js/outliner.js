// The outliner: the scenario's rooms and what is in each, searchable; click to go to a thing.
import { store, on, emit, select, setRoom, roomOf, describe, refEq, COLLECTIONS, AREA_TAGS } from "./scene.js";
import { icon } from "./icons.js";
import { el } from "./ui.js";

const host = () => document.getElementById("outline");
let open = new Set();
let query = "";

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue("--" + name).trim();

function leaves(r, c) {
  const list = r[c];
  return list.map((o) => {
    const d = describe(c, o);
    let label = d.label;
    if (c === "areas") {
      if (o.tag === 0) label = `Door ${o.door} → ${o.to}`;
      else if (o.tag === 1) label = `Exit → ${o.to}`;
      else label = `${d.label} ${o.fields[0] || ""}`.trim();
    } else if (c === "enemies") label = `Enemy ${o.id}`;
    else if (c === "items") label = `Item ${o.kind.toString(16).toUpperCase()} ×${o.qty}`;
    else label = `${d.label} ${o.i}`;
    return { ref: { c, file: r.file, i: o.i }, label, color: d.color, o, isNew: o.isNew };
  });
}

function matches(text) { return !query || text.toLowerCase().includes(query); }

export function render() {
  const h = host();
  h.innerHTML = "";
  h.append(el("div", { class: "panel-head" }, "Rooms", el("span", { class: "spacer" }), el("span", { class: "pill" }, `${store.scene.rooms.length}`)));
  const search = el("div", { class: "search", html: icon("search", 14) });
  const input = el("input", { placeholder: "Search rooms and things", value: query });
  input.addEventListener("input", () => { query = input.value.trim().toLowerCase(); renderTree(); });
  search.append(input);
  h.append(search);
  h.append(el("div", { id: "tree" }));
  renderTree();
}

function renderTree() {
  const tree = document.getElementById("tree");
  if (!tree) return;
  tree.innerHTML = "";
  for (const r of store.scene.rooms) {
    const groups = [];
    for (const c of ["areas", "enemies", "items", "cameras", "lights", "zones", "nodes"]) {
      const ls = leaves(r, c).filter((l) => matches(l.label) || matches(r.name || "") || matches(String(r.id)));
      if (ls.length) groups.push({ c, ls });
    }
    const nameHit = matches(`${r.id} ${r.name || ""}`);
    if (query && !nameHit && !groups.length) continue;
    const isOpen = open.has(r.file) || !!query;
    const room = el("div", { class: "room" + (r.file === store.room ? " current" : "") + (isOpen ? " open" : "") });
    const total = r.areas.length + r.items.length + r.enemies.length + r.cameras.length;
    const head = el("div", { class: "room-head" },
      el("span", { class: "chev", html: icon("chevron", 12) }),
      el("span", { class: "num" }, String(r.id) + (r.variant ? "·" + r.variant : "")),
      el("span", { class: "name", title: r.name || r.file }, r.name || r.file.replace(".rdt", "")),
      el("span", { class: "count" }, String(total)));
    head.addEventListener("click", (e) => {
      if (e.target.closest(".chev")) { open.has(r.file) ? open.delete(r.file) : open.add(r.file); renderTree(); return; }
      setRoom(r.file); emit("frame", r.file);
    });
    head.addEventListener("dblclick", () => { open.has(r.file) ? open.delete(r.file) : open.add(r.file); renderTree(); });
    room.append(head);
    const body = el("div", { class: "room-body" });
    for (const g of groups) {
      const meta = g.c === "areas" ? { label: "Areas" } : COLLECTIONS[g.c];
      body.append(el("div", { class: "group-label" }, meta.label + " (" + g.ls.length + ")"));
      for (const l of g.ls) {
        const leaf = el("div", { class: "leaf" + (store.selection.some((s) => refEq(s, l.ref)) ? " sel" : "") },
          el("span", { class: "sw", style: `background:${css(l.color)}` }), l.label, l.isNew ? el("span", { class: "tag" }, "new") : null);
        leaf.addEventListener("click", () => { select([l.ref]); emit("focus", l.ref); });
        body.append(leaf);
      }
    }
    room.append(body);
    tree.append(room);
  }
}

on("selection", () => { if (store.scene) { const f = store.selection[0] && store.selection[0].file; if (f) open.add(f); renderTree(); } });
on("room", () => store.scene && renderTree());
on("changed", () => store.scene && renderTree());
