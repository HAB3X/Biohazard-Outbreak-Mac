// The models: each room's mesh objects on their bones, drawn with WebGL2 (shaded by height on the plan, textured in 3D), and the floors read back from them
// so things can be set down on the ground.
import { mul } from "./camera.js";

const b64 = (s, T) => { const bin = atob(s), u = new Uint8Array(bin.length); for (let i = 0; i < bin.length; i++) u[i] = bin.charCodeAt(i); return new T(u.buffer); };

// ---- bone matrices (column-major): offset, rotation about x, y, z (x first), scale; a bone's world matrix is its parent's times its own
const ident = () => { const m = new Float32Array(16); m[0] = m[5] = m[10] = m[15] = 1; return m; };
const trans = (x, y, z) => { const m = ident(); m[12] = x; m[13] = y; m[14] = z; return m; };
const scal = (x, y, z) => { const m = ident(); m[0] = x; m[5] = y; m[10] = z; return m; };
const rotX = (a) => { const m = ident(), c = Math.cos(a), s = Math.sin(a); m[5] = c; m[6] = s; m[9] = -s; m[10] = c; return m; };
const rotY = (a) => { const m = ident(), c = Math.cos(a), s = Math.sin(a); m[0] = c; m[2] = -s; m[8] = s; m[10] = c; return m; };
const rotZ = (a) => { const m = ident(), c = Math.cos(a), s = Math.sin(a); m[0] = c; m[1] = s; m[4] = -s; m[5] = c; return m; };
const local = (b) => mul(trans(...b[3]), mul(rotZ(b[2][2]), mul(rotY(b[2][1]), mul(rotX(b[2][0]), scal(...b[1])))));
function worlds(bones) {
  const out = new Array(bones.length);
  const get = (i) => {
    if (out[i]) return out[i];
    const p = bones[i][0];
    out[i] = p >= 0 && p !== i && p < bones.length ? mul(get(p), local(bones[i])) : local(bones[i]);
    return out[i];
  };
  bones.forEach((_, i) => get(i));
  return out;
}
const hidden = (b) => Math.abs(b[1][0]) < 0.001 || Math.abs(b[1][1]) < 0.001 || Math.abs(b[1][2]) < 0.001;

const VS = `#version 300 es
uniform mat4 uVP, uModel; in vec3 aPos; in vec2 aUV; in vec4 aCol; out vec3 vW; out vec2 vUV; out vec4 vCol;
void main(){ vec4 w = uModel * vec4(aPos, 1.0); vW = w.xyz; vUV = aUV; vCol = aCol; gl_Position = uVP * w; }`;
const FS_FLAT = `#version 300 es
precision highp float; uniform vec2 uHeight; uniform float uDim; in vec3 vW; out vec4 o;
void main(){
  vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
  float l = 0.45 + 0.55 * abs(dot(n, normalize(vec3(0.35, 0.85, 0.4))));
  float h = clamp((vW.y - uHeight.x) / max(1.0, uHeight.y - uHeight.x), 0.0, 1.0);
  vec3 c = mix(vec3(0.17, 0.2, 0.28), vec3(0.62, 0.68, 0.8), h) * l;
  o = vec4(c * uDim, 1.0);
}`;
const FS_TEX = `#version 300 es
precision highp float; uniform sampler2D uTex; uniform float uDim; uniform float uGame; uniform float uBlend; in vec3 vW; in vec2 vUV; in vec4 vCol; out vec4 o;
void main(){
  vec4 t = texture(uTex, vUV);
  if (uBlend > 0.5) { if (t.a * vCol.a < 0.04) discard; } else if (t.a < 0.35) discard;
  if (uGame > 0.5) { o = vec4(min(t.rgb * vCol.rgb * 1.15, vec3(1.0)) * uDim, uBlend > 0.5 ? t.a * vCol.a : 1.0); return; }     // the game's own light: the picture times the colour baked into each vertex
  vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
  float l = 0.55 + 0.45 * abs(dot(n, normalize(vec3(0.35, 0.85, 0.4))));
  o = vec4(t.rgb * l * uDim, 1.0);
}`;

function program(gl, vs, fs) {
  const p = gl.createProgram();
  for (const [type, src] of [[gl.VERTEX_SHADER, vs], [gl.FRAGMENT_SHADER, fs]]) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
    gl.attachShader(p, s);
  }
  gl.linkProgram(p);
  return { p, loc: {} };
}

export class Floors {
  // the upward-facing triangles of a room's model in a grid over the ground: heightAt gives the highest floor at or below a height
  constructor(tris) {
    this.cell = 250;
    this.grid = new Map();
    this.tris = tris;
    for (let t = 0; t < tris.length; t += 9) {
      const xs = [tris[t], tris[t + 3], tris[t + 6]], zs = [tris[t + 2], tris[t + 5], tris[t + 8]];
      for (let gx = Math.floor(Math.min(...xs) / this.cell); gx <= Math.floor(Math.max(...xs) / this.cell); gx++) {
        for (let gz = Math.floor(Math.min(...zs) / this.cell); gz <= Math.floor(Math.max(...zs) / this.cell); gz++) {
          const k = gx * 100003 + gz;
          if (!this.grid.has(k)) this.grid.set(k, []);
          this.grid.get(k).push(t);
        }
      }
    }
  }

  heightAt(x, z, below = 1e9) {
    const list = this.grid.get(Math.floor(x / this.cell) * 100003 + Math.floor(z / this.cell));
    if (!list) return null;
    let best = null;
    const T = this.tris;
    for (const t of list) {
      const ax = T[t], ay = T[t + 1], az = T[t + 2], bx = T[t + 3], by = T[t + 4], bz = T[t + 5], cx = T[t + 6], cy = T[t + 7], cz = T[t + 8];
      const d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
      if (Math.abs(d) < 1e-6) continue;
      const u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d, v = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d, w = 1 - u - v;
      if (u < -0.001 || v < -0.001 || w < -0.001) continue;
      const y = u * ay + v * by + w * cy;
      if (y <= below && (best === null || y > best)) best = y;
    }
    return best;
  }
}

export class Renderer {
  constructor(canvas) {
    this.canvas = canvas;
    this.gl = canvas.getContext("webgl2", { antialias: true, alpha: false });
    if (!this.gl) throw new Error("This browser has no WebGL2.");
    const gl = this.gl;
    this.flat = program(gl, VS, FS_FLAT);
    this.tex = program(gl, VS, FS_TEX);
    this.models = {};     // room file -> {bones, world, meshes, bounds, floors}
    this.textures = {};
    this.onTexture = () => {};
  }

  has(file) { return !!this.models[file]; }

  add(file, json) {
    const gl = this.gl;
    const bones = json.bones, world = worlds(bones), meshes = [], tris = [];
    const lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    json.meshes.forEach((m) => {
      const pos = b64(m.p, Float32Array), uv = b64(m.uv, Float32Array);
      const vb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, vb); gl.bufferData(gl.ARRAY_BUFFER, pos, gl.STATIC_DRAW);
      const ub = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, ub); gl.bufferData(gl.ARRAY_BUFFER, uv, gl.STATIC_DRAW);
      const cb = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, cb); gl.bufferData(gl.ARRAY_BUFFER, m.c ? b64(m.c, Float32Array) : new Float32Array(pos.length / 3 * 4).fill(0.5), gl.STATIC_DRAW);
      const w = world[m.bone], hide = !w || hidden(bones[m.bone]);
      const groups = m.groups.map((g) => {
        const idx = b64(g.i, Uint32Array);
        const ib = gl.createBuffer(); gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, ib); gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, idx, gl.STATIC_DRAW);
        if (!hide) {
          // the triangles in world space: bounds and floors
          for (let t = 0; t < idx.length; t += 3) {
            const P = [0, 1, 2].map((k) => {
              const i = idx[t + k] * 3, x = pos[i], y = pos[i + 1], z = pos[i + 2];
              return [w[0] * x + w[4] * y + w[8] * z + w[12], w[1] * x + w[5] * y + w[9] * z + w[13], w[2] * x + w[6] * y + w[10] * z + w[14]];
            });
            for (const p of P) for (let k = 0; k < 3; k++) { if (p[k] < lo[k]) lo[k] = p[k]; if (p[k] > hi[k]) hi[k] = p[k]; }
            const e1 = [P[1][0] - P[0][0], P[1][1] - P[0][1], P[1][2] - P[0][2]], e2 = [P[2][0] - P[0][0], P[2][1] - P[0][1], P[2][2] - P[0][2]];
            const ny = e1[2] * e2[0] - e1[0] * e2[2], len = Math.hypot(e1[1] * e2[2] - e1[2] * e2[1], ny, e1[0] * e2[1] - e1[1] * e2[0]);
            if (len > 0 && Math.abs(ny) / len > 0.5 && P.every((p) => Math.hypot(p[0] - P[0][0], p[2] - P[0][2]) < 4000)) tris.push(...P[0], ...P[1], ...P[2]);
          }
        }
        return { material: g.m, ib, count: idx.length };
      });
      const lop = [1e9, 1e9, 1e9], hip = [-1e9, -1e9, -1e9];
      for (let i = 0; i < pos.length; i += 3) for (let k = 0; k < 3; k++) { if (pos[i + k] < lop[k]) lop[k] = pos[i + k]; if (pos[i + k] > hip[k]) hip[k] = pos[i + k]; }
      const colours = m.c ? b64(m.c, Float32Array) : null;
      let blend = false, additive = false;
      if (colours) {
        let lit = 0, n = 0;
        for (let k = 3; k < colours.length; k += 4) { if (colours[k] < 0.99) blend = true; lit += colours[k - 1] + colours[k - 2] + colours[k - 3]; n++; }
        additive = blend && n > 0 && lit / (3 * n) > 1.4;       // see-through and bright: a patch of light, which adds to what is behind it
      }
      meshes.push({ vb, ub, cb, blend, additive, groups, bone: m.bone, hide, mesh: m.mesh, lo: lop, hi: hip });
    });
    this.models[file] = { bones, base: bones.length, extra: [], world, meshes, tids: json.tids || [], bounds: [...lo, ...hi], floors: new Floors(new Float32Array(tris)) };
  }

  // the skeleton with some bones replaced ({bone: [parent, scale, rotation, offset]}): the objects move on screen as they will in the game
  // `clones`: [{like (an object's number), bone}] the copies made of objects, each on a bone after the model's own (they share the original's buffers)
  setBones(file, changes, clones = []) {
    const m = this.models[file];
    if (!m) return;
    const bones = m.bones.map((b, i) => changes[i] || b);
    clones.forEach((c, j) => { bones[m.base + j] = c.bone; });
    m.world = worlds(bones);
    for (const mesh of m.meshes) mesh.hide = !m.world[mesh.bone] || hidden(bones[mesh.bone]);
    m.extra = clones.map((c, j) => ({ ...(c.geometry ? this.geometryMesh(c.geometry) : m.meshes.find((x) => x.mesh === c.like)), bone: m.base + j, hide: hidden(c.bone) })).filter((x) => x.vb);
  }

  // the buffers of a model made from geometry ({positions, uvs, triangles, material}); kept for as long as the geometry is the same
  geometryMesh(g) {
    const gl = this.gl, key = JSON.stringify([g.material, g.positions.length, g.triangles.length, g.positions[0], g.triangles[0]]);
    this.made = this.made || new Map();
    const old = this.made.get(g);
    if (old && old.key === key) return old.mesh;
    const buf = (type, data) => { const b = gl.createBuffer(); gl.bindBuffer(type, b); gl.bufferData(type, data, gl.STATIC_DRAW); return b; };
    const mesh = {
      vb: buf(gl.ARRAY_BUFFER, new Float32Array(g.positions.flat())), ub: buf(gl.ARRAY_BUFFER, new Float32Array(g.uvs.flat())), cb: buf(gl.ARRAY_BUFFER, Float32Array.from({ length: g.positions.length * 4 }, (_, i) => (i % 4 === 3 ? 1 : 0.45))), blend: false,
      groups: [{ material: g.material, ib: buf(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(g.triangles.flat())), count: g.triangles.length * 3 }], mesh: -1,
    };
    this.made.set(g, { key, mesh });
    return mesh;
  }

  texture(index) {
    const gl = this.gl;
    if (this.textures[index]) return this.textures[index];
    const tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 1, 1, 0, gl.RGBA, gl.UNSIGNED_BYTE, new Uint8Array([150, 156, 170, 255]));
    this.textures[index] = tex;
    const img = new Image();
    img.onload = () => {
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      this.onTexture();
    };
    img.src = "/api/tex?index=" + index;
    return tex;
  }

  // a picture of the room painted over by an image (a data: URL), or back as the disc has it (url null)
  setPicture(index, url) {
    const gl = this.gl, tex = this.texture(index), img = new Image();
    img.onload = () => {
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.generateMipmap(gl.TEXTURE_2D);
      this.onTexture();
    };
    img.src = url || `/api/tex?index=${index}&v=${Date.now()}`;
  }

  resize(w, h, dpr) {
    const c = this.canvas;
    const pw = Math.floor(w * dpr), ph = Math.floor(h * dpr);
    if (c.width !== pw || c.height !== ph) { c.width = pw; c.height = ph; }
  }

  // rooms: the files to draw; dim: {file: 0..1} (the rooms not being worked in are drawn dimmer)
  render(camera, rooms, opts) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    gl.disable(gl.SCISSOR_TEST); gl.depthMask(true); gl.disable(gl.BLEND);
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0.039, 0.047, 0.063, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    if (camera.mode === "game") {        // the game's picture has its own shape: drawn in a frame of that shape
      const r = camera.rect, k = this.canvas.width / camera.w, y = (camera.h - r.y - r.h) * k;
      gl.enable(gl.SCISSOR_TEST); gl.viewport(r.x * k, y, r.w * k, r.h * k); gl.scissor(r.x * k, y, r.w * k, r.h * k);
      gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    }
    gl.enable(gl.DEPTH_TEST);
    const vp = camera.viewProj();
    const textured = (opts.textured && camera.mode !== "plan") || camera.lit;
    const prog = textured ? this.tex : this.flat;
    gl.useProgram(prog.p);
    const U = (name) => prog.loc[name] ?? (prog.loc[name] = gl.getUniformLocation(prog.p, name));
    gl.uniformMatrix4fv(U("uVP"), false, vp);
    const posLoc = gl.getAttribLocation(prog.p, "aPos"), uvLoc = textured ? gl.getAttribLocation(prog.p, "aUV") : -1, colLoc = textured ? gl.getAttribLocation(prog.p, "aCol") : -1;
    if (textured) gl.uniform1f(U("uGame"), camera.lit ? 1 : 0);
    for (const file of rooms) {
      const m = this.models[file];
      if (!m) continue;
      gl.uniform1f(U("uDim"), (opts.dim && opts.dim[file]) ?? 1);
      if (!textured) gl.uniform2f(U("uHeight"), m.bounds[1] - 50, m.bounds[1] + Math.min(1800, Math.max(300, m.bounds[4] - m.bounds[1])));
      for (const mesh of [...m.meshes, ...m.extra].sort((a, b) => (a.blend ? 1 : 0) - (b.blend ? 1 : 0))) {
        if (mesh.hide || mesh.skip) continue;
        if (camera.lit && opts.hidden && opts.hidden[file] && opts.hidden[file].has(mesh.mesh)) continue;     // what the room's own script hides as it starts
        const blend = textured && camera.lit && mesh.blend;
        if (blend) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, mesh.additive ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); } else { gl.disable(gl.BLEND); gl.depthMask(true); }
        if (textured) gl.uniform1f(U("uBlend"), blend ? 1 : 0);
        gl.uniformMatrix4fv(U("uModel"), false, m.world[mesh.bone]);
        gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vb); gl.enableVertexAttribArray(posLoc); gl.vertexAttribPointer(posLoc, 3, gl.FLOAT, false, 0, 0);
        if (textured) {
          gl.bindBuffer(gl.ARRAY_BUFFER, mesh.ub); gl.enableVertexAttribArray(uvLoc); gl.vertexAttribPointer(uvLoc, 2, gl.FLOAT, false, 0, 0);
          if (colLoc >= 0) { gl.bindBuffer(gl.ARRAY_BUFFER, mesh.cb); gl.enableVertexAttribArray(colLoc); gl.vertexAttribPointer(colLoc, 4, gl.FLOAT, false, 0, 0); }
        }
        for (const g of mesh.groups) {
          if (textured) {
            const tid = m.tids[g.material];
            gl.activeTexture(gl.TEXTURE0);
            gl.bindTexture(gl.TEXTURE_2D, this.texture(tid === undefined ? 0 : tid));
            gl.uniform1i(U("uTex"), 0);
          }
          gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ib);
          gl.drawElements(gl.TRIANGLES, g.count, gl.UNSIGNED_INT, 0);
        }
      }
    }
  }
}
