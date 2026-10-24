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
  model: (file) => call("/api/model?file=" + encodeURIComponent(file)),
  messages: () => call("/api/messages"),
  build: (edits, english = true) => send("POST", "/api/build", { edits, english }),
  play: (patch, hops, at) => send("POST", "/api/play", { patch, hops, at }),
  status: () => call("/api/status"),
  stop: () => send("POST", "/api/stop"),
  previewPrepare: (edits) => send("POST", "/api/preview/prepare", { edits }),
  previewShot: (room, hops, at, name, camera) => send("POST", "/api/preview/shot", { room, hops, at, name, camera }),
  survey: () => call("/api/survey"),
  surveyStart: () => send("POST", "/api/survey/start"),
  switchTo: (game, scenario) => send("POST", "/api/switch", { game, scenario }),
};
