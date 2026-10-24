// A short tour for the first visit (and from the ? button): a few cards, one thing each.
import { el } from "./ui.js";

const KEY = "studio-tour-done";
const STEPS = [
  ["Look around", "<b>W A S D</b> fly, <b>Q E</b> go down and up, <b>right-drag</b> to look. What you see is the game's own picture; it follows you a moment after you stop."],
  ["Pick things up", "<b>Click</b> anything to select it, then <b>drag</b> it. Arrows slide it along one way, the ring turns it, the diamond resizes it. <b>Del</b> removes, <b>Ctrl+D</b> copies, <b>Ctrl+Z</b> undoes."],
  ["Put things down", "<b>Library</b> (bottom) has the game's own props: search, click one, click in the room, <b>R</b> turns it. <b>Add</b> has doors, enemies, items, walls and more."],
  ["Rooms and rules", "<b>Rooms</b> (top) lists every room. <b>This room</b> has its rules, puzzles and a way to start a level from nothing."],
  ["Try it", "<b>Play</b> (top right) starts the game with your changes. The <b>?</b> button shows this again."],
];

export function startTour(force) {
  let seen = false;
  try { seen = !!localStorage.getItem(KEY); } catch (e) { /* no storage: the tour shows each time */ }
  if (seen && !force) return;
  let at = 0;
  const card = el("div", { class: "float", id: "tour" });
  const show = () => {
    const [title, body] = STEPS[at];
    card.innerHTML = `<div class="tstep">${at + 1} of ${STEPS.length}</div><b>${title}</b><p>${body}</p>`;
    const row = el("div", { class: "trow" });
    const skip = el("button", { class: "link" }, "Skip");
    const next = el("button", { class: "btn solid" }, at === STEPS.length - 1 ? "Got it" : "Next");
    skip.addEventListener("click", done); next.addEventListener("click", () => { if (at === STEPS.length - 1) done(); else { at++; show(); } });
    row.append(skip, next); card.append(row);
  };
  const done = () => { card.remove(); try { localStorage.setItem(KEY, "1"); } catch (e) { /* nothing remembered */ } };
  show();
  document.getElementById("view").append(card);
}
