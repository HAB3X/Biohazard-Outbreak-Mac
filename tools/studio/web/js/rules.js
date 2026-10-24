// Rules: what a room does as the player comes in. Each is one action, optionally only when a flag is on or off; the page keeps them as words and the build puts them at the start of the room's script.
import { store, commit, roomOf } from "./scene.js";
import { el, number, select as selectBox, section } from "./ui.js";

const FLAG_BASE = 200;                       // the game's own scripts use flags 1 to 129; these are free (measured)
const op = (cls, code) => (cls << 24) | code;

export const ACTIONS = [
  { id: "flag", label: "Set a flag", args: [["flag", "Flag", 1, 32, 1], ["value", "To", 0, 1, 1]],
    words: (a) => [[op(2, 0x0B), 0, FLAG_BASE + a.flag - 1, a.value]] },
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

// Rules are put at the start of the room's second script thread, which the game runs again and again, so a rule is tested all the time (measured: a zone's test and a flag's test both
// worked from there). The flags are the game's first flag bank, which a zone sets (op 0x157) and IfFlagAB reads (the other tests read other banks: measured).

// the lines of script words for the rules: `IfFlagAB flag, 1` + the action + `0xE4` `0xE5` (measured: both end markers are needed), or the action alone; a zone rule is the zone test
export function compile(rules) {
  const lines = [];
  for (const r of rules) {
    if (r.when && r.when.zone !== undefined) {         // when the player is inside the zone: the flag is set
      lines.push([op(3, 0x157), r.when.zone, 0, FLAG_BASE + (r.flag || 1) - 1, 0, 4, 0, 0]);
      continue;
    }
    const a = byId[r.act];
    if (!a) continue;
    if (r.when && r.when.item !== undefined) lines.push([op(2, 0xED), r.when.item, 1, 0]);       // picking an item up sets the game's own flag of its pickup trigger's number (measured), which IfGlobal2 reads
    else if (r.when) lines.push([op(2, 0xEC), FLAG_BASE + r.when.flag - 1, r.when.on ? 1 : 0, 0]);
    lines.push(...a.words(r));
    if (r.when) lines.push([0xE4], [0xE5]);
  }
  return lines;
}

export function section_(r, host) {
  r.rules = r.rules || [];
  const pickups = r.areas.filter((a) => a.tag === 3).map((a) => {
    const q = a.quad, cx = q.reduce((s, p) => s + p[0], 0) / q.length, cz = q.reduce((s, p) => s + p[1], 0) / q.length;
    const near = r.items.slice().sort((p, s) => Math.hypot(p.pos[0] - cx, p.pos[2] - cz) - Math.hypot(s.pos[0] - cx, s.pos[2] - cz))[0];
    return { id: a.fields[0], label: `Pickup ${a.fields[0]}${near ? " (the item at " + Math.round(near.pos[0]) + ", " + Math.round(near.pos[2]) + ")" : ""}` };
  });
  const zones = r.areas.filter((a) => a.tag === 12 && a.fields[0] === 0).map((a) => ({ id: a.fields[1] }));      // the room's script zones, by the number a zone's rule names
  const redraw = () => host();
  const done = (label) => () => { commit(label); };
  const rows = r.rules.map((x, k) => {
    const a = byId[x.act] || ACTIONS[0];
    const card = el("div", { class: "rulecard" });
    const cond = el("div", { class: "rule" },
      selectBox(x.when ? (x.when.item !== undefined ? "item" : x.when.zone !== undefined ? "zone" : x.when.on ? "on" : "off") : "always", [{ value: "always", label: "Always" }, { value: "on", label: "If flag is on" }, { value: "off", label: "If flag is off" }, { value: "zone", label: "While someone is in a zone" }, { value: "item", label: "Once an item is taken" }],
        (v) => { x.when = v === "always" ? null : v === "item" ? { item: (pickups[0] || { id: 0 }).id } : v === "zone" ? { zone: (zones[0] || { id: 0 }).id } : { flag: x.when && x.when.flag ? x.when.flag : 1, on: v === "on" }; if (v === "zone") x.act = "flag"; commit("Edit rule"); }));
    if (x.when && x.when.item !== undefined) cond.append(selectBox(x.when.item, pickups.length ? pickups.map((p) => ({ value: p.id, label: p.label })) : [{ value: 0, label: "no pickup triggers here" }], (v) => { x.when.item = v; commit("Edit rule"); }));
    else if (x.when && x.when.zone !== undefined) cond.append(selectBox(x.when.zone, zones.length ? zones.map((z) => ({ value: z.id, label: `Zone ${z.id}` })) : [{ value: 0, label: "no zones: add one" }], (v) => { x.when.zone = v; commit("Edit rule"); }));
    else if (x.when) cond.append(number(x.when.flag, (v) => { x.when.flag = v; }, done("Edit rule"), { min: 1, max: 32, scrub: false }));
    cond.append(el("button", { class: "ghost", title: "Remove", onclick: () => { r.rules.splice(k, 1); commit("Remove rule"); } }, "×"));
    const doing = el("div", { class: "rule" }, selectBox(x.act, ACTIONS.map((o) => ({ value: o.id, label: o.label })), (v) => { Object.assign(x, newRule(), { when: x.when, act: v }); for (const [key, , , , d] of byId[v].args) x[key] = d; commit("Edit rule"); }));
    const args = el("div", { class: "rule" });
    for (const [key, label, min, max] of a.args) args.append(el("span", {}, label), number(x[key], (v) => { x[key] = v; }, done("Edit rule"), { min, max, scrub: false }));
    card.append(cond, doing, args);
    if (x.message) card.append(el("div", { class: "note", style: "margin:2px 0 0" }, (store.messages || [])[x.message - 1] || "(no such message)"));
    return card;
  });
  const add = el("button", { class: "ghost", onclick: () => { r.rules.push(newRule()); commit("Add rule"); } }, "Add a rule");
  return section("Rules", ...rows, r.rules.length ? null : el("div", { class: "note" }, "Nothing yet. A rule is tested all the time while the room is loaded: set a flag when someone steps into a zone, then lock or unlock things by that flag."), add);
}
