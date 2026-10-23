// Small form controls. Each takes the current value and a callback; `live` is called while a value is being changed (the scene follows), `done` when it settles (a step of history).
import { icon } from "./icons.js";

export const el = (tag, attrs = {}, ...kids) => {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (k === "class") e.className = v;
    else if (k === "html") e.innerHTML = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else if (v !== false && v !== null && v !== undefined) e.setAttribute(k, v === true ? "" : v);
  }
  for (const kid of kids.flat()) if (kid !== null && kid !== undefined && kid !== false) e.append(kid.nodeType ? kid : document.createTextNode(kid));
  return e;
};

const fmt = (v, step) => (Number.isFinite(v) ? (step && step < 1 ? +v.toFixed(3) : Math.round(v * 1000) / 1000) : "");

// a number you can type into or scrub by dragging the little handle
export function number(value, live, done, o = {}) {
  const step = o.step ?? 1;
  const input = el("input", { type: "text", inputmode: "decimal", value: fmt(value, step), spellcheck: "false" });
  const wrap = el("div", { class: "num" });
  let start = null;
  if (o.axis) wrap.append(el("span", { class: "axis " + o.axis }, o.axis.toUpperCase()));
  else if (o.scrub !== false) {
    const grip = el("span", { class: "scrub", html: icon("scrub", 12) });
    grip.addEventListener("pointerdown", (e) => { grip.setPointerCapture(e.pointerId); start = { x: e.clientX, v: parseFloat(input.value) || 0 }; });
    grip.addEventListener("pointermove", (e) => {
      if (!start) return;
      const k = e.shiftKey ? 0.1 : e.altKey ? 10 : 1;
      let v = start.v + Math.round((e.clientX - start.x) / 3) * step * k;
      if (o.min !== undefined) v = Math.max(o.min, v);
      if (o.max !== undefined) v = Math.min(o.max, v);
      input.value = fmt(v, step); live(v);
    });
    grip.addEventListener("pointerup", () => { if (start) { start = null; done && done(parseFloat(input.value)); } });
    wrap.append(grip);
  }
  const parse = () => {
    let v = parseFloat(String(input.value).replace(",", "."));
    if (!Number.isFinite(v)) v = value;
    if (o.min !== undefined) v = Math.max(o.min, v);
    if (o.max !== undefined) v = Math.min(o.max, v);
    return v;
  };
  input.addEventListener("input", () => { const v = parseFloat(input.value); if (Number.isFinite(v)) live(v); });
  input.addEventListener("change", () => { const v = parse(); input.value = fmt(v, step); live(v); done && done(v); });
  input.addEventListener("keydown", (e) => {
    if (e.key === "ArrowUp" || e.key === "ArrowDown") {
      e.preventDefault();
      const k = (e.shiftKey ? 10 : e.altKey ? 0.1 : 1) * (e.key === "ArrowUp" ? 1 : -1);
      const v = (parseFloat(input.value) || 0) + step * k;
      input.value = fmt(v, step); live(v); done && done(v);
    }
    if (e.key === "Enter") input.blur();
  });
  wrap.append(input);
  if (o.unit) wrap.append(el("span", { class: "unit" }, o.unit));
  return wrap;
}

export function vec3(value, live, done, o = {}) {
  const v = value.slice();
  return el("div", { class: "vec" }, ["x", "y", "z"].map((axis, k) => number(v[k], (n) => { v[k] = n; live(v.slice()); }, (n) => { v[k] = n; done && done(v.slice()); }, { ...o, axis, scrub: false })));
}

export function select(value, options, change) {
  const s = el("select", { class: "sel" });
  const list = options.some((x) => String(x.value) === String(value)) ? options : [{ value, label: `#${value} (not in the list)` }, ...options];
  for (const opt of list) s.append(el("option", { value: opt.value, selected: String(opt.value) === String(value) }, opt.label));
  s.addEventListener("change", () => change(isNaN(+s.value) ? s.value : +s.value));
  return s;
}

export function text(value, change) {
  const t = el("input", { class: "text", value: value ?? "", spellcheck: "false" });
  t.addEventListener("change", () => change(t.value));
  return t;
}

export function range(value, min, max, step, live, done) {
  const r = el("input", { class: "range", type: "range", min, max, step, value });
  r.addEventListener("input", () => live(+r.value));
  r.addEventListener("change", () => done && done(+r.value));
  return r;
}

export function chips(options, isOn, toggle) {
  const box = el("div", { class: "chips" });
  options.forEach((opt) => {
    const b = el("button", { type: "button", class: isOn(opt.value) ? "on" : "", title: opt.tip || "" }, opt.label);
    b.addEventListener("click", () => { toggle(opt.value); b.classList.toggle("on", isOn(opt.value)); });
    box.append(b);
  });
  return box;
}

// a facing: a dial you drag, 65536 to a turn, 0 pointing down the plan (+z)
export function dial(value, live, done) {
  const needle = el("i");
  const d = el("div", { class: "dial", title: "Drag to turn" }, needle);
  const set = (v) => { needle.style.transform = `rotate(${(-v / 65536) * 360 + 180}deg)`; };
  set(value);
  let on = false;
  const turn = (e) => {
    const r = d.getBoundingClientRect(), a = Math.atan2(e.clientX - (r.left + r.width / 2), e.clientY - (r.top + r.height / 2));
    let v = Math.round(((a / (Math.PI * 2)) * 65536 + 65536) % 65536);
    if (!e.altKey) v = Math.round(v / 1024) * 1024 % 65536;
    set(v); live(v); return v;
  };
  d.addEventListener("pointerdown", (e) => { d.setPointerCapture(e.pointerId); on = true; turn(e); });
  d.addEventListener("pointermove", (e) => { if (on) turn(e); });
  d.addEventListener("pointerup", (e) => { if (on) { on = false; done && done(turn(e)); } });
  return d;
}

export function colour(hex, live, done) {
  const c = el("input", { class: "swatch", type: "color", value: hex });
  c.addEventListener("input", () => live(c.value));
  c.addEventListener("change", () => done && done(c.value));
  return c;
}

export function field(label, control, hint) {
  const f = el("div", { class: "field" + (control.classList && control.classList.contains("vec") ? " stack" : "") }, el("label", {}, label), control);
  if (hint) f.append(el("div", { class: "hintline" }, hint));
  return f;
}

export function section(title, ...kids) {
  return el("div", { class: "sect" }, title ? el("h4", {}, title) : null, ...kids);
}

export const hex = (r, g, b) => "#" + [r, g, b].map((v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0")).join("");
export const unhex = (h) => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];

// ---- toasts
export function toast(message, kind = "", detail = "", ms = 3200) {
  const box = document.getElementById("toasts");
  const t = el("div", { class: "toast " + kind }, message, detail ? el("small", {}, detail) : null);
  box.append(t);
  setTimeout(() => t.remove(), ms);
}
