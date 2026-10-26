// The page's calls to tools/bh_studio.py.
async function call(url, options) {
  const res = await fetch(url, options);
  let body = {};
  try { body = await res.json(); } catch (e) { /* not JSON */ }
  if (!res.ok) throw new Error(body.error || res.statusText);
  return body;
}
const send = (method, url, body) => call(url, { method, headers: { "Content-Type": "application/json" }, body: JSON.stringify(body || {}) });

export const api = {
  scene: () => call("/api/scene"),
  project: () => call("/api/project"),
  saveProject: (body) => send("PUT", "/api/project", body),
  model: (file, game, scenario) => call("/api/model?file=" + encodeURIComponent(file) + (game ? `&game=${game}&scenario=${scenario}` : "")),
  levels: () => call("/api/levels"),
  character: (kind, cls) => call(`/api/character?kind=${kind}&cls=${cls}`),
  characters: () => call("/api/characters"),
  messages: () => call("/api/messages"),
  build: (edits, english = true) => send("POST", "/api/build", { edits, english }),
  play: (patch, hops, at) => send("POST", "/api/play", { patch, hops, at }),
  status: () => call("/api/status"),
  stop: () => send("POST", "/api/stop"),
  switchTo: (game, scenario) => send("POST", "/api/switch", { game, scenario }),
  re4Catalog: () => call("/api/re4/catalog"),
  re4Model: (id) => call("/api/re4/model?id=" + encodeURIComponent(id)),
  gamesList: () => call("/api/games/list"),
  gamesCatalog: (game, category) => call(`/api/games/catalog?game=${encodeURIComponent(game || "all")}${category ? `&category=${encodeURIComponent(category)}` : ""}`),
  gameModel: (game, id) => call(`/api/games/model?game=${encodeURIComponent(game)}&id=${encodeURIComponent(id)}`),
  gameSoundUrl: (game, id) => `/api/games/sound?game=${encodeURIComponent(game)}&id=${encodeURIComponent(id)}`,
  gameTexUrl: (game, id) => `/api/games/tex?game=${encodeURIComponent(game)}&id=${encodeURIComponent(id)}`,
};
