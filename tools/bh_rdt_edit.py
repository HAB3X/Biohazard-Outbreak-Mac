#!/usr/bin/env python3
"""The room editor: the room map (tools/bh_rdt_map.py) with tools to move a scenario's cameras and trigger areas, saved as a disc patch.

    bh_rdt_edit.py GAME SCENARIO [--names ROOMS.md] [--port 8766] [--out DIR]

Starts a page on http://127.0.0.1:PORT (this Mac only) and leaves it running; Ctrl+C stops it. In the page:
  click a shape to select it (an area, a camera's eye, or a camera's switch box); drag its handles: an area's corners or its body,
  a camera's eye (square) and aim (dot), a switch box's corners; the boxes in the panel take exact numbers (height and the
  camera's y are only there); "Snap" rounds to a step; "Undo" and "Revert" step back; "Save patch" writes the edits and the patch.
Saved to DIR (default work/): room-edits-GAME.json (what was changed, by room file, camera and area number: tools/bh_room_patch.py
documents it) and rooms-GAME.patch. `./run.sh GAME` and the launcher's app put rooms-GAME.patch (the settings folder's
rooms.patch in the app) over the disc as well when it exists; delete it to play the original rooms.
What an edit can do is what the file allows: numbers are overwritten where they are, so a room's size never changes. The arrival
spots the map draws for doors and exits are from the unedited rooms until the page is reloaded.
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

import bh_rdt_map as maps
import bh_room_patch as roompatch
from bh_patchfile import write_patch

ROOT = Path(__file__).resolve().parent.parent

EDITOR = r"""
<script>
const EDIT = true;
const ORIG = JSON.parse(JSON.stringify(DATA));
const room = (file) => DATA.find(r => r.file === file);
let selected = null, undo = [], dragging = null, snapStep = 10, savedMessage = "";
const snap = v => snapStep > 1 ? Math.round(v / snapStep) * snapStep : Math.round(v);
const clone = o => JSON.parse(JSON.stringify(o));
const panel = document.createElement("div"); panel.id = "edit"; document.getElementById("side").appendChild(panel);
const style = document.createElement("style");
style.textContent = "#edit input[type=number]{width:62px;background:#222;color:#ddd;border:1px solid #444;padding:1px 3px}#edit button{margin:2px 2px 2px 0;padding:3px 8px;background:#2d3a4a;color:#ddd;border:1px solid #567;border-radius:4px;cursor:pointer}#edit .row{margin:2px 0}#edit .msg{color:#9c9;white-space:pre-wrap;margin-top:6px}#edit b{color:#fc6}";
document.head.appendChild(style);
function itemOf(sel) { return sel.kind === "area" ? sel.room.areas.find(a => a.i === sel.index) : sel.room.cameras.find(c => c.i === sel.index); }
function origOf(sel) { const r = ORIG.find(r => r.file === sel.room.file); return sel.kind === "area" ? r.areas.find(a => a.i === sel.index) : r.cameras.find(c => c.i === sel.index); }
function changed(a, b) { return JSON.stringify(a) !== JSON.stringify(b); }
function diffs() {
  const edits = {};
  DATA.forEach((r, ri) => {
    const o = ORIG[ri], rooms = {};
    for (const c of r.cameras) {
      const oc = o.cameras.find(x => x.i === c.i), d = {};
      if (changed(c.eye, oc.eye)) d.eye = c.eye;
      if (changed(c.target, oc.target)) d.target = c.target;
      if (changed(c.fov, oc.fov)) d.fov = c.fov;
      if (changed(c.box, oc.box)) d.box = c.box;
      if (Object.keys(d).length) (rooms.cameras = rooms.cameras || {})[c.i] = d;
      if (c.fog && oc.fog) { const f = {}; c.fog.forEach((v, k) => { if (v !== oc.fog[k]) f[k] = v; }); if (Object.keys(f).length) (rooms.fog = rooms.fog || {})[c.i] = {ints: f}; }
    }
    for (const a of r.areas) {
      const oa = o.areas.find(x => x.i === a.i), d = {};
      if (changed(a.quad, oa.quad)) d.quad = a.quad;
      if (changed(a.h, oa.h)) d.height = a.h;
      const f = {};
      a.fields.forEach((v, k) => { if (v !== oa.fields[k]) f[12 + k] = v; });
      if (Object.keys(f).length) d.fields = f;
      if (Object.keys(d).length) (rooms.areas = rooms.areas || {})[a.i] = d;
    }
    if (rooms.cameras || rooms.areas || rooms.fog) edits[r.file] = rooms;
  });
  return {rooms: edits};
}
function apply(edits) {
  for (const [file, spec] of Object.entries(edits.rooms || {})) {
    const r = room(file); if (!r) continue;
    for (const [i, d] of Object.entries(spec.cameras || {})) { const c = r.cameras.find(x => x.i === +i); if (c) Object.assign(c, d); }
    for (const [i, d] of Object.entries(spec.fog || {})) { const c = r.cameras.find(x => x.i === +i); if (c && c.fog) for (const [k, v] of Object.entries(d.ints || {})) c.fog[+k] = v; }
    for (const [i, d] of Object.entries(spec.areas || {})) {
      const a = r.areas.find(x => x.i === +i); if (!a) continue;
      if (d.quad) a.quad = d.quad; if (d.height) a.h = d.height;
      for (const [k, v] of Object.entries(d.fields || {})) a.fields[+k - 12] = v;
    }
  }
}
const f2i = x => { const b = new DataView(new ArrayBuffer(4)); b.setFloat32(0, x, true); return b.getInt32(0, true); };
const i2f = x => { const b = new DataView(new ArrayBuffer(4)); b.setInt32(0, x, true); return b.getFloat32(0, true); };
const hex = v => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0");
function colourRow(label, value, set) {
  const row = document.createElement("div"); row.className = "row"; row.append(label + " ");
  const input = document.createElement("input"); input.type = "color"; input.value = value;
  input.addEventListener("change", () => { remember(); set(input.value); draw(); refreshPanel(); });
  row.appendChild(input); return row;
}
function remember() { if (selected) undo.push({sel: selected, value: clone(itemOf(selected))}); if (undo.length > 200) undo.shift(); }
function pointerWorld(e) { const [mx, my] = toCanvas(e); return [(mx - cv.width / 2) / view.s + view.x, -(my - cv.height / 2) / view.s + view.y]; }
function handles() {
  if (!selected) return [];
  const it = itemOf(selected), out = [];
  if (selected.kind === "area") it.quad.forEach((p, k) => out.push({k: "corner", n: k, x: p[0], z: p[1]}));
  else {
    out.push({k: "eye", x: it.eye[0], z: it.eye[2]}, {k: "aim", x: it.target[0], z: it.target[2]});
    it.box.slice(0, 4).forEach((p, k) => out.push({k: "boxcorner", n: k, x: p[0], z: p[2]}));
  }
  return out;
}
const baseDraw = draw;
draw = function () {
  baseDraw();
  for (const h of handles()) {
    const x = sx(h.x), y = sy(h.z);
    ctx.strokeStyle = "#fff"; ctx.fillStyle = h.k === "aim" ? "#ff0" : "#08f"; ctx.lineWidth = 2;
    ctx.beginPath(); if (h.k === "eye") ctx.rect(x - 6, y - 6, 12, 12); else ctx.arc(x, y, 6, 0, 7); ctx.fill(); ctx.stroke();
  }
  ctx.lineWidth = 1;
  if (selected && selected.kind === "area") {
    const it = itemOf(selected); ctx.strokeStyle = "#fff"; ctx.lineWidth = 2; ctx.beginPath();
    it.quad.forEach((p, i) => i ? ctx.lineTo(sx(p[0]), sy(p[1])) : ctx.moveTo(sx(p[0]), sy(p[1]))); ctx.closePath(); ctx.stroke(); ctx.lineWidth = 1;
  }
};
function hitHandle(e) {
  const [mx, my] = toCanvas(e);
  return handles().find(h => Math.hypot(sx(h.x) - mx, sy(h.z) - my) < 10 * devicePixelRatio);
}
cv.addEventListener("mousedown", e => {
  const h = hitHandle(e);
  if (h) { remember(); dragging = {h, last: pointerWorld(e)}; e.stopImmediatePropagation(); e.preventDefault(); return; }
  const [mx, my] = toCanvas(e), s = pick(mx, my);
  if (s && (s.kind === "area" || s.kind === "cam" || s.kind === "box")) {
    const sel = {kind: s.kind === "area" ? "area" : "cam", room: s.extra.room, index: s.kind === "area" ? s.extra.area.i : s.extra.cam.i};
    const same = selected && selected.kind === sel.kind && selected.room === sel.room && selected.index === sel.index;
    selected = sel; refreshPanel(); draw();
    if (sel.kind === "area" && same || sel.kind === "area") { remember(); dragging = {body: true, last: pointerWorld(e)}; e.stopImmediatePropagation(); e.preventDefault(); }
    return;
  }
  selected = null; refreshPanel(); draw();
}, true);
addEventListener("mousemove", e => {
  if (!dragging) return;
  const w = pointerWorld(e), it = itemOf(selected);
  if (dragging.body) {
    const dx = snap(w[0]) - snap(dragging.last[0]), dz = snap(w[1]) - snap(dragging.last[1]);
    if (dx || dz) { it.quad = it.quad.map(p => [p[0] + dx, p[1] + dz]); dragging.last = w; }
  } else {
    const h = dragging.h, x = snap(w[0]), z = snap(w[1]);
    if (h.k === "corner") it.quad[h.n] = [x, z];
    else if (h.k === "eye") { it.eye = [x, it.eye[1], z]; }
    else if (h.k === "aim") { it.target = [x, it.target[1], z]; }
    else if (h.k === "boxcorner") { it.box[h.n] = [x, it.box[h.n][1], z]; it.box[h.n + 4] = [x, it.box[h.n + 4][1], z]; }
    h.x = x; h.z = z;
  }
  refreshPanel(); draw();
});
addEventListener("mouseup", () => { if (dragging) { dragging = null; refreshPanel(); } });
function num(label, value, set) {
  const row = document.createElement("span"); row.innerHTML = label + " ";
  const input = document.createElement("input"); input.type = "number"; input.value = value; row.appendChild(input);
  input.addEventListener("change", () => { remember(); set(+input.value); draw(); refreshPanel(); });
  row.append(" "); return row;
}
function refreshPanel() {
  panel.innerHTML = "<hr><b>Editor</b><div class='row'><label><input type='checkbox' id='snapbox' " + (snapStep > 1 ? "checked" : "") + "> snap to 10</label></div>";
  panel.querySelector("#snapbox").onchange = e => { snapStep = e.target.checked ? 10 : 1; };
  const total = diffs(), n = Object.values(total.rooms).reduce((a, r) => a + Object.keys(r.cameras || {}).length + Object.keys(r.areas || {}).length, 0);
  const info = document.createElement("div"); info.className = "row";
  if (selected) {
    const it = itemOf(selected), r = selected.room;
    info.innerHTML = selected.kind === "area" ? "room " + r.id + " area " + it.i + " (tag " + it.tag + ", subtype " + it.sub + ")" : "room " + r.id + " camera " + it.i + " (type " + it.kind + ")";
    panel.appendChild(info);
    const rows = [];
    if (selected.kind === "area") {
      it.quad.forEach((p, k) => { const d = document.createElement("div"); d.className = "row"; d.append("corner " + k + " ", num("x", p[0], v => it.quad[k] = [v, it.quad[k][1]]), num("z", p[1], v => it.quad[k] = [it.quad[k][0], v])); panel.appendChild(d); });
      const d = document.createElement("div"); d.className = "row"; d.append("height ", num("from", it.h[0], v => it.h = [v, it.h[1]]), num("to", it.h[1], v => it.h = [it.h[0], v])); panel.appendChild(d);
      const f = document.createElement("div"); f.className = "row"; f.append("fields (int 12 on): ");
      it.fields.forEach((v, k) => { if (v !== 0 || ORIG.find(o => o.file === r.file).areas.find(a => a.i === it.i).fields[k] !== 0) f.appendChild(num(String(12 + k), v, x => it.fields[k] = x)); });
      panel.appendChild(f);
    } else {
      const e = document.createElement("div"); e.className = "row"; e.append("eye ", num("x", it.eye[0], v => it.eye = [v, it.eye[1], it.eye[2]]), num("y", it.eye[1], v => it.eye = [it.eye[0], v, it.eye[2]]), num("z", it.eye[2], v => it.eye = [it.eye[0], it.eye[1], v])); panel.appendChild(e);
      const t = document.createElement("div"); t.className = "row"; t.append("aim ", num("x", it.target[0], v => it.target = [v, it.target[1], it.target[2]]), num("y", it.target[1], v => it.target = [it.target[0], v, it.target[2]]), num("z", it.target[2], v => it.target = [it.target[0], it.target[1], v])); panel.appendChild(t);
      const g = document.createElement("div"); g.className = "row"; g.append("fov ", num("h", it.fov[0], v => it.fov = [v, it.fov[1]]), num("v", it.fov[1], v => it.fov = [it.fov[0], v])); panel.appendChild(g);
      if (it.fog) {
        const env = it.fog[4] >>> 0, tint = [9, 10, 11].map(k => i2f(it.fog[k]));
        panel.appendChild(colourRow("scene colour", "#" + hex((env >> 16) & 255) + hex((env >> 8) & 255) + hex(env & 255), v => {
          const r = parseInt(v.slice(1, 3), 16), g2 = parseInt(v.slice(3, 5), 16), b = parseInt(v.slice(5, 7), 16); it.fog[4] = ((env & 0xff000000) | (r << 16) | (g2 << 8) | b) | 0; }));
        const amb = it.fog[3] >>> 0;
        panel.appendChild(colourRow("ambient colour", "#" + hex((amb >> 16) & 255) + hex((amb >> 8) & 255) + hex(amb & 255), v => {
          const r = parseInt(v.slice(1, 3), 16), g2 = parseInt(v.slice(3, 5), 16), b = parseInt(v.slice(5, 7), 16); it.fog[3] = ((amb & 0xff000000) | (r << 16) | (g2 << 8) | b) | 0; }));
        const dist = document.createElement("div"); dist.className = "row"; dist.append("fog distance ", num("", Math.round(i2f(it.fog[2])), v => it.fog[2] = f2i(v))); panel.appendChild(dist);
        panel.appendChild(colourRow("character tint", "#" + hex(tint[0] * 255) + hex(tint[1] * 255) + hex(tint[2] * 255), v => {
          [1, 3, 5].forEach((p, n) => it.fog[9 + n] = f2i(parseInt(v.slice(p, p + 2), 16) / 255)); }));
      }
    }
    const revert = document.createElement("button"); revert.textContent = "Revert this";
    revert.onclick = () => { remember(); const o = clone(origOf(selected)); const target = itemOf(selected); Object.keys(o).forEach(k => target[k] = o[k]); refreshPanel(); draw(); };
    panel.appendChild(revert);
  } else { info.textContent = "click a shape"; panel.appendChild(info); }
  const undoB = document.createElement("button"); undoB.textContent = "Undo (" + undo.length + ")";
  undoB.onclick = () => { const u = undo.pop(); if (!u) return; const target = itemOf(u.sel); Object.keys(u.value).forEach(k => target[k] = u.value[k]); refreshPanel(); draw(); };
  const save = document.createElement("button"); save.textContent = "Save patch (" + n + " changed)";
  save.onclick = async () => {
    const res = await fetch("/save", {method: "POST", body: JSON.stringify(diffs())}); const body = await res.json();
    savedMessage = res.ok ? "saved: " + body.patch + "\n" + body.report.join("\n") : "not saved: " + body.error; refreshPanel();
  };
  panel.append(document.createElement("br"), undoB, save);
  const msg = document.createElement("div"); msg.className = "msg"; msg.textContent = savedMessage; panel.appendChild(msg);
}
fetch("/edits").then(r => r.json()).then(e => { apply(e); refreshPanel(); draw(); });
refreshPanel();
</script>
"""


def serve(game, scenario, names, port, out):
    data = maps.build(game, scenario, names, False)
    page = maps.render(game, scenario, data, EDITOR)
    out.mkdir(parents=True, exist_ok=True)
    edits_path, patch_path = out / f"room-edits-{game}.json", out / f"rooms-{game}.patch"
    mine = {r["file"].lower() for r in data}

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def send(self, code, body, kind="application/json"):
            payload = body if isinstance(body, bytes) else body.encode()
            self.send_response(code)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def do_GET(self):
            if self.path == "/":
                self.send(200, page, "text/html; charset=utf-8")
            elif self.path == "/edits":
                self.send(200, edits_path.read_text() if edits_path.exists() else '{"rooms": {}}')
            else:
                self.send(404, "{}")

        def do_POST(self):
            if self.path != "/save":
                self.send(404, "{}")
                return
            try:
                mine_now = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
                kept = {}
                if edits_path.exists():
                    kept = {k: v for k, v in json.loads(edits_path.read_text()).get("rooms", {}).items() if k.lower() not in mine}
                edits = {"rooms": {**kept, **mine_now.get("rooms", {})}}
                patches, report = roompatch.build(game, edits)
                edits_path.write_text(json.dumps(edits, indent=1))
                write_patch(str(patch_path), patches)
                self.send(200, json.dumps({"patch": str(patch_path), "report": report or ["no changes: the patch is empty"]}))
            except (SystemExit, Exception) as error:  # a bad edit is told to the page, not a crash
                self.send(400, json.dumps({"error": str(error)}))

    server = HTTPServer(("127.0.0.1", port), Handler)
    print(f"the room editor for {game} r{scenario:03d}: http://127.0.0.1:{port}/   (edits in {out}; Ctrl+C stops)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    options = argv[1:]
    names, port, out = {}, 8766, ROOT / "work"
    for flag in ("--names", "--port", "--out"):
        if flag in options:
            value = options[options.index(flag) + 1]
            args.remove(value)
            if flag == "--names":
                names = maps.room_names(value)
            elif flag == "--port":
                port = int(value)
            else:
                out = Path(value)
    if len(args) != 2:
        print(__doc__)
        return 1
    serve(args[0], int(args[1]), names, port, out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
