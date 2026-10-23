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

import bh_rdt as rdt
import bh_rdt_map as maps
import bh_room_patch as roompatch
from bh_patchfile import write_patch

ROOT = Path(__file__).resolve().parent.parent

EDITOR3D = r"""<script>
// The 3D view: a room's model (its objects each on a bone of the skeleton) with the room's areas and cameras drawn in it; click an
// object to select it, then move, turn, scale or hide it (the bone's numbers: they are patched into the model file in place)
const EDIT3D = (() => {
  const view = document.getElementById("view"), side = document.getElementById("side");
  const c3 = document.createElement("canvas"); c3.style.cssText = "position:absolute;inset:0;width:100%;height:100%;display:none;background:#0b0b0d";
  view.appendChild(c3);
  const gl = c3.getContext("webgl2", {antialias: true});
  const panel = document.createElement("div"); panel.style.display = "none"; side.appendChild(panel);
  const MODELS = {};           // nbd file -> {bones, orig, meshes: [{mesh, bone, count, vb, ib, pickColour}], world}
  let pending = {};            // saved model edits, applied when a model loads
  let current = null, file = null, selected = null, show = {areas: true, cameras: true, zones: true, lights: true, nodes: true, textures: true}, on = false;
  let vertexMode = false, vsel = null;   // vertex editing: the selected object's vertex number being edited
  const cam = {yaw: 0.6, pitch: 0.7, dist: 4000, target: [0, 0, 0]};
  const clone = o => JSON.parse(JSON.stringify(o));
  // ---- matrices (column-major)
  const mul = (a, b) => { const o = new Float32Array(16); for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) { let s = 0; for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k]; o[c * 4 + r] = s; } return o; };
  const ident = () => { const m = new Float32Array(16); m[0] = m[5] = m[10] = m[15] = 1; return m; };
  const trans = (x, y, z) => { const m = ident(); m[12] = x; m[13] = y; m[14] = z; return m; };
  const scal = (x, y, z) => { const m = ident(); m[0] = x; m[5] = y; m[10] = z; return m; };
  const rotX = a => { const m = ident(), c = Math.cos(a), s = Math.sin(a); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; return m; };
  const rotY = a => { const m = ident(), c = Math.cos(a), s = Math.sin(a); m[0] = c; m[2] = -s; m[8] = s; m[10] = c; return m; };
  const rotZ = a => { const m = ident(), c = Math.cos(a), s = Math.sin(a); m[0] = c; m[1] = s; m[4] = -s; m[5] = c; return m; };
  function persp(fov, aspect, n, f) { const t = 1 / Math.tan(fov / 2), m = new Float32Array(16); m[0] = t / aspect; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f); return m; }
  function lookAt(e, t, u) {
    const z = norm(sub(e, t)), x = norm(cross(u, z)), y = cross(z, x), m = ident();
    m[0] = x[0]; m[4] = x[1]; m[8] = x[2]; m[1] = y[0]; m[5] = y[1]; m[9] = y[2]; m[2] = z[0]; m[6] = z[1]; m[10] = z[2];
    m[12] = -dot(x, e); m[13] = -dot(y, e); m[14] = -dot(z, e); return m;
  }
  const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]], dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
  const norm = a => { const l = Math.hypot(...a) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };
  // a bone's local matrix: offset, then rotation about x, y, z (x applied first), then scale; the world matrix is the parent's times it
  const local = b => mul(trans(...b[3]), mul(rotZ(b[2][2]), mul(rotY(b[2][1]), mul(rotX(b[2][0]), scal(...b[1])))));
  function worlds(bones) {
    const out = new Array(bones.length);
    const get = i => { if (out[i]) return out[i]; const p = bones[i][0]; out[i] = p >= 0 && p !== i && p < bones.length ? mul(get(p), local(bones[i])) : local(bones[i]); return out[i]; };
    bones.forEach((_, i) => get(i)); return out;
  }
  // ---- GL programs
  function program(vs, fs) {
    const p = gl.createProgram();
    for (const [t, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) { const s = gl.createShader(t); gl.shaderSource(s, src); gl.compileShader(s); if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s)); gl.attachShader(p, s); }
    gl.linkProgram(p); return p;
  }
  const VS = "#version 300 es\nuniform mat4 uVP, uModel; in vec3 aPos; out vec3 vW; void main(){ vec4 w = uModel * vec4(aPos, 1.0); vW = w.xyz; gl_Position = uVP * w; }";
  const shaded = program(VS, "#version 300 es\nprecision highp float; uniform vec3 uColour; in vec3 vW; out vec4 o; void main(){ vec3 n = normalize(cross(dFdx(vW), dFdy(vW))); float l = 0.3 + 0.7 * abs(dot(n, normalize(vec3(0.4, 0.8, 0.3)))); o = vec4(uColour * l, 1.0); }");
  const TVS = "#version 300 es\nuniform mat4 uVP, uModel; in vec3 aPos; in vec2 aUV; out vec3 vW; out vec2 vUV; void main(){ vec4 w = uModel * vec4(aPos, 1.0); vW = w.xyz; vUV = aUV; gl_Position = uVP * w; }";
  const textured = program(TVS, "#version 300 es\nprecision highp float; uniform sampler2D uTex; uniform vec3 uTint; in vec3 vW; in vec2 vUV; out vec4 o; void main(){ vec4 t = texture(uTex, vUV); if (t.a < 0.35) discard; vec3 n = normalize(cross(dFdx(vW), dFdy(vW))); float l = 0.55 + 0.45 * abs(dot(n, normalize(vec3(0.4, 0.8, 0.3)))); o = vec4(t.rgb * uTint * l, 1.0); }");
  const flat = program(VS, "#version 300 es\nprecision highp float; uniform vec3 uColour; out vec4 o; void main(){ o = vec4(uColour, 1.0); }");
  const lineP = program("#version 300 es\nuniform mat4 uVP; in vec3 aPos; void main(){ gl_Position = uVP * vec4(aPos, 1.0); }", "#version 300 es\nprecision highp float; uniform vec3 uColour; out vec4 o; void main(){ o = vec4(uColour, 1.0); }");
  const pointP = program("#version 300 es\nuniform mat4 uVP, uModel; uniform float uSize; in vec3 aPos; void main(){ gl_Position = uVP * uModel * vec4(aPos, 1.0); gl_PointSize = uSize; }", "#version 300 es\nprecision highp float; uniform vec3 uColour; out vec4 o; void main(){ o = vec4(uColour, 1.0); }");
  const lineBuf = gl.createBuffer();
  let pickFbo = null, pickTex = null, pickDepth = null;
  const b64 = (s, T) => { const bin = atob(s), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return new T(u.buffer); };
  const texCache = {};
  function getTex(index) {
    if (texCache[index]) return texCache[index].tex;
    const tex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([160, 160, 170, 255]));
    texCache[index] = {tex};
    const img = new Image();
    img.onload = () => { gl.bindTexture(gl.TEXTURE_2D, tex); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img); gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT); gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT); render(); };
    img.src = "/tex?index=" + index;
    return tex;
  }
  async function load(nbd) {
    if (MODELS[nbd]) return MODELS[nbd];
    const res = await fetch("/model?file=" + encodeURIComponent(nbd)); const j = await res.json();
    if (j.error) throw new Error(j.error);
    const model = {bones: j.bones, orig: clone(j.bones), meshes: [], tids: j.tids || []};
    j.meshes.forEach((m, n) => {
      const pos = b64(m.p, Float32Array), uv = b64(m.uv, Float32Array);
      const vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb); gl.bufferData(gl.ARRAY_BUFFER, pos, gl.STATIC_DRAW);
      const ub = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, ub); gl.bufferData(gl.ARRAY_BUFFER, uv, gl.STATIC_DRAW);
      const groups = m.groups.map(g => { const idx = b64(g.i, Uint32Array); const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW); return {material: g.m, ib, count: idx.length}; });
      const id = n + 1; model.meshes.push({mesh: m.mesh, bone: m.bone, vb, ub, groups, id, pos, orig: pos.slice(), pickColour: [(id & 255) / 255, ((id >> 8) & 255) / 255, ((id >> 16) & 255) / 255], vertices: pos.length / 3});
    });
    const saved = pending[nbd];
    if (saved) for (const [k, d] of Object.entries(saved.bones || {})) { const b = model.bones[+k]; if (!b) continue; if (d.scale) b[1] = d.scale; if (d.rotation) b[2] = d.rotation; if (d.offset) b[3] = d.offset; }
    if (saved) for (const [k, moved] of Object.entries(saved.vertices || {})) { const m = model.meshes.find(x => x.mesh === +k); if (m) for (const [v, p] of Object.entries(moved)) setVertex(m, +v, p); }
    MODELS[nbd] = model; return model;
  }

  function setVertex(m, i, p) {
    m.pos.set(p, 3 * i); gl.bindBuffer(gl.ARRAY_BUFFER, m.vb); gl.bufferSubData(gl.ARRAY_BUFFER, 12 * i, m.pos.subarray(3 * i, 3 * i + 3));
  }
  // d (a difference of positions) through the inverse of the 3 x 3 part of the column-major matrix m
  function inverse3(m, d) {
    const a = [[m[0], m[4], m[8]], [m[1], m[5], m[9]], [m[2], m[6], m[10]]];
    const det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (Math.abs(det) < 1e-12) return null;
    const inv = [[(a[1][1] * a[2][2] - a[1][2] * a[2][1]) / det, (a[0][2] * a[2][1] - a[0][1] * a[2][2]) / det, (a[0][1] * a[1][2] - a[0][2] * a[1][1]) / det],
                 [(a[1][2] * a[2][0] - a[1][0] * a[2][2]) / det, (a[0][0] * a[2][2] - a[0][2] * a[2][0]) / det, (a[0][2] * a[1][0] - a[0][0] * a[1][2]) / det],
                 [(a[1][0] * a[2][1] - a[1][1] * a[2][0]) / det, (a[0][1] * a[2][0] - a[0][0] * a[2][1]) / det, (a[0][0] * a[1][1] - a[0][1] * a[1][0]) / det]];
    return [0, 1, 2].map(k => inv[k][0] * d[0] + inv[k][1] * d[1] + inv[k][2] * d[2]);
  }
  // a vertex of the selected object on the screen (device pixels from the top left) and its depth, or null behind the camera
  function project(m, i, vp) {
    const w = current.world[m.bone], x = m.pos[3 * i], y = m.pos[3 * i + 1], z = m.pos[3 * i + 2];
    const mv = mul(vp, w), cx = mv[0] * x + mv[4] * y + mv[8] * z + mv[12], cy = mv[1] * x + mv[5] * y + mv[9] * z + mv[13], cw = mv[3] * x + mv[7] * y + mv[11] * z + mv[15];
    if (cw <= 0) return null;
    return {x: (cx / cw * 0.5 + 0.5) * c3.width, y: (1 - (cy / cw * 0.5 + 0.5)) * c3.height, w: cw};
  }
  function nearestVertex(px, py) {
    const vp = viewProj(); let best = null, bd = 14 * devicePixelRatio;
    for (let i = 0; i < selected.vertices; i++) { const s = project(selected, i, vp); if (!s) continue; const d = Math.hypot(s.x - px, s.y - py); if (d < bd) { bd = d; best = i; } }
    return best;
  }
  // moves the selected vertex by a screen drag of (dx, dy) device pixels, in the plane facing the camera
  function dragVertex(dx, dy) {
    const m = selected, i = vsel, vp = viewProj(), s = project(m, i, vp); if (!s) return;
    const cp = Math.cos(cam.pitch), eye = [cam.target[0] + cam.dist * cp * Math.sin(cam.yaw), cam.target[1] + cam.dist * Math.sin(cam.pitch), cam.target[2] + cam.dist * cp * Math.cos(cam.yaw)];
    const z = norm(sub(eye, cam.target)), x = norm(cross([0, 1, 0], z)), y = cross(z, x);
    const unit = 2 * s.w * Math.tan(0.45) / c3.height;
    const dw = [0, 1, 2].map(k => (x[k] * dx - y[k] * dy) * unit), local = inverse3(current.world[m.bone], dw); if (!local) return;
    setVertex(m, i, [m.pos[3 * i] + local[0], m.pos[3 * i + 1] + local[1], m.pos[3 * i + 2] + local[2]]);
  }
  function frame(model) {
    model.world = worlds(model.bones);
    let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (const m of model.meshes) { const w = model.world[m.bone]; if (!w) continue; for (const k of [0, 1, 2]) { lo[k] = Math.min(lo[k], w[12 + k]); hi[k] = Math.max(hi[k], w[12 + k]); } }
    cam.target = [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2];
    cam.dist = Math.max(1500, Math.hypot(hi[0] - lo[0], hi[2] - lo[2]) * 0.9);
  }
  function viewProj() {
    const w = c3.width, h = c3.height, cp = Math.cos(cam.pitch);
    const eye = [cam.target[0] + cam.dist * cp * Math.sin(cam.yaw), cam.target[1] + cam.dist * Math.sin(cam.pitch), cam.target[2] + cam.dist * cp * Math.cos(cam.yaw)];
    return mul(persp(0.9, w / h, 20, 200000), lookAt(eye, cam.target, [0, 1, 0]));
  }
  const hidden = b => Math.abs(b[1][0]) < 0.001 || Math.abs(b[1][1]) < 0.001 || Math.abs(b[1][2]) < 0.001;
  function drawMeshes(prog, model, vp, picking) {
    const useTex = !picking && show.textures;
    prog = useTex ? textured : prog;
    gl.useProgram(prog);
    gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uVP"), false, vp);
    const loc = gl.getAttribLocation(prog, "aPos"), uvLoc = useTex ? gl.getAttribLocation(prog, "aUV") : -1;
    for (const m of model.meshes) {
      if (hidden(model.bones[m.bone])) continue;
      gl.uniformMatrix4fv(gl.getUniformLocation(prog, "uModel"), false, model.world[m.bone]);
      const sel = selected && selected.mesh === m.mesh;
      if (useTex) gl.uniform3fv(gl.getUniformLocation(prog, "uTint"), sel ? [1.6, 0.9, 0.4] : [1, 1, 1]);
      else gl.uniform3fv(gl.getUniformLocation(prog, "uColour"), picking ? m.pickColour : sel ? [1, 0.55, 0.1] : [0.62, 0.64, 0.7]);
      gl.bindBuffer(gl.ARRAY_BUFFER, m.vb); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 0, 0);
      if (useTex) { gl.bindBuffer(gl.ARRAY_BUFFER, m.ub); gl.enableVertexAttribArray(uvLoc); gl.vertexAttribPointer(uvLoc, 2, gl.FLOAT, false, 0, 0); }
      for (const g of m.groups) {
        if (useTex) { const tid = model.tids[g.material]; gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, getTex(tid === undefined ? 0 : tid)); gl.uniform1i(gl.getUniformLocation(prog, "uTex"), 0); }
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ib); gl.drawElements(gl.TRIANGLES, g.count, gl.UNSIGNED_INT, 0);
      }
    }
  }
  const tagColour = tag => { const h = (TAGS[tag] || ["", "#ccc"])[1]; return [parseInt(h.slice(1, 3), 16) / 255, parseInt(h.slice(3, 5), 16) / 255, parseInt(h.slice(5, 7), 16) / 255]; };
  function overlay(vp) {
    const room = DATA.find(r => r.file.replace(".rdt", ".nbd") === file); if (!room) return;
    const groups = [];
    if (show.areas) for (const a of room.areas) {
      if (!okQuad(a.quad)) continue; const pts = [];
      const y0 = a.h[0], y1 = a.h[1];
      for (let i = 0; i < 4; i++) { const p = a.quad[i], q = a.quad[(i + 1) % 4]; pts.push(p[0], y0, p[1], q[0], y0, q[1], p[0], y1, p[1], q[0], y1, q[1], p[0], y0, p[1], p[0], y1, p[1]); }
      groups.push({pts, colour: tagColour(a.tag)});
    }
    if (show.zones) for (const z of room.zones) {
      const q = z.box.slice(0, 4), pts = [], y0 = z.box[0][1], y1 = z.box[4][1];
      if (!okQuad(q.map(p => [p[0], p[2]]))) continue;
      for (let i = 0; i < 4; i++) { const p = q[i], n = q[(i + 1) % 4]; pts.push(p[0], y0, p[2], n[0], y0, n[2], p[0], y1, p[2], n[0], y1, n[2], p[0], y0, p[2], p[0], y1, p[2]); }
      groups.push({pts, colour: [0.15, 0.78, 0.85]});
    }
    if (show.lights) for (const l of room.lights) { const p = l.pos, k = 30; groups.push({pts: [p[0] - k, p[1], p[2], p[0] + k, p[1], p[2], p[0], p[1] - k, p[2], p[0], p[1] + k, p[2], p[0], p[1], p[2] - k, p[0], p[1], p[2] + k], colour: [1, 0.84, 0.3]}); }
    if (show.nodes) for (const n of room.nodes) { const p = n.pos, k = 20; groups.push({pts: [p[0] - k, p[1], p[2], p[0] + k, p[1], p[2], p[0], p[1], p[2] - k, p[0], p[1], p[2] + k], colour: [1, 0.54, 0.4]}); }
    if (show.cameras) for (const c of room.cameras) {
      if (!okCam(c)) continue; const e = c.eye, t = c.target, s = 40;
      groups.push({pts: [e[0] - s, e[1], e[2], e[0] + s, e[1], e[2], e[0], e[1] - s, e[2], e[0], e[1] + s, e[2], e[0], e[1], e[2] - s, e[0], e[1], e[2] + s, e[0], e[1], e[2], t[0], t[1], t[2]], colour: [1, 1, 0.3]});
    }
    gl.useProgram(lineP); gl.uniformMatrix4fv(gl.getUniformLocation(lineP, "uVP"), false, vp);
    const loc = gl.getAttribLocation(lineP, "aPos");
    for (const g of groups) {
      gl.uniform3fv(gl.getUniformLocation(lineP, "uColour"), g.colour);
      gl.bindBuffer(gl.ARRAY_BUFFER, lineBuf); gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(g.pts), gl.DYNAMIC_DRAW);
      gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 0, 0); gl.drawArrays(gl.LINES, 0, g.pts.length / 3);
    }
  }
  function drawVertices(vp) {
    gl.useProgram(pointP); gl.uniformMatrix4fv(gl.getUniformLocation(pointP, "uVP"), false, vp); gl.uniformMatrix4fv(gl.getUniformLocation(pointP, "uModel"), false, current.world[selected.bone]);
    const loc = gl.getAttribLocation(pointP, "aPos"); gl.bindBuffer(gl.ARRAY_BUFFER, selected.vb); gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 0, 0);
    gl.uniform1f(gl.getUniformLocation(pointP, "uSize"), 6); gl.uniform3fv(gl.getUniformLocation(pointP, "uColour"), [0.1, 1, 0.2]); gl.drawArrays(gl.POINTS, 0, selected.vertices);
    if (vsel !== null) { gl.uniform1f(gl.getUniformLocation(pointP, "uSize"), 11); gl.uniform3fv(gl.getUniformLocation(pointP, "uColour"), [1, 0.2, 0.2]); gl.drawArrays(gl.POINTS, vsel, 1); }
  }
  function render() {
    if (!on || !current) return;
    const dpr = devicePixelRatio, w = Math.floor(view.clientWidth * dpr), h = Math.floor(view.clientHeight * dpr);
    if (c3.width !== w || c3.height !== h) { c3.width = w; c3.height = h; }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null); gl.viewport(0, 0, w, h);
    gl.clearColor(0.04, 0.04, 0.05, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT); gl.enable(gl.DEPTH_TEST);
    current.world = worlds(current.bones);
    const vp = viewProj();
    drawMeshes(shaded, current, vp, false);
    gl.disable(gl.DEPTH_TEST); overlay(vp); if (vertexMode && selected) drawVertices(vp);
  }
  function pickAt(px, py) {
    const w = c3.width, h = c3.height;
    if (!pickFbo || pickFbo.w !== w || pickFbo.h !== h) {
      pickFbo = gl.createFramebuffer(); pickFbo.w = w; pickFbo.h = h; gl.bindFramebuffer(gl.FRAMEBUFFER, pickFbo);
      pickTex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, pickTex); gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, w, h, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, pickTex, 0);
      pickDepth = gl.createRenderbuffer(); gl.bindRenderbuffer(gl.RENDERBUFFER, pickDepth); gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16, w, h);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, pickDepth);
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, pickFbo); gl.viewport(0, 0, w, h); gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT); gl.enable(gl.DEPTH_TEST);
    drawMeshes(flat, current, viewProj(), true);
    const px4 = new Uint8Array(4); gl.readPixels(px, h - 1 - py, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px4); gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const id = px4[0] | (px4[1] << 8) | (px4[2] << 16);
    return current.meshes.find(m => m.id === id) || null;
  }
  // ---- interaction
  let drag = null;
  const canvasXY = e => { const r = c3.getBoundingClientRect(); return [(e.clientX - r.left) * devicePixelRatio, (e.clientY - r.top) * devicePixelRatio]; };
  c3.addEventListener("mousedown", e => {
    drag = {x: e.clientX, y: e.clientY, moved: false, button: e.button, shift: e.shiftKey, vertex: false};
    if (vertexMode && selected && vsel !== null && e.button === 0 && !e.shiftKey && nearestVertex(...canvasXY(e)) === vsel) drag.vertex = true;
  });
  addEventListener("mouseup", e => {
    if (drag && drag.vertex && drag.moved) { refresh(); }
    else if (drag && !drag.moved && on && current && e.target === c3) {
      const [px, py] = canvasXY(e), v = vertexMode && selected ? nearestVertex(px, py) : null;
      if (v !== null) vsel = v; else { selected = pickAt(px, py); vsel = null; }
      refresh(); render();
    }
    drag = null;
  });
  addEventListener("mousemove", e => {
    if (!drag || !on) return;
    const dx = e.clientX - drag.x, dy = e.clientY - drag.y; if (Math.abs(dx) + Math.abs(dy) > 3) drag.moved = true; if (!drag.moved) return;
    drag.x = e.clientX; drag.y = e.clientY;
    if (drag.vertex) { dragVertex(dx * devicePixelRatio, dy * devicePixelRatio); render(); return; }
    if (drag.button === 2 || drag.shift) {
      const k = cam.dist * 0.0015, cy = Math.cos(cam.yaw), sy = Math.sin(cam.yaw);
      cam.target[0] += (-dx * cy) * k; cam.target[2] += (dx * sy) * k; cam.target[1] += dy * k;
    } else { cam.yaw -= dx * 0.008; cam.pitch = Math.max(-1.5, Math.min(1.5, cam.pitch + dy * 0.008)); }
    render();
  });
  c3.addEventListener("contextmenu", e => e.preventDefault());
  c3.addEventListener("wheel", e => { e.preventDefault(); cam.dist = Math.max(100, Math.min(120000, cam.dist * Math.exp(e.deltaY * 0.0015))); render(); }, {passive: false});
  addEventListener("resize", () => render());
  // ---- the panel
  function numbers(label, values, set, step) {
    const row = document.createElement("div"); row.className = "row"; row.append(label + " ");
    values.forEach((v, k) => { const i = document.createElement("input"); i.type = "number"; i.step = step; i.style.width = "64px"; i.value = +v.toFixed(4); i.onchange = () => { set(k, +i.value); render(); refresh(); }; row.appendChild(i); row.append(" "); });
    return row;
  }
  function moveWorld(bone, d) {
    const b = current.bones[bone], local = inverse3(b[0] >= 0 ? current.world[b[0]] : ident(), d);   // the world delta as the parent's own
    if (local) for (let k = 0; k < 3; k++) b[3][k] += local[k];
  }
  function diffsModels() {
    const out = {};
    for (const [nbd, m] of Object.entries(MODELS)) {
      const bones = {}, vertices = {};
      m.bones.forEach((b, k) => { const o = m.orig[k], d = {}; if (JSON.stringify(b[1]) !== JSON.stringify(o[1])) d.scale = b[1]; if (JSON.stringify(b[2]) !== JSON.stringify(o[2])) d.rotation = b[2]; if (JSON.stringify(b[3]) !== JSON.stringify(o[3])) d.offset = b[3]; if (Object.keys(d).length) bones[k] = d; });
      for (const mesh of m.meshes) for (let i = 0; i < mesh.vertices; i++) {
        if (mesh.pos[3 * i] !== mesh.orig[3 * i] || mesh.pos[3 * i + 1] !== mesh.orig[3 * i + 1] || mesh.pos[3 * i + 2] !== mesh.orig[3 * i + 2]) (vertices[mesh.mesh] = vertices[mesh.mesh] || {})[i] = Array.from(mesh.pos.subarray(3 * i, 3 * i + 3));
      }
      const entry = {}; if (Object.keys(bones).length) entry.bones = bones; if (Object.keys(vertices).length) entry.vertices = vertices;
      if (Object.keys(entry).length) out[nbd] = entry;
    }
    return out;
  }
  function refresh() {
    panel.innerHTML = "<hr><b>3D view</b> ";
    const back = document.createElement("button"); back.textContent = "Back to the plan"; back.onclick = () => toggle(false); panel.appendChild(back);
    const sel = document.createElement("select");
    sel.style.cssText = "display:block;margin:6px 0;max-width:220px;background:#222;color:#ddd";
    DATA.forEach(r => { const o = document.createElement("option"); o.value = r.file.replace(".rdt", ".nbd"); o.textContent = r.id + " " + r.name; if (o.value === file) o.selected = true; sel.appendChild(o); });
    sel.onchange = () => openRoom(sel.value); panel.appendChild(sel);
    for (const k of ["textures", "areas", "cameras", "zones", "lights", "nodes"]) { const l = document.createElement("label"); const c = document.createElement("input"); c.type = "checkbox"; c.checked = show[k]; c.onchange = () => { show[k] = c.checked; render(); }; l.append(c, " show " + k); panel.appendChild(l); }
    const goto = document.createElement("div"); goto.className = "row"; goto.append("object # ");
    const gin = document.createElement("input"); gin.type = "number"; gin.style.width = "60px"; goto.appendChild(gin);
    const gbt = document.createElement("button"); gbt.textContent = "select"; gbt.onclick = () => selectMesh(+gin.value); goto.appendChild(gbt); panel.appendChild(goto);
    const help = document.createElement("small"); help.textContent = "drag turns, shift-drag or right-drag pans, wheel zooms, click picks an object"; panel.appendChild(help);
    if (current && selected) {
      const b = current.bones[selected.bone], o = current.orig[selected.bone];
      const info = document.createElement("div"); info.className = "row"; info.innerHTML = "<br><b>object " + selected.mesh + "</b> (bone " + selected.bone + ", " + selected.vertices + " vertices)" + (hidden(b) ? " — hidden" : ""); panel.appendChild(info);
      panel.appendChild(numbers("offset", b[3], (k, v) => b[3][k] = v, 10));
      panel.appendChild(numbers("turn (rad)", b[2], (k, v) => b[2][k] = v, 0.1));
      panel.appendChild(numbers("scale", b[1], (k, v) => b[1][k] = v, 0.1));
      const mv = document.createElement("div"); mv.className = "row"; mv.append("move ");
      for (const [label, axis] of [["x", 0], ["y", 1], ["z", 2]]) for (const d of [-100, -10, 10, 100]) {
        const bt = document.createElement("button"); bt.textContent = label + (d > 0 ? "+" : "") + d; bt.style.padding = "1px 4px";
        bt.onclick = () => { const v = [0, 0, 0]; v[axis] = d; current.world = worlds(current.bones); moveWorld(selected.bone, v); render(); refresh(); }; mv.appendChild(bt);
      }
      panel.appendChild(mv);
      const hide = document.createElement("button"); hide.textContent = hidden(b) ? "Show" : "Hide";
      hide.onclick = () => { b[1] = hidden(b) ? clone(o[1]).map(v => Math.abs(v) < 0.001 ? 1 : v) : [0.0001, 0.0001, 0.0001]; render(); refresh(); };
      const rev = document.createElement("button"); rev.textContent = "Revert object"; rev.onclick = () => { current.bones[selected.bone] = clone(o); render(); refresh(); };
      panel.append(hide, rev);
      const vm = document.createElement("label"); vm.style.display = "block"; const vc = document.createElement("input"); vc.type = "checkbox"; vc.checked = vertexMode;
      vc.onchange = () => { vertexMode = vc.checked; vsel = null; refresh(); render(); }; vm.append(vc, " edit vertices (click a green point, drag it)"); panel.appendChild(vm);
      if (vertexMode) {
        const go = document.createElement("div"); go.className = "row"; go.append("vertex # ");
        const gi = document.createElement("input"); gi.type = "number"; gi.style.width = "60px"; gi.min = 0; gi.max = selected.vertices - 1; go.appendChild(gi);
        const gb = document.createElement("button"); gb.textContent = "select"; gb.onclick = () => { const n = +gi.value; if (n >= 0 && n < selected.vertices) { vsel = n; refresh(); render(); } }; go.appendChild(gb); panel.appendChild(go);
      }
      if (vertexMode && vsel !== null) {
        const i = vsel, at = [0, 1, 2].map(k => selected.pos[3 * i + k]);
        panel.appendChild(numbers("vertex " + i + " (local)", at, (k, v) => setVertex(selected, i, [0, 1, 2].map(n => n === k ? v : selected.pos[3 * i + n])), 1));
        const rv = document.createElement("button"); rv.textContent = "Revert vertex"; rv.onclick = () => { setVertex(selected, i, selected.orig.subarray(3 * i, 3 * i + 3)); render(); refresh(); }; panel.appendChild(rv);
      }
    } else { const t = document.createElement("div"); t.className = "row"; t.textContent = "click an object to edit it"; panel.appendChild(t); }
    const changedNow = Object.values(diffsModels()).reduce((n, m) => n + Object.keys(m.bones || {}).length + Object.values(m.vertices || {}).reduce((v, mesh) => v + Object.keys(mesh).length, 0), 0);
    const save = document.createElement("button"); save.textContent = "Save patch (" + changedNow + " object and vertex changes + plan edits)";
    save.onclick = async () => { const res = await fetch("/save", {method: "POST", body: JSON.stringify(diffs())}); const body = await res.json(); msg.textContent = res.ok ? "saved: " + body.patch + "\n" + body.report.join("\n") : "not saved: " + body.error; };
    panel.append(document.createElement("br"), save);
    const msg = document.createElement("div"); msg.className = "msg"; panel.appendChild(msg);
  }
  function selectMesh(n) { if (!current) return; const m = current.meshes.find(x => x.mesh === n); if (!m) return; selected = m; const w = current.world[m.bone]; if (w) cam.target = [w[12], w[13], w[14]]; cam.dist = Math.min(cam.dist, 2500); refresh(); render(); }
  async function openRoom(nbd) {
    file = nbd; selected = null; current = null; refresh();
    try { current = await load(nbd); frame(current); } catch (err) { panel.appendChild(Object.assign(document.createElement("div"), {textContent: "could not load: " + err.message})); return; }
    refresh(); render();
  }
  function toggle(state) {
    on = state; c3.style.display = on ? "block" : "none"; cv.style.display = on ? "none" : "block"; info.style.display = on ? "none" : "";
    panel.style.display = on ? "" : "none"; document.getElementById("edit").style.display = on ? "none" : "";
    for (const el of side.children) if (el !== panel && el.id !== "edit") el.style.display = on ? "none" : "";
    if (on) { openRoom(file || DATA[0].file.replace(".rdt", ".nbd")); } else { draw(); }
  }
  // a button in the plan's panel
  const open = document.createElement("button"); open.textContent = "Open the 3D view"; open.style.cssText = "margin:6px 0;padding:3px 8px;background:#2d3a4a;color:#ddd;border:1px solid #567;border-radius:4px;cursor:pointer";
  open.onclick = () => toggle(true); side.insertBefore(open, side.children[1]);
  return {vertexScreen: i => { const s = selected && project(selected, i, viewProj()); return s && [s.x / devicePixelRatio, s.y / devicePixelRatio]; }, diffs: diffsModels, apply(models) { pending = models || {}; }, toggle, selectMesh, state: () => ({file, selected: selected && selected.mesh, bones: current && current.bones.length})};
})();
const baseDiffs3 = diffs;
diffs = function () { const d = baseDiffs3(); const m = EDIT3D.diffs(); if (Object.keys(m).length) d.models = m; return d; };
fetch("/edits").then(r => r.json()).then(e => EDIT3D.apply(e.models));
</script>
"""

EDITOR = r"""
<script>
const EDIT = true;
const OPS = __OPS__;
let MESSAGES = [];
fetch('/messages').then(r => r.json()).then(m => { MESSAGES = m; });
const MESSAGE_ARG = {85: 1, 95: 1};   // SetExamine's and SetDoorLock's second word is a message number
const ORIG = JSON.parse(JSON.stringify(DATA));
const room = (file) => DATA.find(r => r.file === file);
let selected = null, undo = [], dragging = null, snapStep = 10, savedMessage = "", scriptRoomFile = null, scriptOpen = false;
const snap = v => snapStep > 1 ? Math.round(v / snapStep) * snapStep : Math.round(v);
const clone = o => JSON.parse(JSON.stringify(o));
const panel = document.createElement("div"); panel.id = "edit"; document.getElementById("side").appendChild(panel);
const style = document.createElement("style");
style.textContent = "#edit input[type=number]{width:62px;background:#222;color:#ddd;border:1px solid #444;padding:1px 3px}#edit button{margin:2px 2px 2px 0;padding:3px 8px;background:#2d3a4a;color:#ddd;border:1px solid #567;border-radius:4px;cursor:pointer}#edit .row{margin:2px 0}#edit .msg{color:#9c9;white-space:pre-wrap;margin-top:6px}#edit b{color:#fc6}";
document.head.appendChild(style);
function itemOf(sel) {
  if (sel.kind === "area") return sel.room.areas.find(a => a.i === sel.index);
  if (sel.kind === "zone") return sel.room.zones.find(z => z.i === sel.index);
  if (sel.kind === "spoint") return sel.room.points.find(p => p.i === sel.index);
  if (sel.kind === "light") return sel.room.lights.find(l => l.i === sel.index);
  if (sel.kind === "node") return sel.room.nodes.find(n => n.i === sel.index);
  return sel.room.cameras.find(c => c.i === sel.index);
}
function origOf(sel) { const r = ORIG.find(r => r.file === sel.room.file); if (itemOf(sel).isNew) return null;
  if (sel.kind === "zone") return r.zones.find(z => z.i === sel.index);
  if (sel.kind === "spoint") return r.points.find(p => p.i === sel.index);
  if (sel.kind === "light") return r.lights.find(l => l.i === sel.index);
  if (sel.kind === "node") return r.nodes.find(n => n.i === sel.index);
  return sel.kind === "area" ? r.areas.find(a => a.i === sel.index) : r.cameras.find(c => c.i === sel.index); }
function changed(a, b) { return JSON.stringify(a) !== JSON.stringify(b); }
const COST = {area: 180, camera: 324};
function added(r) { return r.areas.filter(a => a.isNew).length * COST.area + r.cameras.filter(c => c.isNew).length * COST.camera; }
function diffs() {
  const edits = {};
  DATA.forEach((r, ri) => {
    const o = ORIG[ri], rooms = {};
    const newAreas = r.areas.filter(a => a.isNew).map(a => { const f = {}; a.fields.forEach((v, k) => { f[12 + k] = v; }); return {like: a.like, quad: a.quad, height: a.h, fields: f}; });
    const newCameras = r.cameras.filter(c => c.isNew).map(c => ({like: c.like, eye: c.eye, target: c.target, fov: c.fov, box: c.box}));
    if (newAreas.length) rooms.new_areas = newAreas;
    if (newCameras.length) rooms.new_cameras = newCameras;
    for (const c of r.cameras) {
      if (c.isNew) continue;
      const oc = o.cameras.find(x => x.i === c.i), d = {};
      if (changed(c.eye, oc.eye)) d.eye = c.eye;
      if (changed(c.target, oc.target)) d.target = c.target;
      if (changed(c.fov, oc.fov)) d.fov = c.fov;
      if (changed(c.box, oc.box)) d.box = c.box;
      if (Object.keys(d).length) (rooms.cameras = rooms.cameras || {})[c.i] = d;
      if (c.fog && oc.fog) { const f = {}; c.fog.forEach((v, k) => { if (v !== oc.fog[k]) f[k] = v; }); if (Object.keys(f).length) (rooms.fog = rooms.fog || {})[c.i] = {ints: f}; }
    }
    for (const a of r.areas) {
      if (a.isNew) continue;
      const oa = o.areas.find(x => x.i === a.i), d = {};
      if (changed(a.quad, oa.quad)) d.quad = a.quad;
      if (changed(a.h, oa.h)) d.height = a.h;
      const f = {};
      a.fields.forEach((v, k) => { if (v !== oa.fields[k]) f[12 + k] = v; });
      if (Object.keys(f).length) d.fields = f;
      if (Object.keys(d).length) (rooms.areas = rooms.areas || {})[a.i] = d;
    }
    for (const z of r.zones) { const oz = o.zones.find(x => x.i === z.i), d = {}; if (changed(z.box, oz.box)) d.box = z.box; if (z.sound !== oz.sound) d.sound = z.sound; if (Object.keys(d).length) (rooms.sound_zones = rooms.sound_zones || {})[z.i] = d; }
    { const sw = {}; r.script.forEach((v, k) => { if (v !== o.script[k]) sw[k] = v; }); if (Object.keys(sw).length) rooms.script_words = sw; }
    for (const n of r.nodes) { const on = o.nodes.find(x => x.i === n.i), d = {}; if (changed(n.pos, on.pos)) d.position = n.pos; if (n.kind !== on.kind) d.kind = n.kind; if (n.id !== on.id) d.id = n.id; const wv = {}; n.words.forEach((v, k) => { if (v !== on.words[k]) wv[k] = v; }); if (Object.keys(wv).length) d.words = wv; if (Object.keys(d).length) (rooms.nodes = rooms.nodes || {})[n.i] = d; }
    for (const l of r.lights) { const ol = o.lights.find(x => x.i === l.i), d = {}; if (changed(l.pos, ol.pos)) d.position = l.pos; if (l.att !== ol.att) d.attenuation = l.att; if (l.range !== ol.range) d.range = l.range; if (changed(l.col, ol.col)) d.colour = l.col; if (changed(l.col2, ol.col2)) d.colour2 = l.col2; if (Object.keys(d).length) (rooms.lights = rooms.lights || {})[l.i] = d; }
    for (const p of r.points) { const op = o.points.find(x => x.i === p.i), d = {}; if (changed(p.pos, op.pos)) d.position = p.pos; if (p.kind !== op.kind) d.kind = p.kind; if (Object.keys(d).length) (rooms.sound_points = rooms.sound_points || {})[p.i] = d; }
    if (rooms.cameras || rooms.areas || rooms.fog || rooms.new_areas || rooms.new_cameras || rooms.sound_zones || rooms.sound_points || rooms.lights || rooms.script_words || rooms.nodes) edits[r.file] = rooms;
  });
  return {rooms: edits};
}
function apply(edits) {
  for (const [file, spec] of Object.entries(edits.rooms || {})) {
    const r = room(file); if (!r) continue;
    for (const [i, d] of Object.entries(spec.sound_zones || {})) { const z = r.zones.find(x => x.i === +i); if (z) { if (d.box) z.box = d.box; if (d.sound !== undefined) z.sound = d.sound; } }
    for (const [i, d] of Object.entries(spec.nodes || {})) { const n = r.nodes.find(x => x.i === +i); if (n) { if (d.position) n.pos = d.position; if (d.kind !== undefined) n.kind = d.kind; if (d.id !== undefined) n.id = d.id; for (const [k, v] of Object.entries(d.words || {})) n.words[+k] = v; } }
    for (const [i, v] of Object.entries(spec.script_words || {})) { r.script[+i] = v; }
    for (const [i, d] of Object.entries(spec.lights || {})) { const l = r.lights.find(x => x.i === +i); if (l) { if (d.position) l.pos = d.position; if (d.attenuation !== undefined) l.att = d.attenuation; if (d.range !== undefined) l.range = d.range; if (d.colour) l.col = d.colour; if (d.colour2) l.col2 = d.colour2; } }
    for (const [i, d] of Object.entries(spec.sound_points || {})) { const p = r.points.find(x => x.i === +i); if (p) { if (d.position) p.pos = d.position; if (d.kind !== undefined) p.kind = d.kind; } }
    for (const n of spec.new_areas || []) addArea(r, n.like, n);
    for (const n of spec.new_cameras || []) addCamera(r, n.like, n);
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
function addArea(r, like, values) {
  const base = r.areas.find(a => a.i === like); if (!base) return null;
  const n = clone(base); delete n.arrival; n.isNew = true; n.like = like; n.i = r.areas.length;
  if (values && values.quad) { n.quad = clone(values.quad); n.h = clone(values.height); Object.entries(values.fields || {}).forEach(([k, v]) => n.fields[+k - 12] = v); }
  else n.quad = n.quad.map(p => [p[0] + 60, p[1] + 60]);
  r.areas.push(n); return n;
}
function addCamera(r, like, values) {
  const base = r.cameras.find(c => c.i === like); if (!base) return null;
  const n = clone(base); n.isNew = true; n.like = like; n.i = r.cameras.length;
  if (values && values.eye) { n.eye = clone(values.eye); n.target = clone(values.target); n.fov = clone(values.fov); n.box = clone(values.box); }
  else { n.eye = [n.eye[0] + 100, n.eye[1], n.eye[2] + 100]; n.target = [n.target[0] + 100, n.target[1], n.target[2] + 100]; n.box = n.box.map(p => [p[0] + 100, p[1], p[2] + 100]); }
  r.cameras.push(n); return n;
}
function removeNew(sel) {
  const r = sel.room, list = sel.kind === "area" ? r.areas : r.cameras;
  const at = list.findIndex(x => x.i === sel.index); if (at < 0 || !list[at].isNew) return;
  list.splice(at, 1); list.forEach((x, k) => { if (x.isNew) x.i = k; });
  selected = null;
}
function remember() { if (selected) undo.push({sel: selected, value: clone(itemOf(selected))}); if (undo.length > 200) undo.shift(); }
function pointerWorld(e) { const [mx, my] = toCanvas(e); return [(mx - cv.width / 2) / view.s + view.x, -(my - cv.height / 2) / view.s + view.y]; }
function handles() {
  if (!selected) return [];
  const it = itemOf(selected), out = [];
  if (selected.kind === "area") it.quad.forEach((p, k) => out.push({k: "corner", n: k, x: p[0], z: p[1]}));
  else if (selected.kind === "zone") it.box.slice(0, 4).forEach((p, k) => out.push({k: "zonecorner", n: k, x: p[0], z: p[2]}));
  else if (selected.kind === "spoint") out.push({k: "spoint", x: it.pos[0], z: it.pos[2]});
  else if (selected.kind === "light") out.push({k: "light", x: it.pos[0], z: it.pos[2]});
  else if (selected.kind === "node") out.push({k: "node", x: it.pos[0], z: it.pos[2]});
  else {
    out.push({k: "eye", x: it.eye[0], z: it.eye[2]}, {k: "aim", x: it.target[0], z: it.target[2]});
    it.box.slice(0, 4).forEach((p, k) => out.push({k: "boxcorner", n: k, x: p[0], z: p[2]}));
  }
  return out;
}
layers.sound = true;
const baseDraw = draw;
draw = function () {
  baseDraw();
  if (layers.sound) for (const r of DATA) {
    for (const z of r.zones) {
      const q = z.box.slice(0, 4).map(p => [p[0], p[2]]); if (!okQuad(q)) continue;
      ctx.beginPath(); q.forEach((p, i) => i ? ctx.lineTo(sx(p[0]), sy(p[1])) : ctx.moveTo(sx(p[0]), sy(p[1]))); ctx.closePath();
      ctx.fillStyle = "#26c6da33"; ctx.fill(); ctx.strokeStyle = "#26c6da"; ctx.setLineDash([3, 3]); ctx.stroke(); ctx.setLineDash([]);
      addShape("zone", q, "room " + r.id + " sound zone " + z.i + " (floor sound " + z.sound + ")", {room: r, zone: z});
    }
    for (const n of r.nodes) {
      const x = sx(n.pos[0]), y = sy(n.pos[2]), col = ["#bbb", "#ff8a65", "#4dd0e1", "#ba68c8", "#aed581"][n.kind] || "#bbb";
      ctx.fillStyle = col; ctx.fillRect(x - 3, y - 3, 6, 6);
      addShape("node", [[n.pos[0], n.pos[2]]], "room " + r.id + " AI node " + n.i + " (kind " + n.kind + ", id " + n.id + ")", {room: r, node: n});
    }
    for (const l of r.lights) {
      const x = sx(l.pos[0]), y = sy(l.pos[2]), rad = l.range * view.s;
      ctx.strokeStyle = "#ffd54f88"; ctx.beginPath(); ctx.arc(x, y, Math.max(3, rad), 0, 7); ctx.stroke();
      ctx.fillStyle = "#ffd54f"; ctx.beginPath(); ctx.arc(x, y, 4, 0, 7); ctx.fill();
      addShape("light", [[l.pos[0], l.pos[2]]], "room " + r.id + " character light " + l.i + " (range " + l.range + ")", {room: r, light: l});
    }
    for (const p of r.points) {
      const x = sx(p.pos[0]), y = sy(p.pos[2]); ctx.strokeStyle = "#26c6da"; ctx.beginPath(); ctx.moveTo(x - 5, y); ctx.lineTo(x, y - 5); ctx.lineTo(x + 5, y); ctx.lineTo(x, y + 5); ctx.closePath(); ctx.stroke();
      addShape("spoint", [[p.pos[0], p.pos[2]]], "room " + r.id + " sound source " + p.i, {room: r, point: p});
    }
  }
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
  if (s && (s.kind === "area" || s.kind === "cam" || s.kind === "box" || s.kind === "zone" || s.kind === "spoint" || s.kind === "light" || s.kind === "node")) {
    const sel = s.kind === "node" ? {kind: "node", room: s.extra.room, index: s.extra.node.i} : s.kind === "light" ? {kind: "light", room: s.extra.room, index: s.extra.light.i} : s.kind === "zone" ? {kind: "zone", room: s.extra.room, index: s.extra.zone.i} : s.kind === "spoint" ? {kind: "spoint", room: s.extra.room, index: s.extra.point.i}
      : {kind: s.kind === "area" ? "area" : "cam", room: s.extra.room, index: s.kind === "area" ? s.extra.area.i : s.extra.cam.i};
    const same = selected && selected.kind === sel.kind && selected.room === sel.room && selected.index === sel.index;
    selected = sel; refreshPanel(); draw();
    if (sel.kind === "area" || sel.kind === "zone" || sel.kind === "spoint" || sel.kind === "light" || sel.kind === "node") { remember(); dragging = {body: true, last: pointerWorld(e)}; e.stopImmediatePropagation(); e.preventDefault(); }
    return;
  }
  selected = null; refreshPanel(); draw();
}, true);
addEventListener("mousemove", e => {
  if (!dragging) return;
  const w = pointerWorld(e), it = itemOf(selected);
  if (dragging.body) {
    const dx = snap(w[0]) - snap(dragging.last[0]), dz = snap(w[1]) - snap(dragging.last[1]);
    if (dx || dz) {
      if (selected.kind === "area") it.quad = it.quad.map(p => [p[0] + dx, p[1] + dz]);
      else if (selected.kind === "zone") it.box = it.box.map(p => [p[0] + dx, p[1], p[2] + dz]);
      else it.pos = [it.pos[0] + dx, it.pos[1], it.pos[2] + dz];  // a sound source or a light
      dragging.last = w;
    }
  } else {
    const h = dragging.h, x = snap(w[0]), z = snap(w[1]);
    if (h.k === "corner") it.quad[h.n] = [x, z];
    else if (h.k === "eye") { it.eye = [x, it.eye[1], z]; }
    else if (h.k === "aim") { it.target = [x, it.target[1], z]; }
    else if (h.k === "zonecorner") { it.box[h.n] = [x, it.box[h.n][1], z]; it.box[h.n + 4] = [x, it.box[h.n + 4][1], z]; }
    else if (h.k === "spoint" || h.k === "light" || h.k === "node") { it.pos = [x, it.pos[1], z]; }
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
  const total = diffs(), n = Object.values(total.rooms).reduce((a, r) => a + Object.keys(r.cameras || {}).length + Object.keys(r.areas || {}).length + (r.new_areas || []).length + (r.new_cameras || []).length + Object.keys(r.sound_zones || {}).length + Object.keys(r.sound_points || {}).length + Object.keys(r.lights || {}).length + Object.keys(r.script_words || {}).length + Object.keys(r.nodes || {}).length, 0);
  const info = document.createElement("div"); info.className = "row";
  if (selected) {
    const it = itemOf(selected), r = selected.room;
    if (selected.kind === "node") {
      info.innerHTML = "room " + r.id + " AI node " + it.i; panel.appendChild(info);
      const p = document.createElement("div"); p.className = "row"; p.append("position ", num("x", it.pos[0], v => it.pos[0] = v), num("y", it.pos[1], v => it.pos[1] = v), num("z", it.pos[2], v => it.pos[2] = v)); panel.appendChild(p);
      const q = document.createElement("div"); q.className = "row"; q.append("kind ", num("", it.kind, v => it.kind = v), " id ", num("", it.id, v => it.id = v)); panel.appendChild(q);
      const w = document.createElement("div"); w.className = "row"; w.append("words: ");
      it.words.forEach((v, k) => w.appendChild(num(String(k), v, x => it.words[k] = x))); panel.appendChild(w);
      const note = document.createElement("small"); note.textContent = "the AI's navigation nodes: the game finds the one nearest a position (by id); the words are not decoded (neighbours and costs, by their shape)"; panel.appendChild(note);
      const revert = document.createElement("button"); revert.textContent = "Revert this"; revert.onclick = () => { remember(); const o = clone(origOf(selected)); const target = itemOf(selected); Object.keys(o).forEach(k => target[k] = o[k]); refreshPanel(); draw(); }; panel.appendChild(revert);
    } else if (selected.kind === "light") {
      info.innerHTML = "room " + r.id + " character light " + it.i; panel.appendChild(info);
      const p = document.createElement("div"); p.className = "row"; p.append("position ", num("x", it.pos[0], v => it.pos[0] = v), num("y", it.pos[1], v => it.pos[1] = v), num("z", it.pos[2], v => it.pos[2] = v)); panel.appendChild(p);
      const q = document.createElement("div"); q.className = "row"; q.append("range ", num("", it.range, v => it.range = v), " attenuation ", num("", it.att, v => it.att = v)); panel.appendChild(q);
      const c1 = document.createElement("div"); c1.className = "row"; c1.append("colour ", num("r", it.col[0], v => it.col[0] = v), num("g", it.col[1], v => it.col[1] = v), num("b", it.col[2], v => it.col[2] = v)); panel.appendChild(c1);
      const c2 = document.createElement("div"); c2.className = "row"; c2.append("second colour ", num("r", it.col2[0], v => it.col2[0] = v), num("g", it.col2[1], v => it.col2[1] = v), num("b", it.col2[2], v => it.col2[2] = v)); panel.appendChild(c2);
      const note = document.createElement("small"); note.textContent = "lights the characters only (0 to 4 for each colour)"; panel.appendChild(note);
      const revert = document.createElement("button"); revert.textContent = "Revert this"; revert.onclick = () => { remember(); const o = clone(origOf(selected)); const target = itemOf(selected); Object.keys(o).forEach(k => target[k] = o[k]); refreshPanel(); draw(); }; panel.appendChild(revert);
    } else if (selected.kind === "zone" || selected.kind === "spoint") {
      info.innerHTML = selected.kind === "zone" ? "room " + r.id + " sound zone " + it.i : "room " + r.id + " sound source " + it.i + " (kind " + it.kind + ")";
      panel.appendChild(info);
      if (selected.kind === "zone") {
        const d = document.createElement("div"); d.className = "row"; d.append("floor sound ", num("", it.sound, v => it.sound = v)); panel.appendChild(d);
        const h = document.createElement("div"); h.className = "row"; h.append("height ", num("from", it.box[0][1], v => { for (let k = 0; k < 4; k++) it.box[k][1] = v; }), num("to", it.box[4][1], v => { for (let k = 4; k < 8; k++) it.box[k][1] = v; })); panel.appendChild(h);
        it.box.slice(0, 4).forEach((p, k) => { const row = document.createElement("div"); row.className = "row"; row.append("corner " + k + " ", num("x", p[0], v => { it.box[k][0] = v; it.box[k + 4][0] = v; }), num("z", p[2], v => { it.box[k][2] = v; it.box[k + 4][2] = v; })); panel.appendChild(row); });
      } else {
        const d = document.createElement("div"); d.className = "row"; d.append("position ", num("x", it.pos[0], v => it.pos[0] = v), num("y", it.pos[1], v => it.pos[1] = v), num("z", it.pos[2], v => it.pos[2] = v)); panel.appendChild(d);
        const k = document.createElement("div"); k.className = "row"; k.append("kind ", num("", it.kind, v => it.kind = v)); panel.appendChild(k);
      }
      const revert = document.createElement("button"); revert.textContent = "Revert this"; revert.onclick = () => { remember(); const o = clone(origOf(selected)); const target = itemOf(selected); Object.keys(o).forEach(k => target[k] = o[k]); refreshPanel(); draw(); }; panel.appendChild(revert);
    } else {
    info.innerHTML = (selected.kind === "area" ? "room " + r.id + " area " + it.i + " (tag " + it.tag + ", subtype " + it.sub + ")" : "room " + r.id + " camera " + it.i + " (type " + it.kind + ")") + (it.isNew ? " <b>new, copy of " + it.like + "</b>" : "") + "<br><small>room has " + (r.capacity - added(r)) + " bytes free to add into</small>";
    panel.appendChild(info);
    const rows = [];
    if (selected.kind === "area") {
      it.quad.forEach((p, k) => { const d = document.createElement("div"); d.className = "row"; d.append("corner " + k + " ", num("x", p[0], v => it.quad[k] = [v, it.quad[k][1]]), num("z", p[1], v => it.quad[k] = [it.quad[k][0], v])); panel.appendChild(d); });
      const d = document.createElement("div"); d.className = "row"; d.append("height ", num("from", it.h[0], v => it.h = [v, it.h[1]]), num("to", it.h[1], v => it.h = [it.h[0], v])); panel.appendChild(d);
      const f = document.createElement("div"); f.className = "row"; f.append("fields (int 12 on): ");
      it.fields.forEach((v, k) => { if (v !== 0 || (!it.isNew && ORIG.find(o => o.file === r.file).areas.find(a => a.i === it.i).fields[k] !== 0)) f.appendChild(num(String(12 + k), v, x => it.fields[k] = x)); });
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
    if (!it.isNew) {
      const revert = document.createElement("button"); revert.textContent = "Revert this";
      revert.onclick = () => { remember(); const o = clone(origOf(selected)); const target = itemOf(selected); Object.keys(o).forEach(k => target[k] = o[k]); refreshPanel(); draw(); };
      panel.appendChild(revert);
    }
    const kindName = selected.kind === "area" ? "area" : "camera";
    const free = r.capacity - added(r), cost = COST[kindName];
    const add = document.createElement("button"); add.textContent = "Add " + kindName + " like this (" + cost + " of " + free + " bytes free)";
    add.disabled = free < cost;
    add.onclick = () => { const n = selected.kind === "area" ? addArea(r, it.isNew ? it.like : it.i) : addCamera(r, it.isNew ? it.like : it.i); if (n) { selected = {kind: selected.kind, room: r, index: n.i}; } refreshPanel(); draw(); };
    panel.appendChild(add);
    if (it.isNew) { const rm = document.createElement("button"); rm.textContent = "Remove this new " + kindName; rm.onclick = () => { removeNew(selected); refreshPanel(); draw(); }; panel.appendChild(rm); }
    }
  } else { info.textContent = "click a shape"; panel.appendChild(info); }
  const sd = document.createElement("details"); sd.style.marginTop = "6px";
  const sroom = DATA.find(x => x.file === scriptRoomFile) || (selected ? selected.room : DATA[0]);
  const ssum = document.createElement("summary"); ssum.textContent = "Script (room " + sroom.id + ")"; sd.appendChild(ssum); sd.open = scriptOpen; sd.ontoggle = () => { scriptOpen = sd.open; };
  if (sd.open) {
    const sel = document.createElement("select"); sel.style.cssText = "max-width:220px;background:#222;color:#ddd";
    DATA.forEach(x => { const o = document.createElement("option"); o.value = x.file; o.textContent = x.id + " " + x.name; if (x === sroom) o.selected = true; sel.appendChild(o); });
    sel.onchange = () => { scriptRoomFile = sel.value; refreshPanel(); }; sd.appendChild(sel);
    const note = document.createElement("small"); note.textContent = "Records by the width rule (class 1, 2, 3 = 2, 4, 8 words); meanings are not decoded. Numbers are the record's words after its first; w is the word's place."; sd.appendChild(note);
    const w = sroom.script; const start = Math.floor(w[8] / 4), end = Math.min(Math.floor(w[0] / 4), w.length);
    let i = start; const list = document.createElement("div"); list.style.cssText = "font:11px ui-monospace,Menlo,monospace;max-height:320px;overflow:auto;margin-top:4px";
    while (i < end) {
      const top = (w[i] >>> 24); if (top < 1 || top > 3) { i++; continue; }
      const width = 2 ** top, row = document.createElement("div"); row.style.marginBottom = "2px";
      const opn = w[i] & 0xFFFFFF, known = OPS[String(opn)];
      const lab = document.createElement("span"); lab.textContent = "w" + i + " " + (known ? known[0] : "op 0x" + opn.toString(16)) + " "; lab.title = known ? known[1] : "not read in the game's code yet"; lab.style.color = known ? "#9cf" : "#bbb"; row.appendChild(lab);
      const msgArg = MESSAGE_ARG[opn];
      let hint = null; if (msgArg !== undefined) { hint = document.createElement("small"); hint.style.cssText = "display:block;color:#9c9"; }
      const showHint = (idx) => { if (hint) { const lines = MESSAGES[sroom.script[idx] - 1]; hint.textContent = "message " + sroom.script[idx] + ": " + (lines ? lines.join(" / ") : "(none)"); } };
      for (let k = 1; k < width && i + k < w.length; k++) { const idx = i + k; if (hint && k === 1 + msgArg) showHint(idx); const inp = document.createElement("input"); inp.type = "number"; inp.value = w[idx]; inp.style.cssText = "width:70px;background:#222;color:#ddd;border:1px solid #444;padding:0 2px"; inp.onchange = () => { sroom.script[idx] = +inp.value; refreshPanel(); }; row.appendChild(inp); }
      if (hint) row.appendChild(hint);
      list.appendChild(row); i += width;
    }
    sd.appendChild(list);
  }
  panel.appendChild(sd);
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


def model_json(game, scenario, file):
    """A room's model for the 3D view as JSON: the skeleton ([parent, scale xyz, rotation xyz, offset xyz]) and each mesh's bone (i + 2),
    positions (little-endian floats) and triangle indices (uint32), both base64"""
    import base64
    import struct

    import bh_archive as archive
    import bh_nbd as nbd

    disc = archive.Archive(game)
    node = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            node = disc.open(f"{dat}/r{scenario:03d}.afs/{file}")
            break
        except FileNotFoundError:
            continue
    if node is None:
        return {"error": f"{file}: not on the disc"}
    data = node.read()
    table = nbd.chunks(data)
    amo = next(c for c in table if c[0] == "AMO")
    ahi = next(c for c in table if c[0] == "AHI")
    bones = [[b[0], list(b[1][:3]), list(b[2][:3]), list(b[3][:3])] for b in nbd.skeleton(data, ahi[1], ahi[2])]
    meshes = []
    for index, mesh in enumerate(nbd.meshes(data, amo[1], amo[2])):
        import math

        textured = [t for t in mesh.textured_triangles() if max(t[:3]) < len(mesh.positions)
                    and max(math.dist(mesh.positions[t[i]], mesh.positions[t[(i + 1) % 3]]) for i in range(3)) < 6000]
        if not mesh.positions or not textured:
            continue
        flat = [c for p in mesh.positions for c in p]
        uvs = [c for uv in (mesh.uvs if len(mesh.uvs) == len(mesh.positions) else [(0.0, 0.0)] * len(mesh.positions)) for c in uv]
        groups = {}
        for a, b, c, material in textured:
            groups.setdefault(material, []).extend((a, b, c))
        meshes.append({"mesh": index, "bone": index + 2,
                       "p": base64.b64encode(struct.pack(f"<{len(flat)}f", *flat)).decode(),
                       "uv": base64.b64encode(struct.pack(f"<{len(uvs)}f", *uvs)).decode(),
                       "groups": [{"m": material, "i": base64.b64encode(struct.pack(f"<{len(ids)}I", *ids)).decode()} for material, ids in groups.items()]})
    tid = next((c for c in table if c[0] == "TID"), None)
    tids = list(struct.unpack_from(f"<{tid[2] // 4}I", data, tid[1])) if tid else []
    return {"bones": bones, "meshes": meshes, "tids": tids}


def messages_json(game, scenario):
    """The stage's messages as text, in the game's numbering (message N is the file's record N - 1, measured), English where the translation has it: [[line, ...], ...]"""
    import re
    import struct

    import bh_archive as archive
    import bh_text

    translations = {}
    path = ROOT / "translations" / game.replace("file", "file") / "rooms.tsv"
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            parts = line.split("\t")
            if len(parts) >= 2:
                translations[parts[0]] = parts[1]
    disc = archive.Archive(game)
    node = None
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            node = disc.open(f"{dat}/r{scenario:03d}.afs/r{scenario:03d}.bin")
            break
        except FileNotFoundError:
            continue
    if node is None:
        return []
    data = node.read()
    end, arrays = struct.unpack_from("<II", data, 0)
    records = [struct.unpack_from("<II", data, 8 + 8 * i) for i in range((arrays - 8) // 8)]
    out = []
    for count, offset in records:
        lines = []
        for k in range(count):
            start = struct.unpack_from("<I", data, offset + 4 * k)[0]
            try:
                raw = data[start:data.index(b"\r\n\0", start)]
            except ValueError:
                continue
            text = translations.get(bh_text.unit_id(raw)) or raw.decode("cp932", "replace")
            lines.append(re.sub(r"<[^>]*>", "", text))
        out.append(lines)
    return out


def texture_png(game, scenario, index):
    """The scenario's texture number `index` (the index into the pack's first members, which r0xx_h.bin counts as the textures) as PNG bytes, or None"""
    import io

    import bh_archive as archive
    import bh_sld
    import tm2

    disc = archive.Archive(game)
    for dat in ("/NETBIO00.DAT", "/NETBIO01.DAT"):
        try:
            members = disc.open(f"{dat}/r{scenario:03d}.afs").members()
        except FileNotFoundError:
            continue
        if not 0 <= index < len(members):
            return None
        pictures = tm2.pictures(bh_sld.unpack(members[index][1].read()))
        buffer = io.BytesIO()
        pictures[0].image().convert("RGBA").save(buffer, "PNG")
        return buffer.getvalue()
    return None


def serve(game, scenario, names, port, out):
    data = maps.build(game, scenario, names, False)
    ops = json.dumps({str(k): list(v) for k, v in rdt.OPS.items()})
    page = maps.render(game, scenario, data, (EDITOR + EDITOR3D).replace("__OPS__", ops))
    out.mkdir(parents=True, exist_ok=True)
    edits_path, patch_path = out / f"room-edits-{game}.json", out / f"rooms-{game}.patch"
    mine = {r["file"].lower() for r in data}
    models = {}

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
            elif self.path.startswith("/tex?"):
                index = int(self.path.split("=", 1)[1])
                if ("tex", index) not in models:
                    models[("tex", index)] = texture_png(game, scenario, index)
                png = models[("tex", index)]
                self.send(200 if png else 404, png or b"", "image/png")
            elif self.path == "/messages":
                if "messages" not in models:
                    models["messages"] = messages_json(game, scenario)
                self.send(200, json.dumps(models["messages"]))
            elif self.path.startswith("/model?"):
                file = self.path.split("=", 1)[1]
                if file not in models:
                    models[file] = model_json(game, scenario, file)
                self.send(200, json.dumps(models[file]))
            elif self.path == "/edits":
                self.send(200, edits_path.read_text() if edits_path.exists() else '{"rooms": {}, "models": {}}')
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
                prefix = f"r{scenario:03d}"
                kept_models = {}
                if edits_path.exists():
                    kept_models = {k: v for k, v in json.loads(edits_path.read_text()).get("models", {}).items() if not k.lower().startswith(prefix)}
                edits = {"rooms": {**kept, **mine_now.get("rooms", {})}, "models": {**kept_models, **mine_now.get("models", {})}}
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
