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
precision highp float; uniform sampler2D uTex; uniform float uDim; uniform float uGame; uniform float uBlend; uniform float uGain; uniform vec3 uEye; uniform vec3 uFog; uniform vec3 uScene; uniform float uVertex; in vec3 vW; in vec2 vUV; in vec4 vCol; out vec4 o;
void main(){
  vec4 t = texture(uTex, vUV);
  if (uBlend > 0.5) { if (t.a * vCol.a < 0.04) discard; } else if (t.a < 0.35) discard;
  if (uGame > 0.5) {
    // the game's own light (see lightOf): the picture times the colour baked into each vertex times the camera's scene colour, then the fog by distance
    vec3 c = t.rgb * vCol.rgb * uVertex * uGain * uScene;
    float f = uFog.z > 0.5 ? clamp((distance(vW, uEye) - uFog.x) / max(1.0, uFog.y - uFog.x), 0.0, 1.0) : 0.0;
    c = mix(c, vec3(0.0), f);
    o = vec4(min(c, vec3(1.0)) * uDim, uBlend > 0.5 ? t.a * vCol.a : 1.0);
    return;
  }
  vec3 n = normalize(cross(dFdx(vW), dFdy(vW)));
  float l = 0.55 + 0.45 * abs(dot(n, normalize(vec3(0.35, 0.85, 0.4))));
  o = vec4(t.rgb * l * uDim, 1.0);
}`;

// a camera's fog record (the room file's section 13: bh_rdt FOG_FIELDS) as the shader's light. Measured against the game's own pictures of the bar from the same nine
// cameras (mean colour difference 34 of 255 before, 13.9 after): the vertex colours are full at 255 (128 is half), the scene colour (word 4, bytes red, green, blue
// from the lowest) scales the picture as a fraction of 255, and the ambient colour (word 3) made the match worse when added, so it isn't. The fog (words 1 and 2: near
// and far, floats) goes to black: it changed nothing in the bar (everything there is nearer than its start), so that part is unmeasured.
const asFloat = (w) => new Float32Array(new Uint32Array([w >>> 0]).buffer)[0];
const rgbOf = (w) => [(w & 255) / 255, ((w >>> 8) & 255) / 255, ((w >>> 16) & 255) / 255];
function lightOf(fog) {
  if (!fog) return { fog: [0, 1, 0], scene: [1, 1, 1], vertex: 1 };
  return { fog: [asFloat(fog[1]), asFloat(fog[2]), 1], scene: rgbOf(fog[4]), vertex: 0.5 };
}

const BEHIND_LENS = [1e9, 1e9, 0.001];      // nearKept's band: only what is behind the lens

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
    this.characters = {}; // a character model's file (romdata.afs) -> the same, standing at the origin: drawn where each enemy stands (opts.characters)
    this.textures = {};
    this.loaded = {};
    this.onTexture = () => {};
  }

  has(file) { return !!this.models[file]; }

  add(file, json) { this.models[file] = this.build(json); }
  addCharacter(file, json) { this.characters[file] = this.build(json); }

  // a model's buffers, bones and floors from its JSON (bh_rdt_edit.model_json, bh_characters.character_json)
  build(json) {
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
        return { material: g.m, ib, count: idx.length, idx };
      });
      const lop = [1e9, 1e9, 1e9], hip = [-1e9, -1e9, -1e9];
      for (let i = 0; i < pos.length; i += 3) for (let k = 0; k < 3; k++) { if (pos[i + k] < lop[k]) lop[k] = pos[i + k]; if (pos[i + k] > hip[k]) hip[k] = pos[i + k]; }
      const colours = m.c ? b64(m.c, Float32Array) : null;
      // the file says which meshes the game adds to what is behind them (light, glow, the reflections on glass: bh_nbd.Mesh.additive); the others are drawn over
      // it, see-through where their vertices' alpha is under full
      const additive = !!m.additive;
      let blend = additive;
      if (colours) for (let k = 3; k < colours.length; k += 4) if (colours[k] < 0.99) blend = true;
      meshes.push({ vb, ub, cb, blend, additive, groups, bone: m.bone, hide, mesh: m.mesh, lo: lop, hi: hip, pos });
    });
    let sum = 0, count = 0;
    json.meshes.forEach((m) => { if (!m.c) return; const c = b64(m.c, Float32Array); for (let i = 0; i < c.length; i += 4) { sum += c[i] + c[i + 1] + c[i + 2]; count += 3; } });
    const gain = count ? Math.max(1, Math.min(3, 0.45 / Math.max(0.05, sum / count))) : 1;     // a dark room is lifted toward what the game's own lights show
    return { raw: json, gain, bones, base: bones.length, extra: [], world, meshes, tids: json.tids || [], bounds: [...lo, ...hi], floors: new Floors(new Float32Array(tris)) };
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
    m.extra = clones.map((c, j) => ({ ...(c.library ? this.libraryMesh(c.library) : c.geometry ? this.geometryMesh(c.geometry) : m.meshes.find((x) => x.mesh === c.like)), bone: m.base + j, hide: hidden(c.bone) })).filter((x) => x.vb);
  }

  // an object of another room's model (its buffers and that model's pictures)
  libraryMesh(lib) {
    const src = this.models[lib.room];
    const mesh = src && src.meshes.find((x) => x.mesh === lib.mesh);
    return mesh ? { ...mesh, tids: src.tids, hide: false } : {};
  }

  // a picture of one object of a room's model on its own, as a data: URL; ready is false while its pictures are still arriving
  thumbnail(file, k, px = 112, transparent = false) {
    const r = this.thumbnailPixels(file, k, px, transparent);
    if (!r) return null;
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = px;
    const ctx = canvas.getContext("2d"), image = ctx.createImageData(px, px);
    image.data.set(r.pixels);
    ctx.putImageData(image, 0, 0);
    return { url: canvas.toDataURL("image/png"), ready: r.ready };
  }

  // the same picture as rows of RGBA bytes, top row first
  thumbnailPixels(file, k, px, transparent = false) {
    const gl = this.gl, m = this.models[file], mesh = m && m.meshes.find((x) => x.mesh === k);
    if (!mesh) return null;
    if (this.thumbFbo && this.thumbPx !== px) {
      gl.deleteFramebuffer(this.thumbFbo); gl.deleteTexture(this.thumbTex); gl.deleteRenderbuffer(this.thumbDepth);
      this.thumbFbo = null;
    }
    if (!this.thumbFbo) {
      this.thumbPx = px;
      const tex = this.thumbTex = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, px, px, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
      const depth = this.thumbDepth = gl.createRenderbuffer();
      gl.bindRenderbuffer(gl.RENDERBUFFER, depth);
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT16, px, px);
      this.thumbFbo = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, this.thumbFbo);
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depth);
    }
    const lo = mesh.lo, hi = mesh.hi, c = [0, 1, 2].map((i) => (lo[i] + hi[i]) / 2), r = Math.max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], 20) * 0.75;
    const eye = [c[0] + r * 1.7, c[1] + r * 1.25, c[2] + r * 2.1];
    // look at the middle from a corner above
    const f = [c[0] - eye[0], c[1] - eye[1], c[2] - eye[2]], fl = Math.hypot(...f), fz = f.map((v) => -v / fl);
    const rx = [fz[2], 0, -fz[0]], rl = Math.hypot(...rx), xx = rx.map((v) => v / rl);
    const yy = [fz[1] * xx[2] - fz[2] * xx[1], fz[2] * xx[0] - fz[0] * xx[2], fz[0] * xx[1] - fz[1] * xx[0]];
    const view = new Float32Array(16);
    view[0] = xx[0]; view[4] = xx[1]; view[8] = xx[2]; view[1] = yy[0]; view[5] = yy[1]; view[9] = yy[2]; view[2] = fz[0]; view[6] = fz[1]; view[10] = fz[2];
    view[12] = -(xx[0] * eye[0] + xx[1] * eye[1] + xx[2] * eye[2]); view[13] = -(yy[0] * eye[0] + yy[1] * eye[1] + yy[2] * eye[2]); view[14] = -(fz[0] * eye[0] + fz[1] * eye[1] + fz[2] * eye[2]); view[15] = 1;
    const t = 1 / Math.tan(0.28), proj = new Float32Array(16), n = 5, fa = r * 40;
    proj[0] = t; proj[5] = t; proj[10] = (fa + n) / (n - fa); proj[11] = -1; proj[14] = 2 * fa * n / (n - fa);
    const prog = this.tex, U = (name) => prog.loc[name] ?? (prog.loc[name] = gl.getUniformLocation(prog.p, name));
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.thumbFbo);
    gl.viewport(0, 0, px, px); gl.disable(gl.SCISSOR_TEST); gl.disable(gl.CULL_FACE); gl.disable(gl.BLEND); gl.depthMask(true);
    if (transparent) gl.clearColor(0, 0, 0, 0); else gl.clearColor(0.1, 0.11, 0.15, 1);
    gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT); gl.enable(gl.DEPTH_TEST);
    gl.useProgram(prog.p);
    const id = new Float32Array(16); id[0] = id[5] = id[10] = id[15] = 1;
    gl.uniformMatrix4fv(U("uVP"), false, mul(proj, view)); gl.uniformMatrix4fv(U("uModel"), false, id);
    gl.uniform1f(U("uDim"), 1); gl.uniform1f(U("uGame"), 1); gl.uniform1f(U("uGain"), 2.4); gl.uniform1f(U("uBlend"), 0);
    gl.uniform1f(U("uVertex"), 1); gl.uniform3fv(U("uScene"), [1, 1, 1]); gl.uniform3fv(U("uFog"), [0, 1, 0]); gl.uniform3fv(U("uEye"), eye);
    const pos = gl.getAttribLocation(prog.p, "aPos"), uv = gl.getAttribLocation(prog.p, "aUV"), col = gl.getAttribLocation(prog.p, "aCol");
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vb); gl.enableVertexAttribArray(pos); gl.vertexAttribPointer(pos, 3, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.ub); gl.enableVertexAttribArray(uv); gl.vertexAttribPointer(uv, 2, gl.FLOAT, false, 0, 0);
    gl.bindBuffer(gl.ARRAY_BUFFER, mesh.cb); gl.enableVertexAttribArray(col); gl.vertexAttribPointer(col, 4, gl.FLOAT, false, 0, 0);
    let ready = true;
    for (const grp of mesh.groups) {
      const tid = m.tids[grp.material];
      gl.activeTexture(gl.TEXTURE0);
      gl.bindTexture(gl.TEXTURE_2D, this.texture(tid === undefined ? 0 : tid));
      if (!this.loaded[tid]) ready = false;
      gl.uniform1i(U("uTex"), 0);
      gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, grp.ib);
      gl.drawElements(gl.TRIANGLES, grp.count, gl.UNSIGNED_INT, 0);
    }
    const pixels = new Uint8Array(px * px * 4);
    gl.readPixels(0, 0, px, px, gl.RGBA, gl.UNSIGNED_BYTE, pixels);
    gl.bindFramebuffer(gl.FRAMEBUFFER, null);
    const flipped = new Uint8ClampedArray(px * px * 4);
    for (let y = 0; y < px; y++) flipped.set(pixels.subarray(y * px * 4, (y + 1) * px * 4), (px - 1 - y) * px * 4);     // the picture's rows run the other way
    return { pixels: flipped, ready };
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
      this.loaded[index] = true;
      gl.bindTexture(gl.TEXTURE_2D, tex);
      gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, img);
      gl.generateMipmap(gl.TEXTURE_2D);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      this.onTexture();
    };
    img.src = typeof index === "string" ? "/api/character/picture?key=" + encodeURIComponent(index) : "/api/tex?index=" + index;      // a character's own picture ("FILE#n") or the scenario's
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
    if (camera.pictureLens) {        // the game's picture has its own shape: drawn in a frame of that shape
      const r = camera.rect, k = this.canvas.width / camera.w, y = (camera.h - r.y - r.h) * k;
      gl.enable(gl.SCISSOR_TEST); gl.viewport(r.x * k, y, r.w * k, r.h * k); gl.scissor(r.x * k, y, r.w * k, r.h * k);
      gl.clearColor(0, 0, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
    }
    gl.enable(gl.DEPTH_TEST);
    // the game's rooms are drawn one-sided: from outside a wall you see through it (the cameras stand outside)
    if (camera.lit && opts.winding) { gl.enable(gl.CULL_FACE); gl.cullFace(gl.BACK); gl.frontFace(opts.winding === "cw" ? gl.CW : gl.CCW); } else gl.disable(gl.CULL_FACE);
    const vp = camera.viewProj();
    const textured = (opts.textured && camera.mode !== "plan") || camera.lit;
    const prog = textured ? this.tex : this.flat;
    gl.useProgram(prog.p);
    const U = (name) => prog.loc[name] ?? (prog.loc[name] = gl.getUniformLocation(prog.p, name));
    gl.uniformMatrix4fv(U("uVP"), false, vp);
    const posLoc = gl.getAttribLocation(prog.p, "aPos"), uvLoc = textured ? gl.getAttribLocation(prog.p, "aUV") : -1, colLoc = textured ? gl.getAttribLocation(prog.p, "aCol") : -1;
    if (textured) gl.uniform1f(U("uGame"), camera.lit ? 1 : 0);
    if (textured) {
      const look = lightOf(opts.fog);
      gl.uniform3fv(U("uEye"), camera.basis().eye); gl.uniform3fv(U("uFog"), look.fog); gl.uniform3fv(U("uScene"), look.scene); gl.uniform1f(U("uVertex"), look.vertex);
    }
    const vpm = camera.viewProj(), nearCut = textured && camera.lit && opts.fog ? BEHIND_LENS : null;
    const draw = { U, textured, camera, posLoc, uvLoc, colLoc, vpm, nearCut };
    for (const file of rooms) {
      const m = this.models[file];
      if (!m) continue;
      gl.uniform1f(U("uDim"), (opts.dim && opts.dim[file]) ?? 1);
      if (textured) gl.uniform1f(U("uGain"), opts.fog ? 1 : (m.gain || 1));      // (with the game's light known its own scene colour does the work; without it a dark room is lifted)
      if (!textured) gl.uniform2f(U("uHeight"), m.bounds[1] - 50, m.bounds[1] + Math.min(1800, Math.max(300, m.bounds[4] - m.bounds[1])));
      this.drawMeshes(m, [...m.meshes, ...m.extra], (mesh) => m.world[mesh.bone], draw, opts.hidden && opts.hidden[file]);
    }
    // the enemies, each its own character model where it stands, turned its way
    for (const c of opts.characters || []) {
      const m = this.characters[c.file];
      if (!m) continue;
      gl.uniform1f(U("uDim"), 1);
      if (textured) gl.uniform1f(U("uGain"), 1);
      this.drawMeshes(m, m.meshes, (mesh) => mul(c.at, m.world[mesh.bone]), { ...draw, nearCut: null }, null);
    }
  }

  // a model's meshes drawn (the see-through ones last), each placed by place(mesh); `hidden`: the mesh numbers the room's script hides
  drawMeshes(m, meshes, place, draw, hidden) {
    const gl = this.gl, { U, textured, camera, posLoc, uvLoc, colLoc, vpm, nearCut } = draw;
    for (const mesh of [...meshes].sort((a, b) => (a.blend ? 1 : 0) - (b.blend ? 1 : 0))) {
      if (mesh.hide || mesh.skip) continue;
      if (camera.lit && hidden && hidden.has(mesh.mesh)) continue;     // what the room's own script hides as it starts
      const blend = textured && camera.lit && mesh.blend;
      if (blend) { gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, mesh.additive ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA); gl.depthMask(false); } else { gl.disable(gl.BLEND); gl.depthMask(true); }
      if (textured) gl.uniform1f(U("uBlend"), blend ? 1 : 0);
      const at = place(mesh);
      gl.uniformMatrix4fv(U("uModel"), false, at);
      gl.bindBuffer(gl.ARRAY_BUFFER, mesh.vb); gl.enableVertexAttribArray(posLoc); gl.vertexAttribPointer(posLoc, 3, gl.FLOAT, false, 0, 0);
      if (textured) {
        gl.bindBuffer(gl.ARRAY_BUFFER, mesh.ub); gl.enableVertexAttribArray(uvLoc); gl.vertexAttribPointer(uvLoc, 2, gl.FLOAT, false, 0, 0);
        if (colLoc >= 0) { gl.bindBuffer(gl.ARRAY_BUFFER, mesh.cb); gl.enableVertexAttribArray(colLoc); gl.vertexAttribPointer(colLoc, 4, gl.FLOAT, false, 0, 0); }
      }
      for (const g of mesh.groups) {
        if (textured) {
          const tid = (mesh.tids || m.tids)[g.material];
          gl.activeTexture(gl.TEXTURE0);
          gl.bindTexture(gl.TEXTURE_2D, this.texture(tid === undefined ? 0 : tid));
          gl.uniform1i(U("uTex"), 0);
        }
        if (nearCut && mesh.pos && g.idx) {
          const kept = this.nearKept(mesh, g, at, vpm, nearCut);
          if (!kept.count) continue;
          gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, kept.ib);
          gl.drawElements(gl.TRIANGLES, kept.count, gl.UNSIGNED_INT, 0);
        } else {
          gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.ib);
          gl.drawElements(gl.TRIANGLES, g.count, gl.UNSIGNED_INT, 0);
        }
      }
    }
  }

  // the triangles of a mesh's group the game draws from this camera: it has no clipping (its VU1 programs have no CLIP), so a triangle with a corner behind the lens is
  // left out whole. Measured against the game's pictures of the bar from its ten cameras: that rule (`band` [x, y, w]: no limit across, none up, w above 0) gave the best
  // match (edges 0.30 from 0.25); cutting nearer than 100 to 500 or far off the picture made it worse
  nearKept(mesh, g, w, vp, band) {
    const gl = this.gl, key = vp.join(",") + "|" + w.join(",") + "|" + band.join(",");
    g.near = g.near || { ib: gl.createBuffer(), key: "", count: 0 };
    if (g.near.key === key) return g.near;
    const m = mul(vp, w), p = mesh.pos, idx = g.idx, out = new Uint32Array(idx.length), [bx, by, cut] = band;
    let n = 0;
    const fine = (i) => {
      const x = p[i * 3], y = p[i * 3 + 1], z = p[i * 3 + 2];
      const cw = m[3] * x + m[7] * y + m[11] * z + m[15];
      if (cw <= cut) return false;
      const cx = m[0] * x + m[4] * y + m[8] * z + m[12], cy = m[1] * x + m[5] * y + m[9] * z + m[13];
      return Math.abs(cx) <= bx * cw && Math.abs(cy) <= by * cw;
    };
    for (let t = 0; t < idx.length; t += 3) {
      if (fine(idx[t]) && fine(idx[t + 1]) && fine(idx[t + 2])) { out[n++] = idx[t]; out[n++] = idx[t + 1]; out[n++] = idx[t + 2]; }
    }
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, g.near.ib);
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, out.subarray(0, n), gl.DYNAMIC_DRAW);
    g.near.key = key; g.near.count = n;
    return g.near;
  }
}
