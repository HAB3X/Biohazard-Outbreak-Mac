// The markers drawn over the models (areas, doors, cameras, items, enemies ...) on a 2D canvas through the same camera, and finding what is under the pointer.
import { store, describe, centre, refEq, roomOf } from "./scene.js";
import { nameOf } from "./library.js";

const css = getComputedStyle(document.documentElement);
const colour = (name) => css.getPropertyValue("--" + name).trim() || "#9aa";
const rgba = (hex, a) => {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16 & 255},${n >> 8 & 255},${n & 255},${a})`;
};

let hits = [];            // what was drawn last frame, in drawing order: {ref, test(sx, sy) -> distance or -1, order}
let handleList = [];      // the selected thing's handles, on the screen

export const lastHandles = () => handleList;
let floorLookup = () => 0;
export const setFloorLookup = (fn) => { floorLookup = fn; };
const floorY = (file, x, z) => floorLookup(file, x, z);

// ---- what a selected thing can be pulled by
export function handlesOf(ref) {
  const r = roomOf(ref.file);
  if (!r) return [];
  const o = r[ref.c].find((x) => x.i === ref.i);
  if (!o) return [];
  const out = [];
  if (ref.c === "areas") o.quad.forEach((p, n) => out.push({ id: "corner" + n, p: [p[0], (o.h[0] + o.h[1]) / 2, p[1]], kind: "square" }));
  if (ref.c === "cameras") {
    out.push({ id: "target", p: o.target, kind: "ring" });
    o.box.slice(0, 4).forEach((p, n) => out.push({ id: "box" + n, p, kind: "square", dim: true }));
  }
  if (ref.c === "zones") o.box.slice(0, 4).forEach((p, n) => out.push({ id: "zone" + n, p, kind: "square" }));
  if (ref.c === "walls") {
    out.push({ id: "wa", p: [o.a[0], o.y[0], o.a[1]], kind: "square" }, { id: "wb", p: [o.b[0], o.y[0], o.b[1]], kind: "square" });
  }
  if (ref.c === "floors") out.push({ id: "fa", p: [o.min[0], o.y, o.min[1]], kind: "square" }, { id: "fb", p: [o.max[0], o.y, o.max[1]], kind: "square" });
  if (ref.c === "spawns") {
    const a = o.facing / 65536 * Math.PI * 2;
    out.push({ id: "facing", p: [o.pos[0] + Math.sin(a) * 150, floorY(ref.file, o.pos[0], o.pos[2]), o.pos[2] + Math.cos(a) * 150], kind: "ring" });
  }
  if (ref.c === "enemies") {
    const a = o.facing / 65536 * Math.PI * 2;
    out.push({ id: "facing", p: [o.pos[0] + Math.sin(a) * 160, o.pos[1], o.pos[2] + Math.cos(a) * 160], kind: "ring" });
  }
  // the same for everything that stands in the room: a handle to lift it, and for objects one to turn it and one to resize it
  if (store.mode !== "plan") {
    // arrows along the room's own axes: drag one to move the thing along it only
    if (["items", "enemies", "areas", "floors", "scenery", "zones"].includes(ref.c) && (ref.c !== "scenery" || o.cs)) {
      const c = centre(ref.c, o), len = 260;
      out.push({ id: "axx", p: [c[0] + len, c[1], c[2]], kind: "axis", from: c, axis: [1, 0, 0] }, { id: "axz", p: [c[0], c[1], c[2] + len], kind: "axis", from: c, axis: [0, 0, 1] });
    }
    if (ref.c === "items" || ref.c === "enemies") out.push({ id: "lift", p: [o.pos[0], o.pos[1] + (ref.c === "enemies" ? 240 : 150), o.pos[2]], kind: "lift" });
    if (ref.c === "areas") { const q = o.quad, cx = (q[0][0] + q[2][0]) / 2, cz = (q[0][1] + q[2][1]) / 2; out.push({ id: "lift", p: [cx, o.h[1] + 90, cz], kind: "lift" }); }
    if (ref.c === "floors") out.push({ id: "lift", p: [(o.min[0] + o.max[0]) / 2, o.y + 120, (o.min[1] + o.max[1]) / 2], kind: "lift" });
    if (ref.c === "scenery" && o.cs) {
      const top = Math.max(...o.cs.map((p) => p[1])), bottom = Math.min(...o.cs.map((p) => p[1])), wide = Math.max(...o.cs.map((p) => Math.hypot(p[0] - o.wc[0], p[2] - o.wc[2])));
      out.push({ id: "lift", p: [o.wc[0], top + 100, o.wc[2]], kind: "lift" },
               { id: "turn", p: [o.wc[0] + wide + 120, bottom, o.wc[2]], kind: "turn" },
               { id: "size", p: [o.wc[0] + wide * 0.7, top, o.wc[2] + wide * 0.7], kind: "size" });
    }
  }
  return out;
}

export function draw(ctx, camera, dpr, rooms, hover) {
  const w = camera.w, h = camera.h;
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);
  const vp = camera.viewProj();
  const P = (p) => camera.project(p, vp);
  const px = camera.mode === "plan" ? 1 / camera.scale : Math.min(1, 2200 / camera.dist);   // how big world units look
  hits = [];
  const show = store.show;
  const selected = (ref) => store.selection.some((s) => refEq(s, ref));
  const hovered = (ref) => hover && refEq(hover, ref);
  ctx.lineJoin = "round"; ctx.lineCap = "round";
  ctx.font = "11px -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif";

  const pill = (text, x, y, color, strong) => {
    const tw = ctx.measureText(text).width;
    ctx.fillStyle = "rgba(10,12,16,.82)";
    ctx.beginPath(); ctx.roundRect(x - tw / 2 - 5, y - 8, tw + 10, 16, 5); ctx.fill();
    ctx.fillStyle = strong ? "#fff" : color; ctx.textAlign = "center"; ctx.textBaseline = "middle";
    ctx.fillText(text, x, y + 0.5);
  };

  for (const file of rooms) {
    const r = roomOf(file);
    if (!r) continue;
    const current = file === store.room;
    const base = current ? 1 : 0.38;
    const layers = [];

    if (show.zones) {
      for (const z of r.zones) {
        const ref = { c: "zones", file, i: z.i }, pts = z.box.slice(0, 4).map((p) => P([p[0], p[1], p[2]]));
        if (pts.some((p) => p[2] < -0.9)) continue;
        const col = colour("zone"), sel = selected(ref);
        layers.push({ order: 1, run() {
          ctx.setLineDash([5, 4]); ctx.strokeStyle = rgba(col, sel ? 1 : 0.7 * base); ctx.fillStyle = rgba(col, 0.08 * base);
          path(pts); ctx.fill(); ctx.stroke(); ctx.setLineDash([]);
        } });
        hits.push({ ref, order: 1, test: polyTest(pts, 300) });
      }
    }

    if (show.areas) {
      for (const a of r.areas) {
        if (!current && a.tag !== 0 && a.tag !== 1) continue;
        if (a.tag !== 0 && a.tag !== 1 && !show.triggers && !selected({ c: "areas", file, i: a.i }) && !hovered({ c: "areas", file, i: a.i })) continue;
        const ref = { c: "areas", file, i: a.i }, d = describe("areas", a), col = colour(d.color);
        const y = (a.h[0] + a.h[1]) / 2;
        const lo = a.quad.map((p) => P([p[0], a.h[0], p[1]])), pts = a.quad.map((p) => P([p[0], y, p[1]]));
        if (pts.some((p) => p[2] < -0.9)) continue;
        const sel = selected(ref), hov = hovered(ref);
        const big = Math.abs(a.quad[0][0] - a.quad[2][0]) + Math.abs(a.quad[0][1] - a.quad[2][1]);
        layers.push({ order: 2, run() {
          ctx.fillStyle = rgba(col, (sel ? 0.34 : hov ? 0.3 : 0.2) * base);
          ctx.strokeStyle = rgba(col, sel ? 1 : 0.8 * base);
          ctx.lineWidth = sel ? 2.2 : 1.3;
          if (camera.mode !== "plan") {
            const hi = a.quad.map((p) => P([p[0], a.h[1], p[1]]));
            path(hi); ctx.fill(); ctx.stroke();
            path(lo); ctx.stroke();
            ctx.beginPath(); lo.forEach((p, n) => { ctx.moveTo(p[0], p[1]); ctx.lineTo(hi[n][0], hi[n][1]); }); ctx.stroke();
          } else { path(pts); ctx.fill(); ctx.stroke(); }
          ctx.lineWidth = 1;
          const c = mid(pts);
          if (a.tag === 0 || a.tag === 1) arrival(a, c, col, base);
          if (sel || (hov && current) || (show.labels && current && (a.tag === 0 || a.tag === 1) && big * px > 20)) pill(label(a, d), c[0], c[1], col, sel);
        } });
        hits.push({ ref, order: 2, test: polyTest(pts, 7) });
      }
    }

    if (show.cameras && current) {
      for (const c of r.cameras) {
        if (c.eye[0] < -1e8) continue;
        const ref = { c: "cameras", file, i: c.i }, col = colour("camera"), sel = selected(ref);
        const e = P(c.eye), t = P(c.target);
        if (e[2] < -0.9) continue;
        layers.push({ order: 4, run() {
          ctx.strokeStyle = rgba(col, (sel ? 0.95 : 0.4) * base); ctx.lineWidth = 1;
          ctx.setLineDash([4, 4]); ctx.beginPath(); ctx.moveTo(e[0], e[1]); ctx.lineTo(t[0], t[1]); ctx.stroke(); ctx.setLineDash([]);
          // the view: a cone from the eye to the target
          if (sel || current) {
            const dx = t[0] - e[0], dy = t[1] - e[1], len = Math.hypot(dx, dy);
            if (len > 8) {
              const hw = len * Math.tan((c.fov[0] * Math.PI / 180) / 2) * 0.6;
              const nx = -dy / len * hw, ny = dx / len * hw;
              ctx.fillStyle = rgba(col, (sel ? 0.12 : 0.05) * base);
              ctx.beginPath(); ctx.moveTo(e[0], e[1]); ctx.lineTo(t[0] + nx, t[1] + ny); ctx.lineTo(t[0] - nx, t[1] - ny); ctx.closePath(); ctx.fill();
            }
          }
          if (sel) { ctx.fillStyle = rgba(col, 1); ctx.beginPath(); ctx.arc(t[0], t[1], 4, 0, 7); ctx.fill(); }
          glyph("camera", e[0], e[1], col, sel, base);
          if (sel) pill("camera " + c.i, e[0], e[1] - 18, col, true);
          if (sel && camera.mode === "plan") {
            const b = c.box.slice(0, 4).map((p) => P(p));
            ctx.setLineDash([3, 3]); ctx.strokeStyle = rgba(col, 0.6); path(b); ctx.stroke(); ctx.setLineDash([]);
          }
        } });
        hits.push({ ref, order: 4, test: pointTest(e, 12) });
      }
    }

    if (show.lights && current) {
      for (const l of r.lights) {
        const ref = { c: "lights", file, i: l.i }, col = colour("light"), p = P(l.pos), sel = selected(ref);
        if (p[2] < -0.9) continue;
        layers.push({ order: 5, run() {
          if (sel || (current && camera.mode === "plan")) { ctx.strokeStyle = rgba(col, sel ? 0.8 : 0.18); ctx.setLineDash([2, 4]); ctx.beginPath(); ctx.arc(p[0], p[1], Math.max(4, l.range * px), 0, 7); ctx.stroke(); ctx.setLineDash([]); }
          glyph("light", p[0], p[1], col, sel, base);
        } });
        hits.push({ ref, order: 5, test: pointTest(p, 10) });
      }
    }

    if (show.spawns !== false && current) {
      for (const sp of r.spawns) {
        const ref = { c: "spawns", file, i: sp.i }, col = colour("spawn"), sel = selected(ref);
        const y = floorY(file, sp.pos[0], sp.pos[2]), p0 = [sp.pos[0], y, sp.pos[2]], p = P(p0);
        if (p[2] < -0.9) continue;
        if (camera.mode !== "plan") {
          const fig = figure(layers, p0, col, sel, hovered(ref), base, sp.facing, sp.i === 0 ? "P" : String(sp.i + 1));
          if (fig) {
            hits.push({ ref, order: 8, test: rectTest(fig.box), centre: [fig.f[0], (fig.f[1] + fig.h[1]) / 2] });
            if (sel || hovered(ref)) layers.push({ order: 9, run() { pill(sp.i === 0 ? "the player starts here" : `partner ${sp.i} starts here`, fig.f[0], fig.h[1] - 14, col, sel); } });
          }
          continue;
        }
        const a = sp.facing / 65536 * Math.PI * 2, f = P([sp.pos[0] + Math.sin(a) * 150, y, sp.pos[2] + Math.cos(a) * 150]);
        layers.push({ order: 8, run() {
          ctx.strokeStyle = rgba(col, sel ? 1 : 0.9 * base); ctx.lineWidth = 2.2;
          ctx.beginPath(); ctx.moveTo(p[0], p[1]); ctx.lineTo(f[0], f[1]); ctx.stroke();
          ctx.fillStyle = rgba(col, (sel ? 1 : 0.95) * base); ctx.strokeStyle = sel ? "#fff" : "rgba(0,0,0,.6)"; ctx.lineWidth = sel ? 1.8 : 1;
          ctx.beginPath(); ctx.moveTo(p[0], p[1] - 10); ctx.lineTo(p[0] + 8, p[1] + 6); ctx.lineTo(p[0] - 8, p[1] + 6); ctx.closePath(); ctx.fill(); ctx.stroke(); ctx.lineWidth = 1;
          ctx.fillStyle = "#06201a"; ctx.font = "bold 9px -apple-system, sans-serif"; ctx.textAlign = "center"; ctx.textBaseline = "middle"; ctx.fillText(String(sp.i + 1), p[0], p[1] + 1);
          ctx.font = "11px -apple-system, BlinkMacSystemFont, 'SF Pro Text', sans-serif";
          if (sel || (hovered(ref) && current)) pill(sp.i === 0 ? "player start" : `partner ${sp.i} start`, p[0], p[1] - 22, col, sel);
        } });
        hits.push({ ref, order: 8, test: pointTest(p, 13) });
      }
    }

    if (show.walls && current) {
      for (const f of r.floors) {
        const ref = { c: "floors", file, i: f.i }, col = colour("wall"), sel = selected(ref), hov = hovered(ref);
        const pts = [[f.min[0], f.min[1]], [f.max[0], f.min[1]], [f.max[0], f.max[1]], [f.min[0], f.max[1]]].map((p) => P([p[0], f.y, p[1]]));
        if (pts.some((p) => p[2] < -0.9)) continue;
        layers.push({ order: 2, run() {
          ctx.fillStyle = rgba(col, sel ? 0.3 : hov ? 0.24 : 0.14); ctx.strokeStyle = rgba(col, sel ? 1 : 0.7); ctx.lineWidth = sel ? 2 : 1.2; ctx.setLineDash([2, 3]);
          path(pts); ctx.fill(); ctx.stroke(); ctx.setLineDash([]); ctx.lineWidth = 1;
        } });
        hits.push({ ref, order: 2, test: polyTest(pts, 4) });
      }
      for (const w of r.walls) {
        const ref = { c: "walls", file, i: w.i }, col = colour("wall"), sel = selected(ref), hov = hovered(ref);
        const a = P([w.a[0], w.y[0], w.a[1]]), b = P([w.b[0], w.y[0], w.b[1]]);
        if (a[2] < -0.9 || b[2] < -0.9) continue;
        const mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2;
        const nrm = P([(w.a[0] + w.b[0]) / 2 + w.n[0] * 90, w.y[0], (w.a[1] + w.b[1]) / 2 + w.n[1] * 90]);
        layers.push({ order: 3, run() {
          ctx.strokeStyle = rgba(col, sel ? 1 : hov ? 0.95 : 0.75); ctx.lineWidth = sel ? 3 : 2;
          if (camera.mode !== "plan") {
            const a2 = P([w.a[0], w.y[1], w.a[1]]), b2 = P([w.b[0], w.y[1], w.b[1]]);
            ctx.fillStyle = rgba(col, sel ? 0.22 : 0.1); ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.lineTo(b2[0], b2[1]); ctx.lineTo(a2[0], a2[1]); ctx.closePath(); ctx.fill();
          }
          ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(b[0], b[1]); ctx.stroke();
          if (sel || hov) { ctx.lineWidth = 1.2; ctx.beginPath(); ctx.moveTo(mx, my); ctx.lineTo(nrm[0], nrm[1]); ctx.stroke(); }
          ctx.lineWidth = 1;
        } });
        hits.push({ ref, order: 3.5, test: (sx, sy) => { const d = segDist(sx, sy, a, b); return d <= 6 ? d : -1; } });
      }
    }

    if (current && r.scenery && (store.tool === "scenery" || store.tool === "select" || store.selection.some((s) => s.c === "scenery"))) {
      const EDGES = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
      for (const s of r.scenery) {
        const ref = { c: "scenery", file, i: s.i }, sel = selected(ref), hov = hovered(ref), tool = store.tool === "scenery";
        // with the plain Select tool only things that stand (not floor planes, decals or whole walls) answer the pointer, and show only when pointed at
        if (!tool && !sel) {
          const ys = s.cs.map((p) => p[1]), xs = s.cs.map((p) => p[0]), zs = s.cs.map((p) => p[2]);
          if (Math.max(...ys) - Math.min(...ys) < 12 || Math.max(Math.max(...xs) - Math.min(...xs), Math.max(...zs) - Math.min(...zs)) > 700 || s.scale[0] < 0.001) continue;
          if (!hov) { const q = s.cs.map((p) => P(p)); if (q.some((p) => p[2] < -0.9)) continue; const x0 = Math.min(...q.map((p) => p[0])), x1 = Math.max(...q.map((p) => p[0])), y0 = Math.min(...q.map((p) => p[1])), y1 = Math.max(...q.map((p) => p[1])); hits.push({ ref, order: 5, centre: [(x0 + x1) / 2, (y0 + y1) / 2], test: polyTest([[x0, y0], [x1, y0], [x1, y1], [x0, y1]], 3) }); continue; }
        }
        const pts = s.cs.map((p) => P(p));
        if (pts.some((p) => p[2] < -0.9)) continue;
        const col = colour("accent"), hid = s.scale[0] < 0.001;
        const x0 = Math.min(...pts.map((p) => p[0])), x1 = Math.max(...pts.map((p) => p[0])), y0 = Math.min(...pts.map((p) => p[1])), y1 = Math.max(...pts.map((p) => p[1]));
        layers.push({ order: 5, run() {
          ctx.strokeStyle = rgba(col, sel ? 1 : hov ? 0.95 : hid ? 0.25 : 0.5); ctx.lineWidth = sel ? 2 : 1;
          if (hid) ctx.setLineDash([3, 3]);
          if (camera.mode === "plan") { ctx.strokeRect(x0, y0, x1 - x0, y1 - y0); if (sel || hov) { ctx.fillStyle = rgba(col, 0.14); ctx.fillRect(x0, y0, x1 - x0, y1 - y0); } }
          else { ctx.beginPath(); for (const [a, b] of EDGES) { ctx.moveTo(pts[a][0], pts[a][1]); ctx.lineTo(pts[b][0], pts[b][1]); } ctx.stroke(); }
          ctx.setLineDash([]); ctx.lineWidth = 1;
        } });
        hits.push({ ref, order: 5, centre: [(x0 + x1) / 2, (y0 + y1) / 2], test: polyTest([[x0, y0], [x1, y0], [x1, y1], [x0, y1]], 3) });
      }
    }

    // the names of the room's objects (by their shape: the Library's names), for the one pointed at or picked and, with Names on, every named one in view
    if (current && r.scenery) {
      for (const s of r.scenery) {
        if (!s.cs || s.scale[0] < 0.001) continue;
        const ref = { c: "scenery", file, i: s.i }, strong = selected(ref) || hovered(ref), name = nameOf(s.key);
        if (!(strong || (store.show.names && name))) continue;
        const top = P([s.wc[0], Math.max(...s.cs.map((p) => p[1])) + 20, s.wc[2]]);
        if (top[2] < -0.9) continue;
        layers.push({ order: 8, run() { pill(name || `object ${s.i}`, top[0], top[1] - 8, colour("accent"), strong); } });
      }
    }

    // what you added that the game's picture doesn't show yet: its own picture standing in its box, until the game's picture is made again
    if (current && r.scenery && camera.pictureLens && !store.real.fresh) {
      for (const s of r.scenery) {
        if (!s.isNew || !s.library || !s.cs) continue;
        const q = s.cs.map((p) => P(p));
        if (q.some((p) => p[2] < -0.9)) continue;
        const img = sketchImage(s.library);
        if (!img || !img.complete) continue;
        const x0 = Math.min(...q.map((p) => p[0])), x1 = Math.max(...q.map((p) => p[0])), y0 = Math.min(...q.map((p) => p[1])), y1 = Math.max(...q.map((p) => p[1])), side = Math.max(x1 - x0, y1 - y0);
        layers.push({ order: 4, run() { ctx.save(); ctx.globalAlpha = 0.9; ctx.drawImage(img, (x0 + x1) / 2 - side / 2, (y0 + y1) / 2 - side / 2, side, side); ctx.restore(); } });
      }
    }

    if (show.nodes) {
      for (const n of r.nodes) {
        const ref = { c: "nodes", file, i: n.i }, col = colour("node"), p = P(n.pos), sel = selected(ref);
        if (p[2] < -0.9) continue;
        layers.push({ order: 3, run() { ctx.fillStyle = rgba(col, sel ? 1 : 0.7 * base); ctx.beginPath(); ctx.arc(p[0], p[1], sel ? 4.5 : 2.6, 0, 7); ctx.fill(); } });
        hits.push({ ref, order: 3, test: pointTest(p, 8) });
      }
    }

    if (show.items && (current || camera.mode === "plan" && px > 0.08)) {
      for (const n of r.items) {
        const ref = { c: "items", file, i: n.i }, col = colour("item"), p = P(n.pos), sel = selected(ref), hov = hovered(ref);
        if (p[2] < -0.9) continue;
        layers.push({ order: 6, run() {
          const s = sel ? 8 : hov ? 7.5 : 6.5;
          ctx.fillStyle = rgba(col, (sel ? 1 : 0.9) * base); ctx.strokeStyle = sel ? "#fff" : "rgba(0,0,0,.55)"; ctx.lineWidth = sel ? 1.8 : 1;
          ctx.beginPath(); ctx.moveTo(p[0], p[1] - s); ctx.lineTo(p[0] + s, p[1]); ctx.lineTo(p[0], p[1] + s); ctx.lineTo(p[0] - s, p[1]); ctx.closePath(); ctx.fill(); ctx.stroke();
          ctx.lineWidth = 1;
          if (sel || (hov && current)) pill(`${itemName(n.kind)} ×${n.qty}`, p[0], p[1] - 17, col, sel);
        } });
        hits.push({ ref, order: 6, test: pointTest(p, 11) });
      }
    }

    if (show.enemies && (current || camera.mode === "plan" && px > 0.08)) {
      for (const n of r.enemies) {
        if (n.modes === 0 && !store.selection.some((s) => s.c === "enemies" && s.file === file && s.i === n.i)) continue;
        const ref = { c: "enemies", file, i: n.i }, col = colour("enemy"), p = P(n.pos), sel = selected(ref), hov = hovered(ref);
        if (p[2] < -0.9) continue;
        if (camera.mode !== "plan") {
          const fig = figure(layers, [n.pos[0], floorY(file, n.pos[0], n.pos[2]), n.pos[2]], col, sel, hov, base, n.facing, "");
          if (fig) {
            hits.push({ ref, order: 7, test: rectTest(fig.box), centre: [fig.f[0], (fig.f[1] + fig.h[1]) / 2] });
            if (sel || hov) layers.push({ order: 9, run() { pill(`enemy ${n.id} · ${hpOf(n)}`, fig.f[0], fig.h[1] - 14, col, sel); } });
          }
          continue;
        }
        const a = n.facing / 65536 * Math.PI * 2;
        const f = P([n.pos[0] + Math.sin(a) * 120, n.pos[1], n.pos[2] + Math.cos(a) * 120]);
        layers.push({ order: 7, run() {
          ctx.strokeStyle = rgba(col, sel ? 1 : 0.85 * base); ctx.lineWidth = 2;
          ctx.beginPath(); ctx.moveTo(p[0], p[1]); ctx.lineTo(f[0], f[1]); ctx.stroke();
          ctx.fillStyle = rgba(col, (sel ? 1 : 0.92) * base); ctx.strokeStyle = sel ? "#fff" : "rgba(0,0,0,.55)"; ctx.lineWidth = sel ? 1.8 : 1;
          ctx.beginPath(); ctx.arc(p[0], p[1], sel ? 8 : hov ? 7.5 : 6.5, 0, 7); ctx.fill(); ctx.stroke(); ctx.lineWidth = 1;
          if (sel || (hov && current)) pill(`enemy ${n.id} · ${hpOf(n)}`, p[0], p[1] - 18, col, sel);
        } });
        hits.push({ ref, order: 7, test: pointTest(p, 12) });
      }
    }

    layers.sort((a, b) => a.order - b.order).forEach((l) => l.run());
  }

  // handles of what is selected
  handleList = [];
  if (store.selection.length === 1) {
    for (const hd of handlesOf(store.selection[0])) {
      const s = P(hd.p);
      if (s[2] < -0.9) continue;
      handleList.push({ ...hd, s });
      ctx.fillStyle = hd.dim ? "rgba(255,255,255,.5)" : "#fff"; ctx.strokeStyle = "#0d0f14"; ctx.lineWidth = 1.5;
      if (hd.kind === "axis") {            // an arrow along x (red) or z (blue)
        const from = P(hd.from), col = hd.axis[0] ? "#ff6b6b" : "#6b9bff", ang = Math.atan2(s[1] - from[1], s[0] - from[0]);
        ctx.strokeStyle = col; ctx.lineWidth = 2.4; ctx.beginPath(); ctx.moveTo(from[0], from[1]); ctx.lineTo(s[0], s[1]); ctx.stroke();
        ctx.fillStyle = col; ctx.strokeStyle = "#0d0f14"; ctx.lineWidth = 1.2;
        ctx.beginPath(); ctx.moveTo(s[0] + Math.cos(ang) * 10, s[1] + Math.sin(ang) * 10); ctx.lineTo(s[0] + Math.cos(ang + 2.5) * 9, s[1] + Math.sin(ang + 2.5) * 9); ctx.lineTo(s[0] + Math.cos(ang - 2.5) * 9, s[1] + Math.sin(ang - 2.5) * 9); ctx.closePath(); ctx.fill(); ctx.stroke();
      } else if (hd.kind === "lift") {            // an arrow up from the thing: drag to raise or lower it
        const foot = P([hd.p[0], hd.p[1] - 90, hd.p[2]]);
        ctx.strokeStyle = "rgba(255,255,255,.8)"; ctx.lineWidth = 1.6; ctx.beginPath(); ctx.moveTo(foot[0], foot[1]); ctx.lineTo(s[0], s[1] + 6); ctx.stroke();
        ctx.fillStyle = "#7ee0a0"; ctx.strokeStyle = "#0d0f14"; ctx.lineWidth = 1.5;
        ctx.beginPath(); ctx.moveTo(s[0], s[1] - 9); ctx.lineTo(s[0] + 8, s[1] + 5); ctx.lineTo(s[0] - 8, s[1] + 5); ctx.closePath(); ctx.fill(); ctx.stroke();
        ctx.fillStyle = "rgba(0,0,0,.6)"; ctx.font = "10px -apple-system, sans-serif"; ctx.textAlign = "center"; ctx.fillText("up/down", s[0], s[1] - 16);
      } else if (hd.kind === "turn") {     // a ring: drag around the thing to turn it
        ctx.fillStyle = "#ffd479"; ctx.beginPath(); ctx.arc(s[0], s[1], 7, 0, 7); ctx.fill(); ctx.stroke();
        ctx.strokeStyle = "#0d0f14"; ctx.lineWidth = 1.4; ctx.beginPath(); ctx.arc(s[0], s[1], 3.2, 0.5, 5.2); ctx.stroke();
        ctx.fillStyle = "rgba(0,0,0,.6)"; ctx.font = "10px -apple-system, sans-serif"; ctx.textAlign = "center"; ctx.fillText("turn", s[0], s[1] - 12);
      } else if (hd.kind === "size") {     // a corner: drag out or in to make it bigger or smaller
        ctx.fillStyle = "#8fb6ff"; ctx.beginPath(); ctx.moveTo(s[0], s[1] - 8); ctx.lineTo(s[0] + 8, s[1]); ctx.lineTo(s[0], s[1] + 8); ctx.lineTo(s[0] - 8, s[1]); ctx.closePath(); ctx.fill(); ctx.stroke();
        ctx.fillStyle = "rgba(0,0,0,.6)"; ctx.font = "10px -apple-system, sans-serif"; ctx.textAlign = "center"; ctx.fillText("size", s[0], s[1] - 12);
      } else {
        ctx.beginPath();
        if (hd.kind === "square") ctx.rect(s[0] - 4.5, s[1] - 4.5, 9, 9); else ctx.arc(s[0], s[1], 5.5, 0, 7);
        ctx.fill(); ctx.stroke();
      }
    }
  }
  hits.sort((a, b) => a.order - b.order);

  // a person standing there, in the picture's own perspective, so you can tell where they are and how big (about 190 tall, 50 across); `facing` shows which way they look
  function figure(layers, feet, col, sel, hov, base, facing, tag) {
    const f = P(feet), h = P([feet[0], feet[1] + 190, feet[2]]);
    if (f[2] < -0.9 || h[2] < -0.9) return null;
    const left = P([feet[0] - 25, feet[1], feet[2]]), right = P([feet[0] + 25, feet[1], feet[2]]);
    const w = Math.max(6, Math.hypot(right[0] - left[0], right[1] - left[1])), tall = Math.max(14, f[1] - h[1]), cx = (f[0] + h[0]) / 2;
    const head = Math.max(3.5, w * 0.34), top = h[1], bottom = f[1];
    const box = [cx - w / 2, top - head, cx + w / 2, bottom];
    layers.push({ order: 8, run() {
      ctx.save();
      const real = store.real && store.real.on;       // over the game's own picture the people are already there: only their outline and footprint
      ctx.fillStyle = rgba(col, (real ? (sel ? 0.3 : 0.1) : (sel ? 0.9 : hov ? 0.8 : 0.62)) * base); ctx.strokeStyle = sel ? "#fff" : real ? rgba(col, 0.95) : "rgba(255,255,255,.7)"; ctx.lineWidth = sel ? 2.4 : real ? 1.6 : 1.2;
      ctx.beginPath(); ctx.roundRect(cx - w / 2, top + head * 1.1, w, tall - head * 1.1, Math.min(w / 2, 8)); ctx.fill(); ctx.stroke();
      ctx.beginPath(); ctx.arc(cx, top + head * 0.55, head, 0, 7); ctx.fill(); ctx.stroke();
      ctx.beginPath(); ctx.ellipse(f[0], bottom, w * 0.7, w * 0.22, 0, 0, 7); ctx.strokeStyle = rgba(col, 0.9); ctx.lineWidth = 1.5; ctx.stroke();      // where the feet are on the floor
      if (facing !== null) { const a = facing / 65536 * Math.PI * 2, t = P([feet[0] + Math.sin(a) * 110, feet[1], feet[2] + Math.cos(a) * 110]); ctx.strokeStyle = "#fff"; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(f[0], f[1]); ctx.lineTo(t[0], t[1]); ctx.stroke(); }
      if (tag) { ctx.fillStyle = "rgba(0,0,0,.65)"; ctx.font = `bold ${Math.max(9, Math.min(15, w * 0.6))}px -apple-system, sans-serif`; ctx.textAlign = "center"; ctx.textBaseline = "middle"; ctx.fillText(tag, cx, top + tall * 0.45); }
      ctx.restore();
    } });
    return { f, h, box };
  }
  function rectTest(b) { return (sx, sy) => (sx >= b[0] - 4 && sx <= b[2] + 4 && sy >= b[1] - 4 && sy <= b[3] + 4 ? 5 : -1); }

  function path(pts) { ctx.beginPath(); pts.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1]))); ctx.closePath(); }
  function mid(pts) { return [pts.reduce((s, p) => s + p[0], 0) / pts.length, pts.reduce((s, p) => s + p[1], 0) / pts.length]; }
  function arrival(a, c, col, base) {
    if (!a.arrival) return;
    const t = P([a.arrival[0], (a.h[0] + a.h[1]) / 2, a.arrival[1]]);
    ctx.strokeStyle = rgba(col, 0.55 * base); ctx.setLineDash([3, 3]); ctx.beginPath(); ctx.moveTo(c[0], c[1]); ctx.lineTo(t[0], t[1]); ctx.stroke(); ctx.setLineDash([]);
  }
  function glyph(name, x, y, col, sel, base) {
    ctx.fillStyle = rgba(col, (sel ? 1 : 0.9) * base); ctx.strokeStyle = sel ? "#fff" : "rgba(0,0,0,.6)"; ctx.lineWidth = sel ? 1.8 : 1;
    ctx.beginPath();
    if (name === "camera") { ctx.roundRect(x - 7, y - 5, 14, 10, 2); } else { ctx.arc(x, y, 6, 0, 7); }
    ctx.fill(); ctx.stroke(); ctx.lineWidth = 1;
    if (name === "camera") { ctx.fillStyle = "#0d0f14"; ctx.beginPath(); ctx.arc(x, y, 2.4, 0, 7); ctx.fill(); }
  }
}

const label = (a, d) => {
  if (a.tag === 0) return `Door ${a.door} → room ${a.to}`;
  if (a.tag === 1) return `Exit → room ${a.to}`;
  return d.label;
};
const hpOf = (n) => (HP_NOTE[n.hp] || `tier ${n.hp}`);
const HP_NOTE = {};
export const itemName = (kind) => ITEMS[kind] || "item " + kind.toString(16);
export const ITEMS = {};

// the picture of a Library object (drawn once, kept)
const sketches = new Map();
function sketchImage(lib) {
  const key = `${lib.room}:${lib.mesh}`;
  if (!sketches.has(key)) {
    const r = store.renderer && store.renderer.thumbnail(lib.room, lib.mesh, 128, true);
    if (!r) return null;
    const img = new Image();
    img.src = r.url;
    sketches.set(key, img);
  }
  return sketches.get(key);
}

// ---- hit tests: each returns a distance (smaller is nearer) or -1
function pointTest(p, radius) {
  return (sx, sy) => { const d = Math.hypot(sx - p[0], sy - p[1]); return d <= radius ? d : -1; };
}
function polyTest(pts, slop) {
  return (sx, sy) => {
    let inside = false;
    for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
      const [xi, yi] = pts[i], [xj, yj] = pts[j];
      if ((yi > sy) !== (yj > sy) && sx < (xj - xi) * (sy - yi) / (yj - yi) + xi) inside = !inside;
    }
    if (inside) { const a = Math.abs(area(pts)); return 20 + Math.min(1000, a / 40); }   // smaller shapes win
    // near the edge counts too, so thin areas can be grabbed
    let best = 1e9;
    for (let i = 0; i < pts.length; i++) best = Math.min(best, segDist(sx, sy, pts[i], pts[(i + 1) % pts.length]));
    return best <= (slop || 5) ? 10 + best : -1;
  };
}
function area(pts) { let a = 0; for (let i = 0; i < pts.length; i++) { const p = pts[i], q = pts[(i + 1) % pts.length]; a += p[0] * q[1] - q[0] * p[1]; } return a / 2; }
function segDist(x, y, a, b) {
  const dx = b[0] - a[0], dy = b[1] - a[1], l = dx * dx + dy * dy;
  const t = l ? Math.max(0, Math.min(1, ((x - a[0]) * dx + (y - a[1]) * dy) / l)) : 0;
  return Math.hypot(x - (a[0] + t * dx), y - (a[1] + t * dy));
}

// what is under the pointer: the nearest thing (handles of the selection first)
export function pick(sx, sy) {
  let best = null, bd = 1e9;
  for (const h of hits) {
    if (store.tool === "scenery" && h.ref.c !== "scenery") continue;
    const d = h.test(sx, sy);
    if (d >= 0 && d < bd + (h.order >= 6 ? -6 : 0)) { bd = d; best = h; }   // items and enemies sit on top of areas
  }
  return best ? best.ref : null;
}

export function pickHandle(sx, sy) {
  for (const h of handleList) if (Math.abs(h.s[0] - sx) <= 11 && Math.abs(h.s[1] - sy) <= 11) return h;
  return null;
}

export function inRect(x0, y0, x1, y1) {
  // the refs whose centre lies in a screen rectangle (a box selection)
  return hits.filter((h) => h.centre && h.centre[0] >= x0 && h.centre[0] <= x1 && h.centre[1] >= y0 && h.centre[1] <= y1).map((h) => h.ref);
}
