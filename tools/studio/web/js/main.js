// Outbreak Studio: starts the page, builds the window's parts and wires the keys.
import { api } from "./api.js";
import { icon } from "./icons.js";
import { el, toast } from "./ui.js";
import * as scene from "./scene.js";
import { store, on, emit, loadScene, commit, undo, redo, canUndo, canRedo, setTool, setMode, setRoom, select, currentEdits, roomOf, objectOf } from "./scene.js";
import { Viewport } from "./viewport.js";
import { PLACE, FIXED, THINGS } from "./tools.js";
import { render as renderInspector } from "./inspector.js";
import { render as renderOutline } from "./outliner.js";

let viewport = null;
let lastPatch = null;
let saveTimer = null;

// ---- the top bar
function buildTopbar() {
  const bar = document.getElementById("topbar");
  bar.innerHTML = "";
  bar.append(el("div", { class: "brand", html: icon("logo", 26) + "<span>Studio</span>" }));
  const game = el("select", { class: "select", title: "Game and scenario" });
  for (const n of store.scenarios) game.append(el("option", { value: `${store.game}:${n}`, selected: n === store.scenario }, `${store.game === "file1" ? "File 1" : "File 2"} · scenario ${n}`));
  game.addEventListener("change", async () => {
    const [g, s] = game.value.split(":");
    await api.switchTo(g, +s);
    location.reload();
  });
  bar.append(game, el("div", { class: "sep" }));
  const undoBtn = el("button", { class: "btn icon top-tip", "data-tip": "Undo  ⌘Z", html: icon("undo", 17), id: "undo" });
  const redoBtn = el("button", { class: "btn icon top-tip", "data-tip": "Redo  ⇧⌘Z", html: icon("redo", 17), id: "redo" });
  undoBtn.addEventListener("click", undo); redoBtn.addEventListener("click", redo);
  bar.append(el("div", { class: "row", style: "gap:4px" }, undoBtn, redoBtn), el("div", { class: "sep" }));
  const seg = el("div", { class: "seg", id: "viewseg" });
  for (const [m, label, ic] of [["plan", "Plan", "plan"], ["3d", "3D", "cube"]]) {
    const b = el("button", { class: store.mode === m ? "on" : "", "data-mode": m, html: icon(ic, 14) + " " + label });
    b.style.display = "inline-flex"; b.style.gap = "6px"; b.style.alignItems = "center";
    b.addEventListener("click", () => setMode(m));
    seg.append(b);
  }
  bar.append(seg);
  const snap = el("select", { class: "select", title: "Snap to a grid when moving", id: "snap" });
  for (const v of [0, 1, 5, 10, 25, 50, 100]) snap.append(el("option", { value: v, selected: v === store.snap }, v ? `Snap ${v}` : "Snap off"));
  snap.addEventListener("change", () => { store.snap = +snap.value; });
  bar.append(snap);
  bar.append(el("div", { class: "spacer" }));
  bar.append(el("div", { class: "pill", id: "state" }, el("span", { class: "dot" }), el("span", { id: "statetext" }, "Saved")));
  const panelL = el("button", { class: "btn icon ghost top-tip", "data-tip": "Rooms panel  [", html: icon("panelL", 17) });
  const panelR = el("button", { class: "btn icon ghost top-tip", "data-tip": "Inspector  ]", html: icon("panelR", 17) });
  panelL.addEventListener("click", () => togglePanel("no-outline")); panelR.addEventListener("click", () => togglePanel("no-inspector"));
  bar.append(panelL, panelR, el("div", { class: "sep" }));
  const build = el("button", { class: "btn", id: "build", html: icon("build", 16) + " Build" });
  const here = el("button", { class: "btn", id: "playhere", title: "Start the game in the room you are editing", html: icon("play", 14) + " Play here" });
  const play = el("button", { class: "btn primary", id: "play", html: icon("play", 14) + " Play" });
  here.addEventListener("click", () => doPlay(true));
  build.addEventListener("click", () => doBuild());
  play.addEventListener("click", () => doPlay());
  bar.append(build, here, play);
}

function togglePanel(cls) { document.getElementById("app").classList.toggle(cls); setTimeout(() => viewport && viewport.resize(), 30); }

// ---- the tool rail
function buildRail() {
  const rail = document.getElementById("rail");
  rail.innerHTML = "";
  const add = (t, extra = {}) => {
    const b = el("button", { class: "tool", "data-tool": t.id, "data-tip": `${t.label}  (${t.key})`, html: icon(t.icon, 21) + `<span class="key">${t.key}</span>`, ...extra });
    b.addEventListener("click", () => setTool(t.id));
    rail.append(b);
  };
  add({ id: "select", label: "Select and move", icon: "select", key: "V" });
  add({ id: "pan", label: "Pan the view", icon: "pan", key: "H" });
  rail.append(el("div", { class: "rail-sep" }), el("div", { class: "rail-label" }, "Place"));
  PLACE.forEach((t) => add(t));
  rail.append(el("div", { class: "rail-sep" }));
  THINGS.forEach((t) => add(t));
  FIXED.forEach((t) => add(t, { style: "opacity:.55" }));
  syncRail();
}
const syncRail = () => document.querySelectorAll(".tool").forEach((b) => b.classList.toggle("on", b.dataset.tool === store.tool));

// ---- the view's overlays
function buildHud() {
  const tl = document.getElementById("hud-tl"), tr = document.getElementById("hud-tr"), bl = document.getElementById("hud-bl"), br = document.getElementById("hud-br");
  tl.innerHTML = "";
  tl.append(el("div", { class: "hint", id: "hint" }));
  tr.innerHTML = "";
  const layers = el("div", { class: "menu", id: "layers" });
  const LAYERS = [["models", "Rooms"], ["areas", "Areas"], ["cameras", "Cameras"], ["items", "Items"], ["enemies", "Enemies"], ["lights", "Lights"], ["zones", "Sound"], ["nodes", "AI nodes"], ["labels", "Labels"], ["grid", "Grid"]];
  const rows = [...LAYERS, ["textures", "Textures (3D)"]];
  const btn = el("button", { class: "chip", html: icon("eye", 15) + " Layers" });
  const pop = el("div", { class: "popover" });
  for (const [k, label] of rows) {
    const row = el("label", { class: "check" }, el("input", { type: "checkbox", checked: store.show[k] }), label);
    row.firstChild.addEventListener("change", () => { store.show[k] = row.firstChild.checked; viewport.redraw(); });
    pop.append(row);
  }
  btn.addEventListener("click", (e) => { e.stopPropagation(); pop.classList.toggle("on"); });
  document.addEventListener("click", (e) => { if (!pop.contains(e.target)) pop.classList.remove("on"); });
  layers.append(btn, pop);
  tr.append(layers);
  bl.innerHTML = "";
  bl.append(el("div", { class: "chip", id: "coords" }, "—"), el("div", { class: "chip", id: "loadchip", style: "display:none" }));
  br.innerHTML = "";
  const mk = (ic, tip, fn) => { const b = el("button", { class: "chip", title: tip, html: icon(ic, 15) }); b.addEventListener("click", fn); return b; };
  br.append(el("div", { class: "hud-row" },
    mk("target", "Frame this room (F)", () => viewport.frameRoom(store.room)),
    mk("grid", "Frame the whole scenario", () => viewport.frameAll())));
  updateHint();
}

function updateHint() {
  const h = document.getElementById("hint");
  if (!h) return;
  const t = [...PLACE, ...THINGS].find((p) => p.id === store.tool);
  if (t) h.innerHTML = `<b>${t.label}</b>: ${t.hint}`;
  else if (store.tool === "pan") h.innerHTML = "<b>Pan</b>: drag to move the view. Hold <kbd>Space</kbd> any time to pan.";
  else if (FIXED.some((p) => p.id === store.tool)) h.innerHTML = `<b>${FIXED.find((p) => p.id === store.tool).label}</b>: ${FIXED.find((p) => p.id === store.tool).soon}`;
  else h.innerHTML = "<b>Select</b>: click a thing, drag it to move it, drag its white handles to reshape. <kbd>Shift</kbd>-click adds to the selection.";
}

function buildRoomBar() {
  const bar = document.getElementById("roombar");
  bar.innerHTML = "";
  for (const r of store.scene.rooms) {
    const c = el("button", { class: "chip" + (r.file === store.room ? " on" : ""), title: r.name || r.file }, String(r.id) + (r.variant ? "·" + r.variant : ""));
    c.addEventListener("click", () => { setRoom(r.file); viewport.frameRoom(r.file); });
    bar.append(c);
  }
}

// ---- the status bar
function buildStatus() {
  const s = document.getElementById("status");
  s.innerHTML = "";
  s.append(el("span", { id: "st-sel" }), el("span", { class: "spacer" }), el("span", { id: "st-prob", style: "cursor:pointer" }), el("span", { id: "st-step" }));
  updateStatus();
}

function validate() {
  const out = [];
  const ids = new Set(store.scene.rooms.map((r) => r.id));
  for (const r of store.scene.rooms) {
    for (const a of r.areas) {
      if ((a.tag === 0 || a.tag === 1) && !ids.has(a.fields[1])) out.push({ level: "bad", text: `Room ${r.id}: ${a.tag === 0 ? "door" : "exit"} ${a.i} leads to room ${a.fields[1]}, which is not in this scenario`, ref: { c: "areas", file: r.file, i: a.i } });
    }
    const doors = {};
    for (const a of r.areas) if (a.tag === 0) doors[a.fields[0]] = (doors[a.fields[0]] || 0) + 1;
    for (const [n, k] of Object.entries(doors)) if (k > 1) out.push({ level: "warn", text: `Room ${r.id}: ${k} doors share number ${n} (the game crashed with more than one copy in testing)` });
  }
  store.problems = out;
  return out;
}

function updateStatus() {
  const sel = document.getElementById("st-sel");
  if (!sel) return;
  const n = store.selection.length;
  const r = roomOf(store.room);
  sel.textContent = n === 0 ? `Room ${r ? r.id : "—"}${r && r.name ? " · " + r.name : ""}` : n === 1 ? (() => { const o = objectOf(store.selection[0]); return o ? `${scene.describe(store.selection[0].c, o).label} ${o.i} · room ${r.id}` : ""; })() : `${n} selected`;
  const probs = validate();
  const p = document.getElementById("st-prob");
  p.textContent = probs.length ? `${probs.length} problem${probs.length > 1 ? "s" : ""}` : "No problems";
  p.style.color = probs.some((x) => x.level === "bad") ? "var(--bad)" : probs.length ? "var(--warn)" : "var(--good)";
  p.onclick = () => probs.length && toast(`${probs.length} problem${probs.length > 1 ? "s" : ""}`, "err", probs.slice(0, 6).map((x) => x.text).join("\n"), 9000);
  document.getElementById("st-step").textContent = `Step ${store.at} of ${store.history.length - 1}`;
  const u = document.getElementById("undo"), rd = document.getElementById("redo");
  if (u) { u.disabled = !canUndo(); rd.disabled = !canRedo(); }
}

function setState(text, cls = "") {
  const s = document.getElementById("state"), t = document.getElementById("statetext");
  if (!s) return;
  s.className = "pill " + cls; t.textContent = text;
}

// ---- save, build, play
function scheduleSave() {
  setState("Unsaved", "dirty");
  clearTimeout(saveTimer);
  saveTimer = setTimeout(async () => {
    try { await api.saveProject({ edits: currentEdits(), meta: { room: store.room } }); store.dirty = false; setState("Saved"); }
    catch (e) { setState("Not saved", "dirty"); toast("Couldn't save", "err", e.message); }
  }, 700);
}

async function doBuild() {
  setState("Building…", "busy");
  try {
    const out = await api.build(currentEdits());
    lastPatch = out.patch; lastBuilt = JSON.stringify(currentEdits());
    setState(store.dirty ? "Unsaved" : "Saved", store.dirty ? "dirty" : "");
    toast("Built", "ok", out.report.slice(0, 8).join("\n") + `\n${(out.bytes / 1048576).toFixed(1)} MB patch in ${out.seconds}s`, 5200);
    return out;
  } catch (e) {
    setState("Build failed", "dirty");
    toast("The build failed", "err", e.message, 9000);
    return null;
  }
}
let lastBuilt = "";

// the doors and exits that take the player from the scenario's first room to a room: [x, z] to stand at for each step (a breadth-first search over the scene's own doors)
function hopsTo(startId, targetId) {
  const rooms = store.scene.rooms.filter((r) => !r.variant);
  const byId = new Map(rooms.map((r) => [r.id, r]));
  const prev = new Map([[startId, null]]);
  const queue = [startId];
  while (queue.length) {
    const id = queue.shift();
    if (id === targetId) break;
    const r = byId.get(id);
    if (!r) continue;
    for (const a of r.areas) {
      if ((a.tag === 0 || a.tag === 1) && byId.has(a.fields[1]) && !prev.has(a.fields[1])) { prev.set(a.fields[1], { from: id, area: a }); queue.push(a.fields[1]); }
    }
  }
  if (!prev.has(targetId)) return null;
  const hops = [];
  for (let at = targetId; prev.get(at); at = prev.get(at).from) { const c = scene.centre("areas", prev.get(at).area); hops.unshift([c[0], c[2]]); }
  return hops;
}

async function doPlay(here = false) {
  if (!lastPatch || lastBuilt !== JSON.stringify(currentEdits())) { if (!(await doBuild())) return; }
  try {
    let hops = null, at = null;
    if (here) {
      const info = await api.status();
      const room = roomOf(store.room);
      if (!info.automatable || !room) { toast("Play here isn't set up for this scenario yet", "err", "Only File 1's first scenario and File 2's first scenario start by themselves so far."); return; }
      hops = hopsTo(info.start_room, room.id);
      if (!hops) { toast("No way to reach this room", "err", "No chain of doors and exits leads there from the scenario's first room."); return; }
      const sp = room.spawns && room.spawns[0];
      at = sp && hops.length === 0 ? [sp.pos[0], sp.pos[2], sp.facing] : null;
    }
    const out = await api.play(lastPatch, hops, at);
    toast("Starting the game…", "ok", out.automated ? "It takes itself to the room; give it a minute." : "The game opens in its own window with your changes.");
  } catch (e) { toast("Couldn't start the game", "err", e.message); }
}

// ---- the command palette
function palette() {
  const root = document.getElementById("palette"), input = document.getElementById("pal-input"), list = document.getElementById("pal-list");
  let items = [], at = 0;
  const commands = () => [
    { label: "Build the patch", hint: "⌘B", run: doBuild }, { label: "Build and play", hint: "⌘↵", run: doPlay },
    { label: "Undo", hint: "⌘Z", run: undo }, { label: "Redo", hint: "⇧⌘Z", run: redo },
    { label: "Plan view", hint: "1", run: () => setMode("plan") }, { label: "3D view", hint: "2", run: () => setMode("3d") },
    { label: "Frame this room", hint: "F", run: () => viewport.frameRoom(store.room) }, { label: "Frame the scenario", run: () => viewport.frameAll() },
    ...store.scene.rooms.map((r) => ({ label: `Go to room ${r.id}${r.variant ? "·" + r.variant : ""} ${r.name || ""}`, run: () => { setRoom(r.file); viewport.frameRoom(r.file); } })),
  ];
  const show = () => {
    const q = input.value.toLowerCase();
    items = commands().filter((c) => c.label.toLowerCase().includes(q)).slice(0, 30);
    at = Math.min(at, Math.max(0, items.length - 1));
    list.innerHTML = "";
    items.forEach((c, i) => {
      const li = el("li", { class: i === at ? "on" : "" }, c.label, c.hint ? el("small", {}, c.hint) : null);
      li.addEventListener("click", () => run(c));
      list.append(li);
    });
  };
  const run = (c) => { close(); c.run(); };
  const close = () => root.classList.remove("on");
  input.addEventListener("input", () => { at = 0; show(); });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowDown") { at = Math.min(items.length - 1, at + 1); show(); e.preventDefault(); }
    else if (e.key === "ArrowUp") { at = Math.max(0, at - 1); show(); e.preventDefault(); }
    else if (e.key === "Enter" && items[at]) run(items[at]);
    else if (e.key === "Escape") close();
  });
  root.addEventListener("mousedown", (e) => { if (e.target === root) close(); });
  return () => { root.classList.add("on"); input.value = ""; at = 0; show(); input.focus(); };
}

// ---- keys
function keys(openPalette) {
  window.addEventListener("keydown", (e) => {
    const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName);
    const meta = e.metaKey || e.ctrlKey;
    if (meta && e.key.toLowerCase() === "k") { e.preventDefault(); openPalette(); return; }
    if (typing && !(meta && ["z", "s", "b", "Enter"].includes(e.key))) return;
    if (meta && e.key.toLowerCase() === "z") { e.preventDefault(); e.shiftKey ? redo() : undo(); }
    else if (meta && e.key.toLowerCase() === "y") { e.preventDefault(); redo(); }
    else if (meta && e.key.toLowerCase() === "b") { e.preventDefault(); doBuild(); }
    else if (meta && e.key === "Enter") { e.preventDefault(); doPlay(); }
    else if (meta && e.key.toLowerCase() === "s") { e.preventDefault(); doBuild(); }
    else if (meta) return;
    else if (e.key === " ") { viewport.space = true; viewport.ov.style.cursor = "grab"; e.preventDefault(); }
    else if (e.key === "Escape") { select([]); setTool("select"); }
    else if (e.key === "Delete" || e.key === "Backspace") { const s = store.selection[0]; if (s && store.selection.length === 1) { const o = objectOf(s); if (o && o.isNew) { const r = roomOf(s.file); r[s.c].splice(r[s.c].findIndex((x) => x.i === s.i), 1); select([]); commit("Delete"); } else toast("Only things you added can be deleted", "err"); } }
    else if (e.key === "1") setMode("plan");
    else if (e.key === "2") setMode("3d");
    else if (e.key === "Tab") { e.preventDefault(); setMode(store.mode === "plan" ? "3d" : "plan"); }
    else if (e.key.toLowerCase() === "f") { const s = store.selection[0]; s ? viewport.focus(s) : viewport.frameRoom(store.room); }
    else if (e.key.toLowerCase() === "g") { store.show.grid = !store.show.grid; viewport.redraw(); }
    else if (e.key === "[") togglePanel("no-outline");
    else if (e.key === "]") togglePanel("no-inspector");
    else {
      const all = [{ id: "select", key: "V" }, { id: "pan", key: "H" }, ...PLACE, ...THINGS, ...FIXED];
      const t = all.find((x) => x.key === e.key.toUpperCase());
      if (t) setTool(t.id);
    }
  });
  window.addEventListener("keyup", (e) => { if (e.key === " ") { viewport.space = false; viewport.updateCursor(); } });
}

// ---- start
async function boot() {
  const msg = document.getElementById("loadmsg");
  try {
    const [data, saved] = await Promise.all([api.scene(), api.project()]);
    msg.textContent = "Setting up the editor…";
    loadScene(data, saved);
    api.messages().then((m) => { store.messages = m; renderInspector(); }).catch(() => {});
    buildTopbar(); buildRail(); buildStatus();
    viewport = new Viewport();
    window.studio = { store, viewport, scene };      // for the console
    buildHud(); buildRoomBar(); renderOutline(); renderInspector();
    on("tool", () => { syncRail(); updateHint(); });
    on("mode", () => { document.querySelectorAll("#viewseg button").forEach((b) => b.classList.toggle("on", b.dataset.mode === store.mode)); });
    on("room", () => { buildRoomBar(); updateStatus(); });
    on("selection", updateStatus);
    on("changed", () => { updateStatus(); scheduleSave(); });
    on("focus", (ref) => viewport.focus(ref));
    on("frame", (f) => viewport.frameRoom(f));
    on("note", (text, kind) => toast(text, kind === "warn" ? "err" : ""));
    on("pointer", (p) => { const c = document.getElementById("coords"); if (c && p) c.textContent = `x ${p[0]}  z ${p[1]}`; });
    on("camera", () => {});
    keys(palette());
    const progress = (done, total) => {
      const chip = document.getElementById("loadchip");
      if (!chip) return;
      chip.style.display = done < total ? "inline-flex" : "none";
      chip.textContent = `Loading rooms ${done}/${total}`;
    };
    viewport.frameRoom(store.room);
    document.getElementById("loading").classList.add("gone");
    await viewport.loadModels(progress);
    viewport.frameRoom(store.room);
  } catch (e) {
    msg.textContent = "Couldn't start: " + e.message;
    console.error(e);
  }
}

boot();
