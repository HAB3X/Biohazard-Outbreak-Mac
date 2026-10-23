// Rules: what a room does as the player comes in. Each is one action, optionally only when a flag is on or off; the page keeps them as words and the build puts them at the start of the room's script.
import { store, commit, roomOf } from "./scene.js";
import { el, number, select as selectBox, section } from "./ui.js";

const FLAG_BASE = 200;                       // the game's own scripts use flags 1 to 129; these are free (measured)
const op = (cls, code) => (cls << 24) | code;

export const ACTIONS = [
  { id: "flag", label: "Set a flag", args: [["flag", "Flag", 1, 32, 1], ["value", "To", 0, 1, 1]],
    words: (a) => [[op(2, 0x0C), 0, FLAG_BASE + a.flag - 1, a.value]] },
  { id: "lock", label: "Lock a door", args: [["id", "Door number", 0, 255, 0], ["message", "Message", 1, 999, 201]],
    words: (a) => [[op(2, 0x5F), a.id, a.message, 0]] },
  { id: "examine", label: "Set an examine message", args: [["id", "Spot number", 0, 255, 0], ["message", "Message", 1, 999, 1]],
    words: (a) => [[op(2, 0x55), a.id, a.message, 0]] },
  { id: "show", label: "Show an object", args: [["n", "Object", 0, 255, 0]], words: (a) => [[op(1, 0xB0), a.n]] },
  { id: "hide", label: "Hide an object", args: [["n", "Object", 0, 255, 0]], words: (a) => [[op(1, 0xB1), a.n]] },
  { id: "counter", label: "Set a counter", args: [["index", "Counter", 0, 7, 0], ["value", "To", 0, 9999, 1]],
    words: (a) => [[op(2, 0x110), a.index, a.value, 0]] },
];
const byId = Object.fromEntries(ACTIONS.map((a) => [a.id, a]));

export const newRule = () => ({ when: null, act: "flag", flag: 1, value: 1 });

// the lines of script words for the rules: `If flag {action} E4 E5` (measured: both end markers are needed), or the action alone
export function compile(rules) {
  const lines = [];
  for (const r of rules) {
    const a = byId[r.act];
    if (!a) continue;
    if (r.when) lines.push([op(2, 0xE6), FLAG_BASE + r.when.flag - 1, r.when.on ? 1 : 0, 0]);
    lines.push(...a.words(r));
    if (r.when) lines.push([0xE4], [0xE5]);
  }
  return lines;
}

export function section_(r, host) {
  if (store.game !== "file2") return section("When the room starts", el("div", { class: "note" }, "Rules are measured on File 2 only so far; File 1's scripts use other numbers."));
  r.rules = r.rules || [];
  const redraw = () => host();
  const done = (label) => () => { commit(label); };
  const rows = r.rules.map((x, k) => {
    const a = byId[x.act] || ACTIONS[0];
    const card = el("div", { class: "rulecard" });
    const cond = el("div", { class: "rule" },
      selectBox(x.when ? (x.when.on ? "on" : "off") : "always", [{ value: "always", label: "Always" }, { value: "on", label: "If flag is on" }, { value: "off", label: "If flag is off" }],
        (v) => { x.when = v === "always" ? null : { flag: x.when ? x.when.flag : 1, on: v === "on" }; commit("Edit rule"); }));
    if (x.when) cond.append(number(x.when.flag, (v) => { x.when.flag = v; }, done("Edit rule"), { min: 1, max: 32, scrub: false }));
    cond.append(el("button", { class: "ghost", title: "Remove", onclick: () => { r.rules.splice(k, 1); commit("Remove rule"); } }, "×"));
    const doing = el("div", { class: "rule" }, selectBox(x.act, ACTIONS.map((o) => ({ value: o.id, label: o.label })), (v) => { Object.assign(x, newRule(), { when: x.when, act: v }); for (const [key, , , , d] of byId[v].args) x[key] = d; commit("Edit rule"); }));
    const args = el("div", { class: "rule" });
    for (const [key, label, min, max] of a.args) args.append(el("span", {}, label), number(x[key], (v) => { x[key] = v; }, done("Edit rule"), { min, max, scrub: false }));
    card.append(cond, doing, args);
    if (x.message) card.append(el("div", { class: "note", style: "margin:2px 0 0" }, (store.messages || [])[x.message - 1] || "(no such message)"));
    return card;
  });
  const add = el("button", { class: "ghost", onclick: () => { r.rules.push(newRule()); commit("Add rule"); } }, "Add a rule");
  return section("When the room starts", ...rows, r.rules.length ? null : el("div", { class: "note" }, "Nothing yet. Rules run as the player comes in: set a flag in one room, lock a door in another until it is set."), add);
}
