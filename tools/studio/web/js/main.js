// Outbreak Studio: starts the page, builds the window's parts and wires the keys.
import { api } from "./api.js";
import { icon } from "./icons.js";
import { el, toast } from "./ui.js";
import * as scene from "./scene.js";
import { store, on, emit, loadScene, commit, undo, redo, canUndo, canRedo, setTool, setMode, setRoom, select, currentEdits, roomOf, objectOf } from "./scene.js";
import { Viewport } from "./viewport.js";
import { removeCopy, addModel, setPose, duplicate } from "./scenery.js";
import { parseObj } from "./objimport.js";
import { PLACE, FIXED, THINGS, WALL, FLOOR } from "./tools.js";
import { toggle as toggleLibrary, close as closeLibrary } from "./library.js";
import { render as renderInspector } from "./inspector.js";
import { render as renderOutline } from "./outliner.js";

let viewport = null;
let lastPatch = null;
let saveTimer = null;

// ---- the top bar: where you are, the view, undo, and Play
const ADD = [
  { id: "door", label: "Door", about: "Leads to another room", icon: "door", key: "D" },
  { id: "exit", label: "Exit", about: "A step-in way out of the room", icon: "exit", key: "E" },
  { id: "item", label: "Item", about: "Something to pick up", icon: "item", key: "I" },
  { id: "enemy", label: "Enemy", about: "A zombie or monster", icon: "enemy", key: "N" },
  { id: "spawn", label: "Player start", about: "Where the player begins", icon: "spawn", key: "S" },
  { id: "camera", label: "Camera", about: "A fixed viewpoint", icon: "camera", key: "C" },
  { id: "examine", label: "Examine spot", about: "Something to look at", icon: "message", key: "M" },
  { id: "pickup", label: "Pickup trigger", about: "An item's pickup area", icon: "item", key: "P" },
  { id: "wall", label: "Wall", about: "A solid line nobody can cross", icon: "wall", key: "W" },
  { id: "floor", label: "Floor", about: "A flat place to stand, raised or not", icon: "grid", key: "F" },
  { id: "ladder", label: "Ladder", about: "Something to climb", icon: "ladder", key: "L" },
  { id: "zone", label: "Script zone", about: "A zone a script can wait on", icon: "zone", key: "Z" },
];
const pop = (cls, ...kids) => el("div", { class: "pop " + cls }, ...kids);
const closeOnOutside = (menu, p) => document.addEventListener("click", (e) => { if (!menu.contains(e.target)) p.classList.remove("on"); });

function buildTopbar() {
  const bar = document.getElementById("topbar");
  bar.innerHTML = "";
  bar.append(el("span", { class: "brand" }, "Studio"));
  // which scenario
  const place = el("div", { class: "menu" });
  const pb = el("button", { class: "pickbtn", html: `${store.game === "file1" ? "File 1" : "File 2"} · scenario ${store.scenario} ${icon("down", 12)}` });
  const pp = pop("down");
  for (const n of store.scenarios) {
    const it = el("button", { class: "item" }, `${store.game === "file1" ? "File 1" : "File 2"} · scenario ${n}`);
    it.addEventListener("click", async () => { await api.switchTo(store.game, n); location.reload(); });
    pp.append(it);
  }
  const other = store.game === "file1" ? "file2" : "file1";
  pp.append(el("hr"));
  const sw = el("button", { class: "item" }, `Open ${other === "file1" ? "File 1" : "File 2"} instead`);
  sw.addEventListener("click", async () => { const r = await fetch("/api/scene"); await api.switchTo(other, other === "file1" ? 1 : 40); location.reload(); });
  pp.append(sw);
  pb.addEventListener("click", (e) => { e.stopPropagation(); pp.classList.toggle("on"); });
  place.append(pb, pp); closeOnOutside(place, pp);
  bar.append(place, el("div", { class: "spacer" }));
  const seg = el("div", { class: "seg", id: "viewseg" });
  for (const [m, label] of [["3d", "Build"], ["game", "Camera views"], ["plan", "Plan"]]) {
    const b = el("button", { class: store.mode === m ? "on" : "", "data-mode": m }, label);
    b.addEventListener("click", () => setMode(m));
    seg.append(b);
  }
  bar.append(seg, el("div", { class: "spacer" }));
  bar.append(el("span", { class: "quiet", id: "statetext" }, "Saved"));
  const problems = el("button", { class: "link", id: "problems", style: "display:none" });
  bar.append(problems);
  const undoBtn = el("button", { class: "btn icon", title: "Undo  ⌘Z", html: icon("undo", 17), id: "undo" });
  const redoBtn = el("button", { class: "btn icon", title: "Redo  ⇧⌘Z", html: icon("redo", 17), id: "redo" });
  undoBtn.addEventListener("click", undo); redoBtn.addEventListener("click", redo);
  bar.append(undoBtn, redoBtn);
  // Play, with the other ways to start
  const play = el("div", { class: "menu" });
  const split = el("div", { class: "split" });
  const main = el("button", { class: "btn solid", id: "play", html: icon("play", 13) + " Play" });
  const caret = el("button", { class: "btn solid", html: icon("down", 14) });
  split.append(main, caret);
  const pm = pop("down right",
    menuItem("Play from the start", "The scenario's first room", () => doPlay(false)),
    menuItem("Play from this room", "Takes the game to the room you are looking at", () => doPlay(true)),
    el("hr"),
    menuItem("Build only", "Make the patch without starting the game", () => doBuild()),
    menuItem("Save the patch file", "Your changes as one file to keep or share", async () => { if (await doBuild()) { const a = el("a", { href: "/api/download", download: "" }); document.body.append(a); a.click(); a.remove(); } }));
  main.addEventListener("click", () => doPlay(false));
  caret.addEventListener("click", (e) => { e.stopPropagation(); pm.classList.toggle("on"); });
  play.append(split, pm); closeOnOutside(play, pm);
  bar.append(play);
}

function menuItem(label, about, fn, ic) {
  const b = el("button", { class: "item" }, ic ? el("span", { html: icon(ic, 18) }) : null, el("span", {}, label, about ? el("small", {}, about) : null));
  b.addEventListener("click", () => { document.querySelectorAll(".pop.on").forEach((x) => x.classList.remove("on")); fn(); });
  return b;
}

function togglePanel(cls) { document.getElementById("app").classList.toggle(cls); setTimeout(() => viewport && viewport.resize(), 30); }

// ---- the floating toolbar: select, hand, and one Add menu
function buildToolbar() {
  const bar = document.getElementById("toolbar");
  bar.innerHTML = "";
  const sel = el("button", { class: "btn", "data-tool": "select", title: "Select and move  (V)", html: icon("select", 18) + " Select" });
  const objs = el("button", { class: "btn", "data-tool": "scenery", title: "Pick the room's objects to move, turn, resize or hide  (O)", html: icon("cube", 18) + " Objects" });
  objs.addEventListener("click", () => setTool("scenery"));
  const hand = el("button", { class: "btn icon", "data-tool": "pan", title: "Move the view  (H, or hold Space)", html: icon("pan", 18) });
  sel.addEventListener("click", () => setTool("select")); hand.addEventListener("click", () => setTool("pan"));
  const add = el("div", { class: "menu" });
  const ab = el("button", { class: "btn", id: "addbtn", html: icon("plus", 17) + " Add" });
  const grid = el("div", { class: "grid2" });
  for (const a of ADD) {
    const it = menuItem(a.label, a.about, () => setTool(a.id), a.icon);
    it.append(el("span", { class: "key" }, a.key));
    grid.append(it);
  }
  const lib = menuItem("Library", "Browse the game's tables, shelves, chairs… and place them", () => toggleLibrary(viewport), "cube");
  const own = menuItem("Your own model", "Bring in a .obj file as scenery", () => importModel(), "cube");
  const am = pop("up center", grid, el("hr"), lib, own);
  ab.addEventListener("click", (e) => { e.stopPropagation(); am.classList.toggle("on"); });
  add.append(ab, am); closeOnOutside(add, am);
  const libBtn = el("button", { class: "btn", id: "libbtn", title: "Browse the game's objects and place them  (B)", html: icon("cube", 18) + " Library" });
  libBtn.addEventListener("click", () => toggleLibrary(viewport));
  bar.append(sel, objs, hand, el("span", { class: "vsep" }), libBtn, add);
  syncRail();
}
// a .obj from the disk becomes a new object in the room being worked in, standing where the view is centred
function importModel() {
  const pick = el("input", { type: "file", accept: ".obj" });
  pick.addEventListener("change", async () => {
    const file = pick.files[0];
    if (!file) return;
    try {
      const room = roomOf(store.room);
      if (!room || !room.scenery || !room.scenery.length) throw new Error("This room's model isn't loaded yet.");
      const g = parseObj(await file.text());
      const at = viewport.camera.target, y = viewport.floorAt(room.file, at[0], at[2], at[1] + 3000);
      const c = addModel(room, g, viewport.renderer, [at[0], y === null ? 0 : y, at[2]]);
      commit("Import model");
      select([{ c: "scenery", file: room.file, i: c.i }]);
      setTool("scenery");
      toast("Model added", "ok", `${g.triangles.length} triangles. Pick a picture for it in the panel.`);
    } catch (e) { toast("Couldn't use that file", "err", e.message, 6000); }
  });
  pick.click();
}
// in the game view: which of the room's cameras the picture is seen through
function buildCamNav() {
  const nav = el("div", { class: "float", id: "camnav" });
  const prev = el("button", { class: "btn icon", title: "The previous camera  (,)", html: icon("chevron", 16, 'style="transform:rotate(180deg)"') });
  const next = el("button", { class: "btn icon", title: "The next camera  (.)", html: icon("chevron", 16) });
  const label = el("span", { class: "camlabel" });
  prev.addEventListener("click", () => viewport.stepCamera(-1)); next.addEventListener("click", () => viewport.stepCamera(1));
  const marks = el("button", { class: "btn", title: "Show or hide the markers drawn over the picture", html: icon("eye", 16) + " Markers" });
  let shown = true;
  marks.addEventListener("click", () => {
    shown = !shown;
    for (const k of ["areas", "items", "enemies", "labels"]) store.show[k] = shown;
    marks.classList.toggle("on", shown);
    viewport.redraw();
  });
  marks.classList.add("on");
  const real = el("button", { class: "btn", title: "Show the game's own picture of this camera (the game is started behind the scenes; the first picture takes a minute or two)", html: icon("play", 14) + " Real view" });
  real.addEventListener("click", () => { store.real.on = !store.real.on; store.real.always = store.real.on; real.classList.toggle("on", store.real.on); if (store.real.on) refreshReal(); else viewport.setShot(null); });
  const refresh = el("button", { class: "btn", style: "display:none", title: "Start the game again with your changes and take the picture again" }, "Update picture");
  refresh.addEventListener("click", () => refreshReal(true));
  store.real.chip = { label, refresh };
  nav.append(prev, label, next, el("span", { class: "vsep" }), marks, real, refresh);
  document.getElementById("view").append(nav);
  const hint = el("div", { class: "float", id: "flyhint" });
  hint.innerHTML = "<b>W A S D</b> move &nbsp; <b>Q E</b> down, up &nbsp; <b>drag</b> to look &nbsp; <b>wheel</b> forward, back &nbsp; <b>Shift</b> faster &nbsp; ";
  const back = el("button", { class: "btn", style: "margin-left:6px" }, "Back to the game's camera");
  back.addEventListener("click", () => setMode("game"));
  const pic = el("button", { class: "btn", style: "margin-left:6px", title: "Show the game's own picture from where the free camera is (taken in the game started behind the scenes, a moment after you stop moving)", html: icon("play", 14) + " Game's picture" });
  pic.addEventListener("click", () => { store.real.free = !store.real.free; pic.classList.toggle("on", store.real.free); freeChanged(true); });
  store.real.freeBtn = pic;
  const upd = el("button", { class: "btn", style: "display:none;margin-left:6px", title: "Start the game again with your changes and take the picture again" }, "Update picture");
  upd.addEventListener("click", () => refreshFree(true));
  store.real.freeUpdate = upd;
  hint.append(back, pic, upd);
  hint.style.pointerEvents = "auto";
  const editHint = el("div", { class: "float", id: "edithint" });
  editHint.innerHTML = "Pick a thing, then <b>drag</b> it to move it &nbsp; <b>arrow keys</b> nudge &nbsp; <b>R</b> turns &nbsp; the handles lift, turn and resize objects";
  document.getElementById("view").append(hint, editHint);
  const sync = () => {
    const r = store.scene && roomOf(store.room);
    nav.style.display = store.mode === "game" && r && r.cameras.length ? "flex" : "none";
    hint.style.display = store.mode === "3d" ? "block" : "none";
    editHint.style.display = store.mode !== "plan" && store.selection.length ? "block" : "none";
    if (r) label.textContent = `Camera ${(viewport.camIndex[r.file] ?? 0) + 1} of ${r.cameras.length}`;
  };
  on("camera", () => { sync(); if (store.real.on) refreshReal(); if (store.mode === "3d") freeChanged(); });
  on("mode", () => { sync(); if (store.mode !== "game") viewport.setShot(null); if (store.mode === "game" && store.real.on) refreshReal(); freeChanged(); });
  on("room", sync); on("selection", sync);
  on("changed", () => { if (store.real.on) refreshReal(); });
  sync();
}
// ---- the real view: the game's own picture of the room and camera shown, taken in a game started behind the scenes on the current edits
const hash = (s) => { let h = 5381; for (let i = 0; i < s.length; i++) h = ((h << 5) + h + s.charCodeAt(i)) | 0; return String(h); };
async function refreshReal(force) {
  const real = store.real, r = roomOf(store.room);
  if (!real.on || store.mode !== "game" || !r || real.busy) return;
  const k = viewport.camIndex[r.file], c = r.cameras[k];
  if (!c) return;
  const edits = JSON.stringify(currentEdits()), clean = edits === '{"rooms":{}}';
  const key = hash(edits), slot = `${key}:${r.file}:${k}`;
  const { label, refresh } = real.chip;
  const taken = store.survey.have.has(`r${r.id}c${k}`) ? `/api/shot?kind=survey&name=r${r.id}c${k}.jpg` : null;     // the game's picture taken with nothing changed
  if (clean && taken) { viewport.setShot(taken, false); refresh.style.display = "none"; return; }
  if (!force && real.urls[slot]) { viewport.setShot(real.urls[slot], false); return; }
  if (!force && (taken || real.last)) { viewport.setShot(taken || real.last, true); refresh.style.display = ""; return; }     // changed since: the picture stays, dimmed, until you ask
  if (!force && !real.always) { viewport.setShot(null); return; }
  real.busy = true;
  label.classList.add("busy"); refresh.style.display = "none";
  try {
    if (real.key !== key) {
      label.textContent = "Starting the game…";
      const out = await api.previewPrepare(currentEdits());
      Object.assign(real, { key, room: out.room, urls: {} });
    }
    label.textContent = "Taking the picture…";
    const hops = hopsTo(real.room, r.id);
    if (!hops) throw new Error("no chain of doors leads there from where the game is");
    const b = c.box.slice(0, 4), at = [b.reduce((s, p) => s + p[0], 0) / 4, b.reduce((s, p) => s + p[2], 0) / 4, 0];
    const out = await api.previewShot(r.id, hops, at, "s" + Date.now() % 1e7);
    real.room = r.id; real.urls[slot] = out.url; real.last = out.url;
    if (viewport.camIndex[r.file] === k && store.room === r.file) viewport.setShot(out.url, false);
  } catch (e) {
    toast("The real view didn't work", "err", e.message, 7000);
    viewport.setShot(null);
  } finally {
    real.busy = false; label.classList.remove("busy"); emit("camera");
  }
}

// ---- the free camera's game picture: a moment after the camera stops, the game is asked for its own picture from exactly there
let freeTimer = null, freeSeq = 0, freePose = "";
const poseOf = () => JSON.stringify([viewport.camera.eye().map(Math.round), viewport.camera.target.map(Math.round), store.room, store.mode]);
// the view is drawn many times a second (the "camera" event comes with each): only a different pose, mode or room asks for another picture
function freeChanged(force) {
  const pose = poseOf();
  if (!force && pose === freePose) return;
  freePose = pose;
  clearTimeout(freeTimer);
  const on = store.mode === "3d" && store.real.free && store.survey.supported;
  viewport.usePhoto(on);
  const known = on && onGameCamera();
  if (known) { viewport.setShot(known, false); return; }
  viewport.setShot(on ? viewport.shot : null, true);       // the last picture stays, dimmed, until the game's new one arrives
  if (on) freeTimer = setTimeout(refreshFree, 450);
}
// where the free camera stands exactly where one of the room's cameras does, with nothing changed since: the picture already taken of it, with no need for the game
function onGameCamera() {
  const r = roomOf(store.room), cam = viewport.camera;
  if (!r || JSON.stringify(currentEdits()) !== '{"rooms":{}}') return null;
  const eye = cam.eye(), k = r.cameras.findIndex((c) => c.eye.every((v, i) => Math.abs(v - eye[i]) < 2) && c.target.every((v, i) => Math.abs(v - cam.target[i]) < 2));
  return k >= 0 && store.survey.have.has(`r${r.id}c${k}`) ? `/api/shot?kind=survey&name=r${r.id}c${k}.jpg` : null;
}

async function refreshFree(force) {
  const real = store.real, r = roomOf(store.room);
  if (store.mode !== "3d" || !real.free || !r || !store.survey.supported) return;
  if (real.busy) { freeTimer = setTimeout(refreshFree, 500); return; }
  const cam = viewport.camera, eye = cam.eye().map(Math.round), target = cam.target.map(Math.round), seq = ++freeSeq;
  real.busy = true;
  const { label } = real.chip;
  label.classList.add("busy");
  try {
    const key = hash(JSON.stringify(currentEdits()));
    if (!real.key || (force && real.key !== key)) {
      store.real.freeBtn.textContent = "Starting the game…";
      const out = await api.previewPrepare(currentEdits());
      Object.assign(real, { key, room: out.room, urls: {} });
    }
    store.real.freeBtn.textContent = "Taking the picture…";
    const hops = hopsTo(real.room, r.id);
    if (!hops) throw new Error("no chain of doors leads there from where the game is");
    const sp = r.spawns[0], at = sp ? [sp.pos[0], sp.pos[2], 0] : [eye[0], eye[2], 0];
    const out = await api.previewShot(r.id, hops, at, "f" + Date.now() % 1e7, { eye, target, fov: cam.photo ? cam.photo.fov : 60 });
    real.room = r.id;
    const same = [...cam.eye()].every((v, i) => Math.abs(v - eye[i]) < 2) && cam.target.every((v, i) => Math.abs(v - target[i]) < 2);
    if (seq === freeSeq && store.mode === "3d" && same) {
      viewport.setShot(out.url, real.key !== hash(JSON.stringify(currentEdits())));
      store.real.freeUpdate.style.display = real.key !== hash(JSON.stringify(currentEdits())) ? "" : "none";
    }
  } catch (e) {
    toast("The game's picture didn't work", "err", e.message, 7000);
    store.real.free = false; store.real.freeBtn.classList.remove("on"); viewport.usePhoto(false);
  } finally {
    real.busy = false; label.classList.remove("busy");
    store.real.freeBtn.innerHTML = icon("play", 14) + " Game's picture";
  }
}

// the game's pictures of every room are taken in the background the first time, and kept (work/studio/survey)
async function watchSurvey() {
  const chip = document.getElementById("loadchip");
  try {
    let s = await api.surveyStart();
    store.survey.supported = !!s.supported;
    if (!s.supported) return;
    store.real.on = true;
    store.real.free = true;
    if (store.real.freeBtn) store.real.freeBtn.classList.add("on");
    const realBtn = [...document.querySelectorAll("#camnav .btn")].find((b) => b.textContent.includes("Real view"));
    if (realBtn) realBtn.classList.add("on");
    let first = true;
    for (;;) {
      store.survey.have = new Set(s.have || []);
      if (first) { first = false; freeChanged(true); }
      if (chip && s.running) { chip.style.display = "inline-flex"; chip.textContent = `Taking the game's pictures${s.here ? "" : " of " + s.where}: ${s.done}/${s.total}${s.current ? " (" + s.current + ")" : ""}`; }
      else if (chip && !s.running) chip.style.display = "none";
      if (store.mode === "game") refreshReal();
      if (!s.running) break;
      await new Promise((r) => setTimeout(r, 6000));
      s = await api.survey();
    }
    if (s.error) toast("The game's pictures stopped", "err", s.error, 8000);
  } catch (e) { /* not supported: Studio's own drawing stays */ }
}

const syncRail = () => {
  document.querySelectorAll("#toolbar [data-tool]").forEach((b) => b.classList.toggle("on", b.dataset.tool === store.tool));
  const adding = ADD.some((a) => a.id === store.tool);
  const ab = document.getElementById("addbtn");
  if (ab) ab.classList.toggle("on", adding);
};

// ---- the view menu (top right of the picture) and the small notes over it
function buildViewMenu() {
  const host = document.getElementById("viewmenu");
  host.innerHTML = "";
  const btn = el("button", { class: "btn line", html: icon("eye", 16) + " View" });
  const p = pop("down right");
  const rows = [["walls", "Walls and floors (what you stand on)"], ["areas", "Doors and exits"], ["triggers", "Triggers (examine spots, locks…)"], ["items", "Items"], ["enemies", "Enemies"], ["cameras", "Cameras"], ["lights", "Lights"], ["zones", "Sound zones"], ["nodes", "AI paths"], ["labels", "Names on things"], ["grid", "Grid"]];
  for (const [k, label] of rows) {
    const row = el("label", { class: "check" }, el("input", { type: "checkbox", checked: store.show[k] }), label);
    row.firstChild.addEventListener("change", () => { store.show[k] = row.firstChild.checked; viewport.redraw(); });
    p.append(row);
  }
  p.append(el("hr"));
  const rm = el("label", { class: "check" }, el("input", { type: "checkbox", checked: store.show.models }), "Draw the rooms");
  rm.firstChild.addEventListener("change", () => { store.show.models = rm.firstChild.checked; viewport.redraw(); });
  const tx = el("label", { class: "check" }, el("input", { type: "checkbox", checked: store.show.textures }), "Textures (in 3D)");
  tx.firstChild.addEventListener("change", () => { store.show.textures = tx.firstChild.checked; viewport.redraw(); });
  const snap = el("select", { class: "sel", style: "margin:6px 10px 6px;width:calc(100% - 20px)", title: "Things jump to this grid when moved" });
  for (const v of [0, 1, 5, 10, 25, 50, 100]) snap.append(el("option", { value: v, selected: v === store.snap }, v ? `Snap to ${v}` : "No snapping"));
  snap.addEventListener("change", () => { store.snap = +snap.value; });
  p.append(rm, tx, snap);
  btn.addEventListener("click", (e) => { e.stopPropagation(); p.classList.toggle("on"); });
  host.append(btn, p); closeOnOutside(host, p);
}

let bannerSeen = false;
function updateHint() {
  const b = document.getElementById("banner");
  if (!b) return;
  const a = ADD.find((x) => x.id === store.tool);
  const t = [...PLACE, ...THINGS, WALL, FLOOR].find((x) => x.id === store.tool);
  if (a && t) { b.innerHTML = `<b>${a.label}</b> — ${t.hint} <kbd>Esc</kbd> to stop.`; b.classList.add("on"); }
  else if (store.tool === "library") { const p = store.libraryPick; b.innerHTML = p ? `<b>Placing ${p.name}</b> — click to put it down, again for more, <kbd>R</kbd> turns it. <kbd>Esc</kbd> to stop.` : "Pick an object in the Library, then click in the room."; b.classList.add("on"); }
  else if (store.tool === "scenery") { b.innerHTML = "Click an object to pick it: drag to move it, or turn and resize it in the panel. <kbd>Esc</kbd> to go back."; b.classList.add("on"); }
  else if (store.tool === "pan") { b.innerHTML = "Drag to move the view. <kbd>Esc</kbd> to go back."; b.classList.add("on"); }
  else if (!bannerSeen && !localStorage.getItem("studio-hint")) { b.innerHTML = "Click anything to edit it. Use <b>Add</b> to put new things down. Scroll to zoom. <button class='link' id='gotit' style='margin-left:8px'>Got it</button>"; b.classList.add("on"); const g = b.querySelector("#gotit"); if (g) g.onclick = () => { localStorage.setItem("studio-hint", "1"); bannerSeen = true; b.classList.remove("on"); }; }
  else b.classList.remove("on");
}

// arrow keys move what is selected along the room's own axes (x across, z along; Alt or PageUp and PageDown for up and down), a grid step at a time, ten with Shift
function nudge(e) {
  const step = (store.snap > 1 ? store.snap : 10) * (e.shiftKey ? 10 : 1);
  let dx = 0, dy = 0, dz = 0;
  if (e.key === "ArrowLeft") dx = -step; else if (e.key === "ArrowRight") dx = step;
  else if (e.key === "ArrowUp") { if (e.altKey) dy = step; else dz = -step; } else if (e.key === "ArrowDown") { if (e.altKey) dy = -step; else dz = step; }
  else if (e.key === "PageUp") dy = step; else if (e.key === "PageDown") dy = -step;
  for (const ref of store.selection) { const o = objectOf(ref); if (o) scene.translate(ref.c, o, dx, dy, dz); }
  emit("live"); commit("Nudge");
}

function turnSelection(degrees) {
  for (const ref of store.selection) {
    const o = objectOf(ref);
    if (!o) continue;
    if (ref.c === "scenery") setPose(o, { rot: [o.rot[0], o.rot[1] + degrees * Math.PI / 180, o.rot[2]] });
    else o.facing = (o.facing + Math.round(degrees / 360 * 65536) + 65536) % 65536;
  }
  emit("live"); commit("Turn");
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
  const probs = validate();
  const p = document.getElementById("problems");
  if (p) {
    p.style.display = probs.length ? "" : "none";
    p.textContent = `${probs.length} problem${probs.length > 1 ? "s" : ""}`;
    p.style.color = probs.some((x) => x.level === "bad") ? "var(--bad)" : "var(--warn)";
    p.onclick = () => toast(`${probs.length} problem${probs.length > 1 ? "s" : ""}`, "err", probs.slice(0, 6).map((x) => x.text).join("\n"), 9000);
  }
  const u = document.getElementById("undo"), rd = document.getElementById("redo");
  if (u) { u.disabled = !canUndo(); rd.disabled = !canRedo(); }
}

function setState(text, cls = "") {
  const t = document.getElementById("statetext");
  if (t) t.textContent = text;
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
      hops = hopsTo(scene.startRoomId() ?? info.start_room, room.id);
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
    { label: "Build view", hint: "1", run: () => setMode("3d") }, { label: "Camera views", hint: "2", run: () => setMode("game") }, { label: "Plan view", hint: "3", run: () => setMode("plan") },
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
    else if (meta && (e.key === "d" || e.key === "D") && store.selection.length === 1 && store.selection[0].c === "scenery") {
      e.preventDefault();
      const s = store.selection[0], room = roomOf(s.file), o = objectOf(s);
      if (room && o) { const c = duplicate(room, o); commit("Copy object"); select([{ c: "scenery", file: s.file, i: c.i }]); }
    }
    else if (meta) return;
    else if (viewport.flyKey(e, true)) { e.preventDefault(); }
    else if (store.selection.length && (e.key.startsWith("Arrow") || e.key === "PageUp" || e.key === "PageDown")) { e.preventDefault(); nudge(e); }
    else if (store.tool === "library" && (e.key === "r" || e.key === "R")) { e.preventDefault(); store.libraryRot = ((store.libraryRot || 0) + (e.shiftKey ? -1 : 1) * Math.PI / 12) % (2 * Math.PI); viewport.redraw(); }
    else if (store.selection.length && (e.key === "r" || e.key === "R") && store.selection.every((s) => s.c === "scenery" || s.c === "enemies" || s.c === "spawns")) { e.preventDefault(); turnSelection(e.shiftKey ? -15 : 15); }
    else if (store.selection.length === 1 && store.selection[0].c === "scenery" && (e.key === "=" || e.key === "-")) { e.preventDefault(); const o = objectOf(store.selection[0]); if (o) { const k = e.key === "=" ? 1.1 : 1 / 1.1; setPose(o, { scale: o.scale.map((x) => x * k) }); emit("live"); commit("Resize object"); } }
    else if (e.key === " ") { viewport.space = true; viewport.ov.style.cursor = "grab"; e.preventDefault(); }
    else if (e.key === "Escape") { select([]); setTool("select"); closeLibrary(); }
    else if ((e.key === "b" || e.key === "B") && !e.metaKey && !e.ctrlKey) { e.preventDefault(); toggleLibrary(viewport); }
    else if (e.key === "Delete" || e.key === "Backspace") { const s = store.selection[0]; if (s && store.selection.length === 1) { const o = objectOf(s); if (o && s.c === "enemies" && !o.isNew) { o.modes = 0; select([]); commit("Remove enemy"); } else if (o && s.c === "scenery" && o.isNew) { removeCopy(roomOf(s.file), o); select([]); commit("Delete object"); } else if (o && (o.isNew || s.c === "walls" || s.c === "floors")) { const r = roomOf(s.file); r[s.c].splice(r[s.c].findIndex((x) => x.i === s.i), 1); select([]); commit("Delete"); } else toast("Only things you added, walls and enemies can be deleted", "err"); } }
    else if (e.key === "1") setMode("3d");
    else if (e.key === "2") setMode("game");
    else if (e.key === "3") setMode("plan");
    else if (e.key === "Tab") { e.preventDefault(); setMode({ "3d": "game", game: "plan", plan: "3d" }[store.mode]); }
    else if (store.mode === "game" && (e.key === "," || e.key === ".")) viewport.stepCamera(e.key === "." ? 1 : -1);
    else if (e.key.toLowerCase() === "f") { const s = store.selection[0]; s ? viewport.focus(s) : viewport.overview(store.room); }
    else if (e.key.toLowerCase() === "g") { store.show.grid = !store.show.grid; viewport.redraw(); }
    else if (e.key === "[") togglePanel("no-side");
    else if (e.key === "]") togglePanel("no-inspector");
    else {
      const all = [{ id: "select", key: "V" }, { id: "scenery", key: "O" }, { id: "pan", key: "H" }, ...ADD];
      const t = all.find((x) => x.key === e.key.toUpperCase());
      if (t) setTool(t.id);
    }
  });
  window.addEventListener("keyup", (e) => { if (e.key === " ") { viewport.space = false; viewport.updateCursor(); } viewport.flyKey(e, false); });
  window.addEventListener("blur", () => { if (viewport.keys) viewport.keys.clear(); });
}

// ---- start
async function boot() {
  const msg = document.getElementById("loadmsg");
  try {
    const [data, saved] = await Promise.all([api.scene(), api.project()]);
    msg.textContent = "Setting up the editor…";
    loadScene(data, saved);
    api.messages().then((m) => { store.messages = m; renderInspector(); }).catch(() => {});
    buildTopbar(); buildToolbar(); buildViewMenu();
    viewport = new Viewport();
    window.studio = { store, viewport, scene };      // for the console
    renderOutline(); renderInspector(); updateHint();
    on("tool", () => { syncRail(); updateHint(); });
    on("mode", () => { document.querySelectorAll("#viewseg button").forEach((b) => b.classList.toggle("on", b.dataset.mode === store.mode)); });
    on("room", updateStatus);
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
    viewport.applyMode();
    buildCamNav();
    watchSurvey();
    document.getElementById("loading").classList.add("gone");
    await viewport.loadModels(progress);
    viewport.redraw();
  } catch (e) {
    msg.textContent = "Couldn't start: " + e.message;
    console.error(e);
  }
}

boot();

export const currentViewport = () => viewport;
