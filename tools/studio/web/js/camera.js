// The viewport's camera: a plan view (straight down, orthographic) or an orbit view (perspective), one set of matrices for the models and the markers.
// The game's axes: x to the right on the plan, z down the screen, y up.

const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = (a) => { const l = Math.hypot(a[0], a[1], a[2]) || 1; return [a[0] / l, a[1] / l, a[2] / l]; };

export function mul(a, b) {
  const o = new Float32Array(16);
  for (let c = 0; c < 4; c++) for (let r = 0; r < 4; r++) {
    let s = 0;
    for (let k = 0; k < 4; k++) s += a[k * 4 + r] * b[c * 4 + k];
    o[c * 4 + r] = s;
  }
  return o;
}

export class Camera {
  constructor() {
    this.mode = "plan";
    this.target = [0, 0, 0];
    this.scale = 20;          // plan: world units a pixel
    this.yaw = 0.6;
    this.pitch = 0.7;
    this.dist = 6000;         // orbit: distance from the target
    this.w = 1; this.h = 1;
    this.game = null;         // game view: {eye, target, fov (vertical degrees), aspect}: one of the room's own cameras
    this.rect = { x: 0, y: 0, w: 1, h: 1 };
  }

  resize(w, h) { this.w = Math.max(1, w); this.h = Math.max(1, h); this.fit(); }

  // in the game view the picture keeps the game's shape and sits in the middle of the pane
  fit() {
    if (this.mode !== "game" || !this.game) { this.rect = { x: 0, y: 0, w: this.w, h: this.h }; return; }
    let w = this.w, h = w / this.game.aspect;
    if (h > this.h) { h = this.h; w = h * this.game.aspect; }
    this.rect = { x: (this.w - w) / 2, y: (this.h - h) / 2, w, h };
  }

  // g.aspect is the shape of the picture, g.lens the shape the projection is made for: the game's picture is squeezed (measured: lamps 48 and 49 of the zoo's first room land where the game's snapshot has them with a vertical field of 50 degrees and a 16:9 lens in a 8:7 picture)
  setGame(g) { this.game = g; this.mode = "game"; this.fit(); }

  // the eye and the basis the screen is drawn with
  basis() {
    let t = this.target;
    let eye;
    if (this.mode === "game" && this.game) {
      eye = this.game.eye; t = this.game.target;
    } else if (this.mode === "plan") {
      eye = [t[0], t[1] + 40000, t[2] + 1];
    } else {
      const cp = Math.cos(this.pitch);
      eye = [t[0] + this.dist * cp * Math.sin(this.yaw), t[1] + this.dist * Math.sin(this.pitch), t[2] + this.dist * cp * Math.cos(this.yaw)];
    }
    const z = norm(sub(eye, t)), x = norm(cross([0, 1, 0], z)), y = cross(z, x);
    return { eye, x, y, z };
  }

  view() {
    const { eye, x, y, z } = this.basis();
    const m = new Float32Array(16);
    m[0] = x[0]; m[4] = x[1]; m[8] = x[2];
    m[1] = y[0]; m[5] = y[1]; m[9] = y[2];
    m[2] = z[0]; m[6] = z[1]; m[10] = z[2];
    m[12] = -dot(x, eye); m[13] = -dot(y, eye); m[14] = -dot(z, eye); m[15] = 1;
    return m;
  }

  proj() {
    const m = new Float32Array(16), aspect = this.mode === "game" && this.game ? this.game.lens : this.w / this.h;
    if (this.mode === "game" && this.game) {
      const t = 1 / Math.tan(this.game.fov * Math.PI / 360), n = 30, f = 300000;
      m[0] = t / aspect; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
    } else if (this.mode === "plan") {
      const hh = this.scale * this.h / 2, hw = hh * aspect, n = 100, f = 200000;
      m[0] = 1 / hw; m[5] = 1 / hh; m[10] = -2 / (f - n); m[14] = -(f + n) / (f - n); m[15] = 1;
    } else {
      const fov = 1.0, t = 1 / Math.tan(fov / 2), n = 30, f = 300000;
      m[0] = t / aspect; m[5] = t; m[10] = (f + n) / (n - f); m[11] = -1; m[14] = 2 * f * n / (n - f);
    }
    return m;
  }

  viewProj() { return mul(this.proj(), this.view()); }

  // a world point on the screen: [x, y, depth], depth negative when behind the camera
  project(p, vp = this.viewProj()) {
    const x = vp[0] * p[0] + vp[4] * p[1] + vp[8] * p[2] + vp[12];
    const y = vp[1] * p[0] + vp[5] * p[1] + vp[9] * p[2] + vp[13];
    const w = vp[3] * p[0] + vp[7] * p[1] + vp[11] * p[2] + vp[15];
    const zc = vp[2] * p[0] + vp[6] * p[1] + vp[10] * p[2] + vp[14];
    if (w <= 0) return [0, 0, -1];
    const r = this.rect;
    return [r.x + (x / w * 0.5 + 0.5) * r.w, r.y + (1 - (y / w * 0.5 + 0.5)) * r.h, zc / w];
  }

  // the ray through a screen point
  ray(sx, sy) {
    const { eye, x, y, z } = this.basis();
    const r = this.rect, g = this.mode === "game" && this.game;
    const nx = ((sx - r.x) / r.w) * 2 - 1, ny = 1 - ((sy - r.y) / r.h) * 2, aspect = g ? g.lens : this.w / this.h;
    if (this.mode === "plan") {
      const hh = this.scale * this.h / 2, hw = hh * aspect;
      const o = [eye[0] + x[0] * nx * hw + y[0] * ny * hh, eye[1] + x[1] * nx * hw + y[1] * ny * hh, eye[2] + x[2] * nx * hw + y[2] * ny * hh];
      return { o, d: [-z[0], -z[1], -z[2]] };
    }
    const t = Math.tan((g ? g.fov * Math.PI / 180 : 1.0) / 2);
    const d = norm([x[0] * nx * t * aspect + y[0] * ny * t - z[0], x[1] * nx * t * aspect + y[1] * ny * t - z[1], x[2] * nx * t * aspect + y[2] * ny * t - z[2]]);
    return { o: eye, d };
  }

  // [x, z] where the ray through a screen point meets the plane at height y (null when it never does)
  onPlane(sx, sy, y = 0) {
    const { o, d } = this.ray(sx, sy);
    if (Math.abs(d[1]) < 1e-6) return null;
    const t = (y - o[1]) / d[1];
    if (t < 0) return null;
    return [o[0] + d[0] * t, o[2] + d[2] * t];
  }

  pan(dx, dy) {
    if (this.mode === "game") return;
    const { x, y } = this.basis();
    const k = this.mode === "plan" ? this.scale : this.dist * 0.0011;
    if (this.mode === "plan") {
      this.target = [this.target[0] - x[0] * dx * k + y[0] * dy * k, this.target[1], this.target[2] - x[2] * dx * k + y[2] * dy * k];
    } else {
      this.target = [this.target[0] - x[0] * dx * k + y[0] * dy * k, this.target[1] - x[1] * dx * k + y[1] * dy * k, this.target[2] - x[2] * dx * k + y[2] * dy * k];
    }
  }

  // true when the picture is lit the way the game lights it (the game view and the free camera)
  get lit() { return this.mode === "game" || this.mode === "orbit"; }

  // the free camera starts where a game camera stood, looking where it looked
  startFree(eye, target) {
    const d = [eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]];
    this.dist = Math.max(300, Math.hypot(...d));
    this.yaw = Math.atan2(d[0], d[2]);
    this.pitch = Math.asin(Math.max(-1, Math.min(1, d[1] / this.dist)));
    this.target = [...target];
  }

  // the free camera: the mouse turns the view about the eye (the eye stays where it is)
  look(dx, dy) {
    if (this.mode === "game") return;
    const e = this.eye();
    this.yaw -= dx * 0.005;
    this.pitch = Math.max(-1.5, Math.min(1.5, this.pitch - dy * 0.005));
    const d = this.offset();
    this.target = [e[0] - d[0], e[1] - d[1], e[2] - d[2]];
  }

  orbit(dx, dy) { this.look(dx, dy); }

  // from the target to the eye
  offset() {
    const cp = Math.cos(this.pitch);
    return [this.dist * cp * Math.sin(this.yaw), this.dist * Math.sin(this.pitch), this.dist * cp * Math.cos(this.yaw)];
  }

  eye() { const d = this.offset(); return [this.target[0] + d[0], this.target[1] + d[1], this.target[2] + d[2]]; }

  // keys held: the eye moves along where it looks (w, s), sideways (a, d) and up and down (e, q), `dt` seconds' worth at `speed` units a second
  fly(keys, dt, speed) {
    if (this.mode !== "orbit") return false;
    const { x, z } = this.basis();
    const fwd = [-z[0], -z[1], -z[2]];
    let mx = 0, my = 0, mz = 0;
    const add = (v, k) => { mx += v[0] * k; my += v[1] * k; mz += v[2] * k; };
    if (keys.has("w")) add(fwd, 1);
    if (keys.has("s")) add(fwd, -1);
    if (keys.has("d")) add(x, 1);
    if (keys.has("a")) add(x, -1);
    if (keys.has("e")) add([0, 1, 0], 1);
    if (keys.has("q")) add([0, 1, 0], -1);
    if (!mx && !my && !mz) return false;
    const k = speed * dt;
    this.target = [this.target[0] + mx * k, this.target[1] + my * k, this.target[2] + mz * k];
    return true;
  }

  // zoom about a screen point, so what is under the cursor stays under it
  zoom(factor, sx, sy) {
    if (this.mode === "game") return;
    if (this.mode === "plan") {
      const before = this.onPlane(sx, sy, 0);
      this.scale = Math.max(0.4, Math.min(400, this.scale * factor));
      const after = this.onPlane(sx, sy, 0);
      if (before && after) this.target = [this.target[0] + before[0] - after[0], this.target[1], this.target[2] + before[1] - after[1]];
    } else {
      const { z } = this.basis(), k = (factor > 1 ? -1 : 1) * Math.abs(Math.log(factor)) * 2200;     // the wheel moves the eye along where it looks
      this.target = [this.target[0] - z[0] * k * -1, this.target[1] - z[1] * k * -1, this.target[2] - z[2] * k * -1];
    }
  }

  // fits a box [minx, miny, minz, maxx, maxy, maxz] in the view
  frame(box, margin = 1.25) {
    const cx = (box[0] + box[3]) / 2, cy = (box[1] + box[4]) / 2, cz = (box[2] + box[5]) / 2;
    this.target = [cx, cy, cz];
    const w = Math.max(200, box[3] - box[0]), d = Math.max(200, box[5] - box[2]);
    this.scale = Math.max(w / this.w, d / this.h) * margin;
    this.dist = Math.max(w, d) * 1.1 * margin;
  }

  setMode(mode) {
    if (mode === this.mode) return;
    if (mode === "orbit" && this.mode === "plan") this.dist = Math.max(800, this.scale * this.h * 0.9);
    if (mode === "plan" && this.mode === "orbit") this.scale = Math.max(2, this.dist / this.h * 1.1);
    this.mode = mode;
    this.fit();
  }
}
