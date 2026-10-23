// A Wavefront .obj read into the geometry a room object is written from: triangles, the picture coordinates, a size in the room's units, standing on its middle.
export function parseObj(text, size = 400) {
  const v = [], vt = [], key = new Map(), positions = [], uvs = [], triangles = [];
  const pick = (s, n) => { const i = parseInt(s, 10); return i < 0 ? n + i : i - 1; };
  for (const raw of text.split(/\r?\n/)) {
    const p = raw.trim().split(/\s+/);
    if (p[0] === "v") v.push([+p[1], +p[2], +p[3]]);
    else if (p[0] === "vt") vt.push([+p[1], +p[2]]);
    else if (p[0] === "f" && p.length >= 4) {
      const ids = p.slice(1).map((c) => {
        const [a, b] = c.split("/");
        const vi = pick(a, v.length), ti = b ? pick(b, vt.length) : -1, k = vi + "/" + ti;
        if (!key.has(k)) {
          key.set(k, positions.length);
          positions.push(v[vi]);
          const t = ti >= 0 && vt[ti] ? vt[ti] : [0, 0];
          uvs.push([t[0], 1 - t[1]]);          // the game's pictures start at the top
        }
        return key.get(k);
      });
      for (let i = 1; i < ids.length - 1; i++) triangles.push([ids[0], ids[i], ids[i + 1]]);
    }
  }
  if (!triangles.length) throw new Error("There are no faces in that file.");
  const lo = [0, 1, 2].map((k) => Math.min(...positions.map((q) => q[k]))), hi = [0, 1, 2].map((k) => Math.max(...positions.map((q) => q[k])));
  const big = Math.max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) || 1, k = size / big;
  const cx = (lo[0] + hi[0]) / 2, cz = (lo[2] + hi[2]) / 2;
  return { positions: positions.map((q) => [(q[0] - cx) * k, (q[1] - lo[1]) * k, (q[2] - cz) * k].map((x) => Math.round(x * 100) / 100)), uvs, triangles, material: 0 };
}

export const boundsOf = (g) => {
  const lo = [0, 1, 2].map((k) => Math.min(...g.positions.map((q) => q[k]))), hi = [0, 1, 2].map((k) => Math.max(...g.positions.map((q) => q[k])));
  return { lo, hi, lc: lo.map((x, i) => (x + hi[i]) / 2) };
};
