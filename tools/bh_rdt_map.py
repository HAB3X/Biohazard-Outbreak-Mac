#!/usr/bin/env python3
"""A top-down map of a scenario's rooms (a self-contained HTML page): every room of one r0xx.afs in the shared world coordinates,
with its cameras (the eye and where it looks, and the box that switches to it), its doors, exits and the other trigger areas, and
the links between rooms.

    bh_rdt_map.py GAME SCENARIO [OUT.html] [--names ROOMS.md] [--variants]
    bh_rdt_map.py GAME all [--names ROOMS.md]     every scenario of the disc, and work/rdt-maps-GAME.html linking them

SCENARIO is the archive's number (40 for r040.afs: Wild Things). OUT defaults to work/rdt-map-GAME-rSCENARIO.html (work/ is not
in the repository: the page holds the game's room numbers). Only each room's variant 00 is drawn unless --variants.
--names takes the community wiki's rooms.md (Outbreak-Research/Documentation, `docs/game data/rooms.md`) for the rooms' names.

The page: the wheel zooms, a drag pans, hovering a shape lists its fields, a click on a room in the list centres it, the boxes at the
top switch layers. Rooms are placed by the coordinates in their own files: the rooms of a scenario share one world, so they sit
where the game puts them (the map doesn't need the room models).
"""
import json
import math
import re
import struct
import sys
from pathlib import Path

import base64
import io

import bh_archive as archive
import bh_items as items_module
import bh_nbd as nbd
import bh_rdt as rdt

ROOT = Path(__file__).resolve().parent.parent


def room_names(path):
    """{room file name in lower case: name} from the community wiki's rooms.md tables"""
    names = {}
    for line in Path(path).read_text(errors="replace").splitlines():
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) >= 5 and cells[3].lower().endswith(".rdt"):
            names[cells[3].lower()] = cells[4]
    return names


def scenario_sgl(game, scenario):
    """The scenario's .sgl as an rdt.Sgl, or None"""
    disc = archive.Archive(game)
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            pack = disc.open(f"{dat}/r{scenario:03d}.afs")
        except FileNotFoundError:
            continue
        for name, node in pack.members():
            if name.lower().endswith(".sgl"):
                return rdt.Sgl(node.read())
    return None


def build(game, scenario, names, variants):
    wanted = f"r{scenario:03d}"
    members = [(m, node) for pack, m, node in rdt.rooms(game) if pack.lower() == f"{wanted}.afs"]
    rooms = {}
    for member, node in members:
        match = re.fullmatch(r"r\d{3}(\d\d)(\d\d)\.rdt", member.lower())
        if not match:
            continue
        number, variant = int(match.group(1)), int(match.group(2))
        if variant and not variants:
            continue
        rooms[(number, variant)] = (member, rdt.Room(node.read()))
    if not rooms:
        raise SystemExit(f"{game}: no rooms in {wanted}.afs")
    out = []
    sgl = scenario_sgl(game, scenario)
    try:
        scenario_items = items_module.Items(game, scenario)
    except FileNotFoundError:
        scenario_items = None
    for (number, variant), (member, room) in sorted(rooms.items()):
        entry = {"id": number, "variant": variant, "file": member, "name": names.get(member.lower(), ""), "capacity": room.capacity(), "cameras": [], "areas": []}
        for index, cam in enumerate(room.cameras):
            try:
                fog = list(room.fog(index))
            except (IndexError, struct.error):
                fog = None
            entry["cameras"].append({"i": index, "kind": cam.kind, "fov": list(cam.fov), "eye": list(cam.eye), "target": list(cam.target),
                                     "box": [list(p) for p in cam.box], "fog": fog})
        for index, area in enumerate(room.areas):
            item = {"i": index, "tag": area.tag, "sub": area.subtype, "quad": [list(p) for p in area.quad], "h": list(area.height),
                    "fields": list(area.fields)}
            if area.leads_to is not None:
                item["to"] = area.leads_to
                dest = rooms.get((area.leads_to, 0))
                spot = area.arrival(dest[1]) if dest else None
                if spot:
                    item["arrival"] = [spot[0], spot[1], spot[2]]
                if area.tag == 0:
                    item["door"] = area.door_number
                    item["other"] = area.other_door
                    item["facing"] = area.facing
            entry["areas"].append(item)
        entry["script"] = room.script_words()
        entry["nodes"] = [{"i": i, "kind": n.kind, "id": n.id, "pos": [round(v, 1) for v in n.position], "words": list(n.words)}
                          for i, n in enumerate(sgl.nodes(number | (variant << 16)) if sgl else [])]
        entry["items"] = [{"i": n.number, "kind": next(iter(n.kinds.values()), (0, 0))[0], "qty": next(iter(n.kinds.values()), (0, 0))[1],
                           "pos": [round(v, 1) for v in n.position]} for n in (scenario_items.items if scenario_items and not variant else []) if n.room == number]
        entry["lights"] = [{"i": i, "pos": [round(v, 1) for v in l.position], "att": l.attenuation, "range": l.range, "col": [round(v, 4) for v in l.colour],
                            "col2": [round(v, 4) for v in l.colour2]} for i, l in enumerate(room.lights)]
        entry["zones"] = [{"i": i, "sound": z.sound, "box": [list(p) for p in z.box]} for i, z in enumerate(room.sound_zones)]
        entry["points"] = [{"i": i, "kind": p.kind, "pos": [round(v, 1) for v in p.position], "rest": list(p.rest)} for i, p in enumerate(room.sound_points)]
        out.append(entry)
    return out


MODEL_SCALE = 10  # world units a pixel of the models' picture


def model_picture(game, scenario, numbers, bounds):
    """The rooms' models seen from above as one picture of the scenario's world: (PNG data URL, min x, max z, width, height in
    pixels): only the triangles inside `bounds` (x0, z0, x1, z1: the rooms' own areas and cameras plus a margin: the skies and far
    backdrops are huge), each triangle drawn by its height (higher is brighter, the higher drawn last). None without Pillow or models."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("no Pillow: the map is drawn without the rooms' models", file=sys.stderr)
        return None
    disc = archive.Archive(game)
    pack = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            pack = disc.open(f"{dat}/r{scenario:03d}.afs")
            break
        except FileNotFoundError:
            continue
    if pack is None:
        return None
    wanted = {f"r{scenario:03d}{number:02d}00.nbd" for number in numbers}
    x0, z0, x1, z1 = bounds
    floors, walls = [], []
    for name, node in pack.members():
        if name.lower() in wanted:
            f, w = nbd.floors_and_walls(nbd.room_triangles(node.read()))
            floors += f
            walls += w
    inside = lambda t: all(x0 <= p[0] <= x1 and z0 <= p[2] <= z1 for p in t)
    floors, walls = [t for t in floors if inside(t)], [t for t in walls if inside(t)]
    if not floors and not walls:
        return None
    every = floors + walls
    low, high = min(p[0] for t in every for p in t), max(p[0] for t in every for p in t)
    near, far = min(p[2] for t in every for p in t), max(p[2] for t in every for p in t)
    width, height = int((high - low) / MODEL_SCALE) + 1, int((far - near) / MODEL_SCALE) + 1
    if width * height > 40_000_000:
        return None
    ys = sorted(p[1] for t in floors for p in t) or [0.0]
    base, top = ys[len(ys) // 50], ys[-len(ys) // 50 - 1]
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    plan = lambda t: [((p[0] - low) / MODEL_SCALE, (far - p[2]) / MODEL_SCALE) for p in t]
    for t in sorted(floors, key=lambda t: sum(p[1] for p in t)):
        level = (sum(p[1] for p in t) / 3 - base) / max(1.0, top - base)
        shade = int(60 + 90 * min(1.0, max(0.0, level)))
        draw.polygon(plan(t), fill=(shade, shade, shade, 255))
    for t in walls:
        points = plan(t)
        draw.line(points + [points[0]], fill=(235, 235, 240, 255), width=1)
    buffer = io.BytesIO()
    image.save(buffer, "PNG", optimize=True)
    return ("data:image/png;base64," + base64.b64encode(buffer.getvalue()).decode(), low, far, width, height)


PAGE = """<!doctype html>
<meta charset="utf-8">
<title>__TITLE__</title>
<style>
html,body{margin:0;height:100%;background:#111;color:#ddd;font:13px/1.35 -apple-system,system-ui,sans-serif}
#app{display:flex;height:100%}
#side{width:250px;overflow:auto;padding:10px;background:#1b1b1d;border-right:1px solid #333;box-sizing:border-box}
#view{flex:1;position:relative}
canvas{display:block;width:100%;height:100%}
#info{position:absolute;right:10px;bottom:10px;max-width:420px;background:#000c;border:1px solid #444;padding:8px 10px;border-radius:6px;white-space:pre-wrap;font:12px/1.4 ui-monospace,Menlo,monospace;pointer-events:none}
h1{font-size:14px;margin:0 0 8px}
label{display:block;cursor:pointer;white-space:nowrap}
.sw{display:inline-block;width:10px;height:10px;margin-right:6px;border-radius:2px;vertical-align:-1px}
.room{padding:2px 4px;cursor:pointer;border-radius:3px}.room:hover{background:#333}
hr{border:0;border-top:1px solid #333;margin:10px 0}
small{color:#888}
</style>
<div id="app"><div id="side"></div><div id="view"><canvas id="c"></canvas><div id="info">hover a shape</div></div></div>
<script>
const DATA = __DATA__;
const MODEL = __MODEL__;
const modelImage = new Image(); if (MODEL) { modelImage.onload = () => draw(); modelImage.src = MODEL.url; }
const TAGS = {0:["door","#4fc3f7"],1:["exit","#ffb74d"],2:["room door","#81c784"],3:["lock / item","#e57373"],4:["use item","#ba68c8"],
  5:["examine / message","#fff176"],6:["tag 6 (ladder?)","#a1887f"],7:["item glint","#f06292"],10:["tag 10","#90a4ae"],
  11:["message","#aed581"],12:["tag 12","#4db6ac"],13:["tag 13","#7986cb"]};
const layers = {models:true, boxes:false, cameras:true, links:true, names:true};
for (const t in TAGS) layers["t"+t] = true;
const cv = document.getElementById("c"), ctx = cv.getContext("2d"), info = document.getElementById("info");
let view = {x:0, y:0, s:0.05};   // world (x, z) -> screen: sx = (x - view.x) * s, sy = -(z - view.y) * s
let hover = null, shapes = [];
const sx = x => (x - view.x) * view.s + cv.width / 2, sy = z => -(z - view.y) * view.s + cv.height / 2;
const okQuad = q => q.some(p => p[0] || p[1]) && q.every(p => Math.abs(p[0]) < 1e6 && Math.abs(p[1]) < 1e6);
const okCam = c => [c.eye, c.target].every(p => p.every(v => Math.abs(v) < 1e6));
function bounds() {
  let a = [1e9, 1e9, -1e9, -1e9];
  for (const r of DATA) for (const ar of r.areas) if (okQuad(ar.quad)) for (const p of ar.quad) { a = [Math.min(a[0], p[0]), Math.min(a[1], p[1]), Math.max(a[2], p[0]), Math.max(a[3], p[1])]; }
  for (const r of DATA) for (const c of r.cameras) if (okCam(c)) for (const p of [c.eye, c.target]) { a = [Math.min(a[0], p[0]), Math.min(a[1], p[2]), Math.max(a[2], p[0]), Math.max(a[3], p[2])]; }
  return a;
}
function fit() {
  const b = bounds(), w = cv.width, h = cv.height;
  view.x = (b[0] + b[2]) / 2; view.y = (b[1] + b[3]) / 2;
  view.s = Math.min(w / (b[2] - b[0] + 400), h / (b[3] - b[1] + 400));
}
function roomColor(i) { return "hsl(" + (i * 47 % 360) + ",60%,60%)"; }
function roomCentre(r) {
  let n = 0, x = 0, z = 0;
  for (const ar of r.areas) if (okQuad(ar.quad)) for (const p of ar.quad) { x += p[0]; z += p[1]; n++; }
  for (const c of r.cameras) if (okCam(c)) for (const p of c.box.slice(0, 4)) { x += p[0]; z += p[2]; n++; }
  return n ? [x / n, z / n] : [0, 0];
}
function addShape(kind, pts, label, extra) { shapes.push({kind, pts, label, extra}); }
function draw() {
  ctx.clearRect(0, 0, cv.width, cv.height);
  shapes = [];
  ctx.lineWidth = 1;
  if (MODEL && layers.models && modelImage.complete && modelImage.naturalWidth) {
    ctx.imageSmoothingEnabled = true;
    ctx.drawImage(modelImage, sx(MODEL.x), sy(MODEL.z), MODEL.w * MODEL.scale * view.s, MODEL.h * MODEL.scale * view.s);
  }
  DATA.forEach((r, ri) => {
    const col = roomColor(ri);
    if (layers.boxes) for (const c of r.cameras) {
      const q = c.box.slice(0, 4).map(p => [p[0], p[2]]);
      if (!okQuad(q)) continue;
      ctx.beginPath(); q.forEach((p, i) => i ? ctx.lineTo(sx(p[0]), sy(p[1])) : ctx.moveTo(sx(p[0]), sy(p[1]))); ctx.closePath();
      ctx.fillStyle = col.replace("60%)", "60%,0.07)").replace("hsl", "hsla"); ctx.fill();
      ctx.strokeStyle = col.replace("60%)", "60%,0.35)").replace("hsl", "hsla"); ctx.stroke();
      addShape("box", q, "room " + r.id + " camera " + c.i + " switch box (type " + c.kind + ", fov " + c.fov[0] + "/" + c.fov[1] + ")", {room:r, cam:c});
    }
    for (const ar of r.areas) {
      if (!layers["t" + ar.tag] || !okQuad(ar.quad)) continue;
      const tc = (TAGS[ar.tag] || ["tag " + ar.tag, "#ccc"])[1];
      ctx.beginPath(); ar.quad.forEach((p, i) => i ? ctx.lineTo(sx(p[0]), sy(p[1])) : ctx.moveTo(sx(p[0]), sy(p[1]))); ctx.closePath();
      ctx.fillStyle = tc + "44"; ctx.fill(); ctx.strokeStyle = tc; ctx.lineWidth = ar.tag <= 1 ? 2 : 1; ctx.stroke(); ctx.lineWidth = 1;
      addShape("area", ar.quad, "room " + r.id + " area " + ar.i, {room:r, area:ar});
    }
    if (layers.cameras) for (const c of r.cameras) {
      if (!okCam(c)) continue;
      const e = [sx(c.eye[0]), sy(c.eye[2])], t = [sx(c.target[0]), sy(c.target[2])];
      ctx.strokeStyle = col; ctx.beginPath(); ctx.moveTo(e[0], e[1]); ctx.lineTo(t[0], t[1]); ctx.stroke();
      ctx.fillStyle = col; ctx.fillRect(e[0] - 3, e[1] - 3, 6, 6);
      ctx.beginPath(); ctx.arc(t[0], t[1], 2, 0, 7); ctx.fill();
      addShape("cam", [[c.eye[0], c.eye[2]]], "room " + r.id + " camera " + c.i, {room:r, cam:c});
    }
  });
  if (layers.links) DATA.forEach(r => { for (const ar of r.areas) {
    if (ar.to === undefined || !ar.arrival || !layers["t" + ar.tag] || !okQuad(ar.quad)) continue;
    const cx = ar.quad.reduce((a, p) => a + p[0], 0) / 4, cz = ar.quad.reduce((a, p) => a + p[1], 0) / 4;
    ctx.strokeStyle = TAGS[ar.tag][1]; ctx.setLineDash([4, 4]); ctx.beginPath(); ctx.moveTo(sx(cx), sy(cz)); ctx.lineTo(sx(ar.arrival[0]), sy(ar.arrival[1])); ctx.stroke(); ctx.setLineDash([]);
    ctx.fillStyle = TAGS[ar.tag][1]; ctx.beginPath(); ctx.arc(sx(ar.arrival[0]), sy(ar.arrival[1]), 3, 0, 7); ctx.fill();
  } });
  if (layers.names) { ctx.font = "13px sans-serif"; ctx.textAlign = "center";
    DATA.forEach((r, ri) => { const c = roomCentre(r); ctx.fillStyle = roomColor(ri); ctx.fillText(r.id + (r.name ? " " + r.name : ""), sx(c[0]), sy(c[1])); }); }
}
function pick(mx, my) {
  let best = null, bd = 1e9;
  for (const s of shapes) {
    const pts = s.pts.map(p => [sx(p[0]), sy(p[1])]);
    let d = 1e9;
    if (s.kind === "cam") d = Math.hypot(pts[0][0] - mx, pts[0][1] - my) - 4;
    else {
      let inside = false;
      for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) if ((pts[i][1] > my) !== (pts[j][1] > my) && mx < (pts[j][0] - pts[i][0]) * (my - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0]) inside = !inside;
      d = inside ? 0 : 1e9;
      if (inside && s.kind === "box") d = 8;   // areas beat boxes under the pointer
      if (inside) d -= 1 / Math.max(1, Math.abs(pts.reduce((a, p, i) => a + (p[0] * pts[(i + 1) % pts.length][1] - pts[(i + 1) % pts.length][0] * p[1]), 0)) / 2) * 1000;
    }
    if (d < bd && d < 1e8) { bd = d; best = s; }
  }
  return best;
}
function text(s) {
  const r = s.extra.room;
  if (s.kind === "cam") { const c = s.extra.cam; return "room " + r.id + (r.name ? " (" + r.name + ")" : "") + " camera " + c.i + "\\ntype " + c.kind + "  fov " + c.fov.join("/") + "\\neye " + c.eye.join(", ") + "\\ntarget " + c.target.join(", "); }
  if (s.kind === "box") { const c = s.extra.cam; return "room " + r.id + (r.name ? " (" + r.name + ")" : "") + "\\ncamera " + c.i + " switch box\\ntype " + c.kind + "  fov " + c.fov.join("/") + "\\neye " + c.eye.join(", ") + "\\ntarget " + c.target.join(", "); }
  const a = s.extra.area, t = TAGS[a.tag] ? TAGS[a.tag][0] : "tag " + a.tag;
  let o = "room " + r.id + (r.name ? " (" + r.name + ")" : "") + "  " + r.file + "\\narea " + a.i + ": " + t + " (tag " + a.tag + ", subtype " + a.sub + ")\\nheight " + a.h.join(" to ") + "\\nquad " + a.quad.map(p => p.join(",")).join("  ");
  if (a.to !== undefined) o += "\\nleads to room " + a.to + (a.door !== undefined ? "  (its door " + a.other + ", this door " + a.door + ", facing " + a.facing + ")" : "") + (a.arrival ? "\\narrives at " + a.arrival[0].toFixed(0) + ", " + a.arrival[1].toFixed(0) + " facing " + a.arrival[2] : "");
  o += "\\nfields " + a.fields.join(" ");
  return o;
}
function resize() { const r = cv.parentElement.getBoundingClientRect(); cv.width = r.width * devicePixelRatio; cv.height = r.height * devicePixelRatio; draw(); }
function toCanvas(e) { const r = cv.getBoundingClientRect(); return [(e.clientX - r.left) * devicePixelRatio, (e.clientY - r.top) * devicePixelRatio]; }
let drag = null;
cv.addEventListener("mousedown", e => { drag = [e.clientX, e.clientY, view.x, view.y]; });
addEventListener("mouseup", () => { drag = null; });
cv.addEventListener("mousemove", e => {
  const [mx, my] = toCanvas(e);
  if (drag) { view.x = drag[2] - (e.clientX - drag[0]) * devicePixelRatio / view.s; view.y = drag[3] + (e.clientY - drag[1]) * devicePixelRatio / view.s; draw(); return; }
  const s = pick(mx, my); if (s !== hover) { hover = s; info.textContent = s ? text(s) : "hover a shape"; }
});
cv.addEventListener("wheel", e => {
  e.preventDefault(); const [mx, my] = toCanvas(e);
  const wx = (mx - cv.width / 2) / view.s + view.x, wz = -(my - cv.height / 2) / view.s + view.y;
  view.s *= Math.exp(-e.deltaY * 0.0015);
  view.x = wx - (mx - cv.width / 2) / view.s; view.y = wz + (my - cv.height / 2) / view.s; draw();
}, {passive:false});
addEventListener("resize", resize);
const side = document.getElementById("side");
let html = "<h1>__TITLE__</h1><small>wheel zooms, drag pans</small><hr>";
html += ["models:room models (from above)", "boxes:camera boxes", "cameras:camera eyes and aims", "links:door and exit arrivals", "names:room names"].map(s => { const [k, n] = s.split(":"); return '<label><input type="checkbox" data-k="' + k + '"' + (layers[k] ? " checked" : "") + "> " + n + "</label>"; }).join("");
html += "<hr>";
for (const t in TAGS) html += '<label><input type="checkbox" data-k="t' + t + '" checked><span class="sw" style="background:' + TAGS[t][1] + '"></span>' + TAGS[t][0] + "</label>";
html += "<hr><b>Rooms</b>" + DATA.map((r, i) => '<div class="room" data-i="' + i + '"><span class="sw" style="background:' + roomColor(i) + '"></span>' + r.id + (r.variant ? "." + r.variant : "") + " " + r.name + "</div>").join("");
side.innerHTML = html;
side.addEventListener("change", e => { layers[e.target.dataset.k] = e.target.checked; draw(); });
side.addEventListener("click", e => { const d = e.target.closest(".room"); if (!d) return; const c = roomCentre(DATA[+d.dataset.i]); view.x = c[0]; view.y = c[1]; view.s = Math.max(view.s, 0.12); draw(); });
resize(); fit(); draw();
</script>
__EDITOR__
"""


def render(game, scenario, data, editor=""):
    """The page's HTML for rooms from build(); `editor` is extra markup (the editor's script) put after the map's"""
    spots = [p for r in data for a in r["areas"] for p in a["quad"] if any(p) and max(map(abs, p)) < 1e6]
    spots += [(c["eye"][0], c["eye"][2]) for r in data for c in r["cameras"] if max(map(abs, c["eye"])) < 1e6]
    margin = 1200
    bounds = (min(p[0] for p in spots) - margin, min(p[1] for p in spots) - margin, max(p[0] for p in spots) + margin,
              max(p[1] for p in spots) + margin)
    picture = model_picture(game, scenario, [r["id"] for r in data if r["variant"] == 0], bounds)
    model = "null" if picture is None else json.dumps({"url": picture[0], "x": picture[1], "z": picture[2], "w": picture[3], "h": picture[4],
                                                       "scale": MODEL_SCALE})
    title = f"{game} r{scenario:03d}: {len(data)} rooms"
    return PAGE.replace("__TITLE__", title).replace("__DATA__", json.dumps(data)).replace("__MODEL__", model).replace("__EDITOR__", editor)


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    options = argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 1
    game = args[0]
    names = {}
    if "--names" in options:
        names = room_names(options[options.index("--names") + 1])
        args = [a for a in args if a != options[options.index("--names") + 1]]
    if args[1] == "all":
        scenarios = sorted({int(pack[1:4]) for pack, _, _ in rdt.rooms(game)})
        links = []
        for number in scenarios:
            data = build(game, number, names, False)
            page = ROOT / "work" / f"rdt-map-{game}-r{number:03d}.html"
            page.parent.mkdir(parents=True, exist_ok=True)
            page.write_text(render(game, number, data))
            links.append(f'<li><a href="{page.name}">r{number:03d}</a> {len(data)} rooms, {sum(len(r["areas"]) for r in data)} areas</li>')
            print(page)
        index = ROOT / "work" / f"rdt-maps-{game}.html"
        index.write_text(f"<!doctype html><meta charset=utf-8><title>{game} room maps</title><body style='font:14px sans-serif;background:#111;color:#ddd'><h1>{game} room maps</h1><ul>{''.join(links)}</ul>")
        print(index)
        return 0
    scenario = int(args[1])
    out = Path(args[2]) if len(args) > 2 else ROOT / "work" / f"rdt-map-{game}-r{scenario:03d}.html"
    data = build(game, scenario, names, "--variants" in options)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(render(game, scenario, data))
    print(f"{out}: {len(data)} rooms, {sum(len(r['areas']) for r in data)} areas, {sum(len(r['cameras']) for r in data)} cameras")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
