#!/usr/bin/env python3
"""Bringing one game's enemy AI into the other: the two games share their engine (the same functions at other addresses), so a creature's own code can be
carried across once every engine function and every piece of engine data it uses is found in the other game.

This module is the first half: the two games' code side by side.

    Code(game)                      the executable and its overlays, as they load (address -> 32-bit word), with the recompiler's function list
    match(a, b)                     {function address in a: the same function's address in b}: functions alike in every instruction but the addresses
                                    they hold, first those alike in only one place each, then the calls of matched functions in step
    data_map(a, b, functions)       {data address in a: the same data's address in b}, read off the matched functions' address-forming instructions

    bh_transplant.py match GAME_FROM GAME_TO     how much of the engine matches
    bh_transplant.py build GAME_FROM GAME_TO OUT KIND...      the kinds' AI for GAME_TO, as the files recomp.py's generated overlay reads

Instruction words: the MIPS fields that hold addresses (a J/JAL target, the 16-bit immediate of LUI, ADDIU, ORI and the loads and stores) are left out of a
function's fingerprint; everything else (opcodes, registers, shifts, branch distances inside the function) must be the same.
"""
import struct
import sys
import tomllib
from pathlib import Path

import bh_archive as archive

ROOT = Path(__file__).resolve().parent.parent
EXECUTABLE = {"file1": "SLPM_654.28", "file2": "SLPM_656.92"}
# the overlays that are in memory together in a scenario (the game's own code, the submain overlay its data tables live in, the single play overlay)
IN_PLAY = ("submain", "game", "single")


class Code:
    """The executable and the overlays in play as they load, with their functions found by the recompiler's own analysis (the same starts and ends its
    generated code has, read here from the disc so that a game's build can use another's code before either is recompiled)"""

    def __init__(self, game):
        import os
        import tempfile

        recomp = Path(os.environ.get("PS2_RUNTIME_DIR", ROOT / "ps2-native-runtime")) / "recomp"
        sys.path.insert(0, str(recomp))
        from analysis import Program
        from elf import Elf
        from image import CodeImage
        from recomp import load_config

        self.game = game
        config = load_config(ROOT / "config" / f"{game}.toml")
        disc = archive.Archive(game)
        with tempfile.NamedTemporaryFile(suffix=".elf") as f:
            f.write(disc.open("/" + EXECUTABLE[game]).read())
            f.flush()
            elf = Elf(f.name)
        base, image = elf.image()
        main = elf.segments[0]                          # (the others hold no file: the overlays' places)
        self.segments = {"main": (base, image[:main["vaddr"] + main["filesz"] - base])}     # name -> (base, bytes)
        program = Program(CodeImage.from_elf(elf, config["text"], config["vutext"]), config)
        self.functions = {"main": [(s, e, f"f_{s:08x}") for s, e in program.analyse()]}
        self.jump_targets = {a for targets in program.jump_tables.values() for a in targets}     # (reached by JR through a table)
        for overlay in config["overlays"]:
            if overlay["name"] in IN_PLAY:
                data = disc.open("/" + overlay["file"]).read()
                self.segments[overlay["name"]] = (overlay["base"], data)
                program = Program(CodeImage.from_overlay(data, overlay["base"], overlay["text"]), overlay)
                found = program.analyse()
                self.jump_targets |= {a for targets in program.jump_tables.values() for a in targets}
                self.functions[overlay["name"]] = [(s, e, f"{overlay['name']}_{s:08x}") for s, e in found]

    def word(self, segment, address):
        base, data = self.segments[segment]
        return struct.unpack_from("<I", data, address - base)[0]

    def words(self, segment, start, end):
        base, data = self.segments[segment]
        return list(struct.unpack_from(f"<{(end - start) // 4}I", data, start - base))


ADDRESS_IMMEDIATE = {9, 13, 15} | set(range(32, 64))        # ADDIU, ORI, LUI, the loads and stores
GP = {"file1": 0x241C70, "file2": 0x2510F0}                    # each executable's gp (its start-up code: lui a0 / addiu a0 / move gp, a0)
# each game's map of enemy kind to AI routine (u16 a kind) and its table of routines, in the game overlay's data (found: File 1's read by the enemy creation
# at 0x689xxx; File 2's by its contents, the kinds its scenarios have and no others)
KIND_ROUTINES = {"file1": (0x6D7040, 0x6D7110, 30), "file2": (0x719150, 0x719220, 47)}
KINDS = 66
# where each game's transplanted code goes: memory no game data ever holds (zero in every dump taken: File 1 0x1D7E150-0x1EB91DC)
BASE = {"file1": 0x1D80000}
# memory below 0x8000, zero in every File 1 dump (the kernel's place on the console; the runtime keeps none of it), where one instruction based on $zero
# reaches what the carried code reached from gp: the globals File 1 hasn't that it reaches so are put here (not the first 16 KB: a null pointer's fields)
LOW = (0x4000, 0x8000)
# the memory addresses the games' code forms (main memory above the kernel's): any other value an address-forming pair makes is a constant
MEMORY = (0x100000, 0x2000000)


def signed16(v):
    return v - 0x10000 if v & 0x8000 else v


def fingerprint(words):
    out = []
    for w in words:
        op = w >> 26
        if op in (2, 3):
            out.append(op << 26)
        elif op in ADDRESS_IMMEDIATE:
            out.append(w & 0xFFFF0000)
        else:
            out.append(w)
    return tuple(out)


def calls(words, start):
    """[(index of the instruction, target)] of a function's JALs"""
    return [(i, ((start + 4 * i) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)) for i, w in enumerate(words) if w >> 26 == 3]


def match(a, b):
    """{(segment, address in a): (segment, address in b)} for the functions found in both"""
    def index(code):
        out, by_address = {}, {}
        for segment, functions in code.functions.items():
            for start, end, name in functions:
                words = code.words(segment, start, end)
                key = (segment, fingerprint(words))
                out.setdefault(key, []).append(start)
                by_address[start] = (segment, start, end, words)
        return out, by_address
    ia, fa = index(a)
    ib, fb = index(b)
    pairs = {}
    for key, starts in ia.items():
        if len(starts) == 1 and len(ib.get(key, [])) == 1:
            pairs[starts[0]] = ib[key][0]
    # the calls of matched functions, in step: the n-th call of a matched pair goes to the same function
    grew = True
    while grew:
        grew = False
        for x, y in list(pairs.items()):
            ca, cb = calls(fa[x][3], x), calls(fb[y][3], y)
            if len(ca) != len(cb):
                continue
            for (i, ta), (j, tb) in zip(ca, cb):
                if i == j and ta not in pairs and ta in fa and tb in fb and len(fa[ta][3]) == len(fb[tb][3]):
                    pairs[ta] = tb
                    grew = True
    return pairs, fa, fb


def data_map(fa, fb, pairs, gp_a=None, gp_b=None):
    """{address in a: address in b} of the data the matched functions name with LUI + ADDIU/ORI/load/store pairs or by gp, where both name one"""
    votes = {}
    for x, y in pairs.items():
        wa, wb = fa[x][3], fb[y][3]
        if len(wa) != len(wb):
            continue
        hi_a, hi_b = {}, {}
        for i, (p, q) in enumerate(zip(wa, wb)):
            op = p >> 26
            if op == 15:                                       # LUI rt, upper
                hi_a[(p >> 16) & 31] = (p & 0xFFFF) << 16
                hi_b[(q >> 16) & 31] = (q & 0xFFFF) << 16
            elif op in ADDRESS_IMMEDIATE and gp_a is not None and (p >> 21) & 31 == 28 and (q >> 21) & 31 == 28:
                pa, pb = (gp_a + signed16(p & 0xFFFF)) & 0xFFFFFFFF, (gp_b + signed16(q & 0xFFFF)) & 0xFFFFFFFF
                votes.setdefault(pa, {}).setdefault(pb, 0)
                votes[pa][pb] += 1
            elif op in ADDRESS_IMMEDIATE and op != 15:
                rs = (p >> 21) & 31
                if rs in hi_a and rs in hi_b:
                    lo_a = struct.unpack("<h", struct.pack("<H", p & 0xFFFF))[0] if op != 13 else p & 0xFFFF
                    lo_b = struct.unpack("<h", struct.pack("<H", q & 0xFFFF))[0] if op != 13 else q & 0xFFFF
                    pa, pb = (hi_a[rs] + lo_a) & 0xFFFFFFFF, (hi_b[rs] + lo_b) & 0xFFFFFFFF
                    votes.setdefault(pa, {}).setdefault(pb, 0)
                    votes[pa][pb] += 1
    return {pa: max(c, key=c.get) for pa, c in votes.items()}


def writes(w):
    """The general register an instruction sets, or None"""
    op = w >> 26
    if op == 0:
        funct = w & 63
        return None if funct in (8, 12, 13, 15, 24, 25, 26, 27) else (w >> 11) & 31     # (jr, syscall, break, sync, mult/div: none)
    if op == 3:
        return 31
    if op in (8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 30) or 32 <= op <= 39 or op == 55:
        return (w >> 16) & 31
    if op == 17 and (w >> 21) & 31 in (0, 2):                     # mfc1, cfc1
        return (w >> 16) & 31
    return None


def _branch_offset(w):
    """True for an instruction with a 16-bit branch offset"""
    op = w >> 26
    return op in (1, 4, 5, 6, 7, 20, 21, 22, 23) or (op in (17, 18) and (w >> 21) & 31 == 8)


def _likely(w):
    """True for a branch whose delay slot runs only when it is taken"""
    op = w >> 26
    return op in (20, 21, 22, 23) or (op == 1 and (w >> 16) & 31 in (2, 3, 18, 19)) or (op in (17, 18) and (w >> 21) & 31 == 8 and (w >> 16) & 2)


def _has_delay_slot(w):
    op = w >> 26
    return _branch_offset(w) or op in (2, 3) or (op == 0 and w & 63 in (8, 9))


def _always(w):
    """True for a transfer that never falls through: JR, J, B (BEQ $0, $0)"""
    op = w >> 26
    return (op == 0 and w & 63 == 8) or op == 2 or (op == 4 and (w >> 16) & 0x3FF == 0)


def _ends(w):
    """True when a function's code doesn't fall through its end into the next (past the zero words that pad it)"""
    n = len(w)
    while n and w[n - 1] == 0:
        n -= 1
    return any(_always(w[k]) for k in (n - 2, n - 1) if k >= 0)


CALLER_SAVED = frozenset(range(1, 16)) | {24, 25, 31}


def _flow_formed(function, jump_targets):
    """_formed's work over one function (segment, start, end, words): the LUIs reaching each address-forming use, followed forward from the start
    through every branch to a fixed point. A register holds {(LUI index, value)}; where paths meet it keeps those all of them have it from"""
    seg, s, e, w = function
    n = len(w)

    def edges(i):
        """(successor, whether a call came between)"""
        x = w[i]
        if _has_delay_slot(x):
            return [(i + 1, False)] + ([(i + 2, False)] if _likely(x) else [])
        if i > 0 and _has_delay_slot(w[i - 1]):
            b = w[i - 1]
            op = b >> 26
            out = []
            if _branch_offset(b):
                target = i + signed16(b & 0xFFFF)
                if 0 <= target < n:
                    out.append((target, False))
                if not _always(b) and not _likely(b):
                    out.append((i + 1, False))
            elif op == 2:
                target = (((s + 4 * i) & 0xF0000000) | ((b & 0x3FFFFFF) << 2)) - s
                if 0 <= target < 4 * n:
                    out.append((target // 4, False))
            elif op == 3 or (op == 0 and b & 63 == 9):
                out.append((i + 1, True))
            return [(k, c) for k, c in out if k < n]
        return [(i + 1, False)] if i + 1 < n else []

    def transfer(i, state):
        x = w[i]
        out = dict(state)
        r = writes(x)
        if r is not None:
            out.pop(r, None)
        if x >> 26 == 15:
            out[(x >> 16) & 31] = frozenset({(i, (x & 0xFFFF) << 16)})
        return out

    entry = {s + 4 * i for i in range(n)} & jump_targets
    states = [None] * n
    states[0] = {}
    work = [0]
    while work:
        i = work.pop()
        out = transfer(i, states[i])
        for k, call in edges(i):
            if k >= n:
                continue
            st = {r: v for r, v in out.items() if r not in CALLER_SAVED} if call else out
            if s + 4 * k in entry:
                st = {}
            if states[k] is None:
                new = st
            else:
                new = {r: states[k][r] | st[r] for r in states[k] if r in st}
            if new != states[k]:
                states[k] = new
                work.append(k)
    found = []
    for i, x in enumerate(w):
        op = x >> 26
        rs = (x >> 21) & 31
        if states[i] is None or not (op in (9, 13) or 32 <= op < 64) or rs not in states[i]:
            continue
        his = states[i][rs]
        if len({v for _, v in his}) != 1:
            continue                                    # (LUIs of different values meet here: no one address)
        lo = signed16(x & 0xFFFF) if op != 13 else x & 0xFFFF
        found.append((tuple(sorted(li for li, _ in his)), i, (next(iter(his))[1] + lo) & 0xFFFFFFFF))
    return found


class Transplant:
    """A creature's AI of game `src` laid out for game `dst` at `base`: its own functions (the ones of its routine's closure the other game hasn't) copied,
    their calls and the addresses they form turned into the other game's, and the record fields they use moved to the other game's record."""

    def __init__(self, src, dst):
        self.src, self.dst = src, dst
        self.a, self.b = Code(src), Code(dst)
        self.pairs, self.fa, self.fb = match(self.a, self.b)
        self.data = data_map(self.fa, self.fb, self.pairs, GP[src], GP[dst])
        self.data_keys = sorted(self.data)
        self.starts = sorted(self.fa)
        self.fields = self._field_map()
        self._formed_cache = {}
        # every address the source's code forms: each names a piece of data of its own
        self.labels = sorted({a for f in self.fa for _, _, a in self._formed(f)})
        self.label_set = set(self.labels)

    # ---- what the source has
    def routine(self, kind):
        at, table, count = KIND_ROUTINES[self.src]
        base, data = self.a.segments["game"]
        index = struct.unpack_from("<h", data, at - base + 2 * kind)[0]
        return struct.unpack_from("<I", data, table - base + 4 * index)[0] if index else None

    def function_at(self, address):
        import bisect
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and self.fa[self.starts[i]][1] <= address < self.fa[self.starts[i]][2]:
            return self.starts[i]
        return None

    def word(self, address):
        for base, data in self.a.segments.values():
            if base <= address and address + 4 <= base + len(data):
                return struct.unpack_from("<I", data, address - base)[0]
        return None

    def _formed(self, start):
        """[(the LUIs' indices, index of the instruction using them, the address formed)] of a function: the addresses its LUI + ADDIU/ORI/load/
        store pairs form, followed through its branches (a use reached from two places takes the LUIs of both, when they agree; kind 32's use at
        0x5D49F0 is reached only by a branch, from a LUI of its own, not from the LUI just above it)"""
        if start not in self._formed_cache:
            self._formed_cache[start] = _flow_formed(self.fa[start], self.a.jump_targets)
        return self._formed_cache[start]

    def closure(self, start):
        """The functions of `start`'s closure the destination hasn't (its calls, the code addresses it forms, the tables of code addresses it names)"""
        own, seen, todo, forced = [], set(), [start], set()
        while todo:
            f = todo.pop()
            if f in seen or f not in self.fa:
                continue
            seen.add(f)
            if f in self.pairs and f not in forced:
                continue
            own.append(f)
            seg, s, e, w = self.fa[f]
            todo += [t for _, t in calls(w, s)]
            # a branch into another function, or falling through into the next, needs that function carried beside it (even one the destination
            # has: a branch can't reach it there)
            beside = [s + 4 * (i + 1 + signed16(x & 0xFFFF)) for i, x in enumerate(w) if _branch_offset(x)]
            beside = [self.function_at(a) for a in beside if not s <= a < e]
            if not _ends(w):
                beside.append(e)
            for g in beside:
                if g is not None and g not in seen and g in self.fa:
                    forced.add(g)
                    todo.append(g)
            todo += [((s + 4 * i) & 0xF0000000) | ((x & 0x3FFFFFF) << 2) for i, x in enumerate(w) if x >> 26 == 2 and not (s <= (((s + 4 * i) & 0xF0000000) | ((x & 0x3FFFFFF) << 2)) < e)]
            for _, _, address in self._formed(f):
                if self.function_at(address) == address:
                    todo.append(address)
                else:
                    for k in range(256):                      # a table of code addresses there (a state machine's handlers)
                        v = self.word(address + 4 * k)
                        if v is None or self.function_at(v) != v:
                            break
                        todo.append(v)
        return own

    # ---- the record's fields
    def _field_map(self):
        votes = {}
        for x, y in self.pairs.items():
            wa, wb = self.fa[x][3], self.fb[y][3]
            if len(wa) != len(wb):
                continue
            for p, q in zip(wa, wb):
                if 32 <= p >> 26 < 64 and p >> 16 == q >> 16 and (p >> 21) & 31 not in (28, 29):
                    ia, ib = p & 0xFFFF, q & 0xFFFF
                    if 0x400 <= ia < 0x8000 and ib < 0x8000:
                        votes.setdefault(ia, {}).setdefault(ib, 0)
                        votes[ia][ib] += 1
        return {k: max(c, key=c.get) for k, c in votes.items()}

    def field(self, offset):
        """A record field's offset in the destination: the matched functions' agreement where they use it; past them, the per-kind work area at the
        record's end (File 2's 0xDB0 bytes, File 1's 0xD90: the kinds' own code uses 0xD05-0xD95 in File 2 and 0xCFC-0xD7D in File 1), moved with
        the record's end; otherwise as the nearest field with a vote moved"""
        if offset in self.fields:
            return self.fields[offset]
        if offset >= 0xD00:
            return offset + (0xD90 - 0xDB0 if self.src == "file2" else 0xDB0 - 0xD90)
        below = [k for k in self.fields if k < offset and offset - k < 0x40]
        if below:
            k = max(below)
            return offset + self.fields[k] - k
        return offset

    # ---- addresses
    def address(self, a, placed, problems, why):
        """Where address `a` of the source is in the destination: a function laid out here, a matched function, mapped data, data near mapped data
        (a field of a mapped global), or ("copy", a): data the destination hasn't, to be copied with the carried code"""
        f = self.function_at(a)
        if f is not None:
            if f in placed:
                return placed.at(f, a)
            if f in self.pairs and len(self.fa[f][3]) == len(self.fb[self.pairs[f]][3]):
                return self.pairs[f] + (a - f)
            problems.append(Missing(f, f"{why}: code {a:#x} in a function neither matched nor carried"))
            return None
        if a in self.data:
            return self.data[a]
        import bisect
        i = bisect.bisect_right(self.data_keys, a) - 1
        base, data = self.a.segments["game"]
        in_game = base <= a < base + 0x400000
        # (a field of a mapped global; but data of the game overlay the code names as its own is copied: kind 34's state handler table, next to a
        # mapped one, was taken for File 1's data beside that, and its call through it went to 0)
        if i >= 0 and a - self.data_keys[i] < 0x100 and not (in_game and a in self.label_set):
            k = self.data_keys[i]
            return self.data[k] + (a - k)
        if MEMORY[0] <= a < MEMORY[1]:
            return ("copy", a)
        problems.append(f"{why}: data {a:#x} not found in {self.dst}")
        return None

    def read(self, a, size):
        """`size` bytes of the source's memory at `a` as it loads: a segment's file, else zero (its uninitialised data)"""
        for base, data in self.a.segments.values():
            if base <= a < base + len(data):
                out = data[a - base:a - base + size]
                return out + bytes(size - len(out))
        return bytes(size)


class Missing(str):
    """A problem that is a function the carried code needs and doesn't carry (build carries it and builds again)"""

    def __new__(cls, function, text):
        out = super().__new__(cls, text)
        out.function = function
        return out


class Built:
    def __init__(self, base, blob, code_end, low, functions, routines, problems):
        self.base, self.blob, self.code_end = base, blob, code_end
        self.low = low                      # (address, bytes): the copied globals the code reaches from gp, put where $zero reaches (LOW)
        self.functions = functions          # [(start, end)] laid out here
        self.routines = routines            # {kind: the address of its routine here}
        self.problems = problems


def build(t, kinds, base):
    """The creatures `kinds` of t.src as one block of code and data at `base` in t.dst's memory"""
    own, routines = [], {}
    for kind in kinds:
        r = t.routine(kind)
        if r is None:
            raise ValueError(f"{t.src} has no AI for kind {kind}")
        if r in t.pairs:
            routines[kind] = t.pairs[r]
            continue
        own += [f for f in t.closure(r) if f not in own]
        routines[kind] = r
    # built again while it finds more to do: the functions the copied data points at (a kind's handlers in a table of the executable's data)
    # carried too, with their own; the accesses one instruction no longer reaches given an instruction of their own
    expand = {}
    while True:
        built = _Builder(t, own, base, expand).build(routines)
        missing = [p.function for p in built.problems if isinstance(p, Missing) and p.function not in own]
        wide = [p for p in built.problems if isinstance(p, Wide) and p.index not in expand.get(p.function, set())]
        if not missing and not wide:
            return built
        for f in missing:
            own += [g for g in t.closure(f) if g not in own]
        for p in wide:
            expand.setdefault(p.function, set()).add(p.index)


class Wide(str):
    """A problem that is an access whose address one instruction doesn't reach from its base here (build gives it a LUI of its own)"""

    def __new__(cls, function, index, text):
        out = super().__new__(cls, text)
        out.function, out.index = function, index
        return out


AT = 1                                                  # the assembler's temporary: the compiler's code forms addresses in it, pair by pair


class _Builder:
    """build's work: the carried functions laid out from `base` (with a LUI of its own before each access of `expand` {function: {index}}),
    then the data they name that the destination hasn't copied after them (or, what they reach from gp, into LOW), then every address in the
    code and the copied data moved to where it now is"""

    def __init__(self, t, own, base, expand):
        # (in the source's order: a function that falls through into the next, or branches into it, keeps it beside it)
        self.t, self.own, self.base, self.expand = t, sorted(own), base, expand
        self.order_of = {f: self._emission(f) for f in own}
        self.start, at = {}, base
        for f in own:
            self.start[f] = at
            at += 4 * len(self.order_of[f][0])
        self.code_end = at
        self.copies = {}            # source address -> its place here (None until laid out)
        self.order = []             # the copies, in the order found
        self.from_gp = set()        # the copies the code reaches from gp
        self.problems = []

    # ---- where the carried code is
    def _emission(self, f):
        """f's instructions as laid out: [(old index, is the inserted LUI)], and {old index: where its code begins (its LUI's place, or for an
        instruction in a delay slot, its branch's)}"""
        w = self.t.fa[f][3]
        extra = self.expand.get(f, set())
        out, first = [], {}
        for i in range(len(w)):
            if i + 1 in extra and _has_delay_slot(w[i]):
                first[i + 1] = len(out)
                out.append((i + 1, True))
            first.setdefault(i, len(out))
            if i in extra and not (i > 0 and _has_delay_slot(w[i - 1])):
                out.append((i, True))
            out.append((i, False))
        return out, first

    def __contains__(self, f):
        return f in self.start

    def at(self, f, a):
        """Where the source's code address `a` (in carried function f) is here"""
        return self.start[f] + 4 * self.order_of[f][1][(a - f) // 4]

    def resolve(self, a, why):
        r = self.t.address(a, self, self.problems, why)
        if isinstance(r, tuple) and a not in self.copies:
            self.copies[a] = None
            self.order.append(a)
        return r

    def extent(self, a):
        """A copy's size: to the next address the code names (the next piece of data), at most 8 KB"""
        import bisect
        labels = self.t.labels
        i = bisect.bisect_right(labels, a)
        end = labels[i] if i < len(labels) else a + 0x100
        return max(4, min(end - a, 0x2000))

    def relocate(self, f):
        """f's words with its calls moved and its record fields, and (to patch once the copies have their places) its address-forming pairs
        and gp uses"""
        t = self.t
        seg, s, e, w = t.fa[f]
        out = list(w)
        # the LUIs and their uses, in groups that share a LUI (each LUI is given one upper half: its uses' addresses must agree on it)
        pairs = []                              # [(set of LUI indices, [(use index, where its address is here)])]
        for lis, ui, a in t._formed(f):
            if not MEMORY[0] <= a < MEMORY[1]:
                continue                        # (a constant, not an address: a float's upper half, a mask)
            joined = [g for g in pairs if g[0] & set(lis)]
            pairs = [g for g in pairs if not g[0] & set(lis)]
            pairs.append((set(lis).union(*(g[0] for g in joined)), [u for g in joined for u in g[1]] + [(ui, self.resolve(a, f"{s + 4 * ui:#x}"))]))
        paired = {ui for _, items in pairs for ui, _ in items}
        gp_uses = []
        for i, x in enumerate(w):
            op = x >> 26
            pc = s + 4 * i
            if op in (2, 3):
                target = (pc & 0xF0000000) | ((x & 0x3FFFFFF) << 2)
                new = self.at(f, target) if s <= target < e else t.address(target, self, self.problems, f"{pc:#x} call")
                if isinstance(new, int):
                    out[i] = (op << 26) | ((new >> 2) & 0x3FFFFFF)
            elif (32 <= op < 64 or op == 9) and (x >> 21) & 31 == 28:
                a = (GP[t.src] + signed16(x & 0xFFFF)) & 0xFFFFFFFF
                new = self.resolve(a, f"{pc:#x} gp")
                if isinstance(new, tuple):
                    self.from_gp.add(a)
                if new is not None:
                    gp_uses.append((i, new))
            elif 32 <= op < 64 and (x >> 21) & 31 not in (28, 29) and i not in paired:
                imm = x & 0xFFFF
                if 0x400 <= imm < 0x8000:
                    out[i] = (x & 0xFFFF0000) | t.field(imm)
        return out, pairs, gp_uses

    def place(self, r):
        return self.copies[r[1]] if isinstance(r, tuple) else r

    def _at_free(self, f, i):
        """True when $at holds nothing at instruction i of f (no address-forming pair in $at spans it), so a LUI of its own may use it"""
        w = self.t.fa[f][3]
        start = i - 1 if i > 0 and _has_delay_slot(w[i - 1]) else i
        return not any((w[li] >> 16) & 31 == AT and li < start <= ui for lis, ui, _ in self.t._formed(f) for li in lis) and \
            not any(((w[k] >> 21) & 31 == AT or (w[k] >> 16) & 31 == AT) for k in range(start, i) if k != i)

    def _own_lui(self, f, i, word, n, own):
        """Access `word` (index i of f) of address n given a LUI of its own (`own` {index: LUI word} grows), or a Wide problem asking for one"""
        if i not in self.expand.get(f, set()):
            self.problems.append(Wide(f, i, f"{self.t.fa[f][1] + 4 * i:#x}: address {n:#x} needs a LUI of its own"))
            return word
        if not self._at_free(f, i):
            self.problems.append(f"{self.t.fa[f][1] + 4 * i:#x}: $at is in use there, so no LUI of its own")
            return word
        hi = n >> 16 if word >> 26 == 13 else (n + 0x8000) >> 16
        own[i] = (15 << 26) | (AT << 16) | (hi & 0xFFFF)
        return (word & ~(31 << 21) & 0xFFFF0000) | (AT << 21) | ((n - (hi << 16)) & 0xFFFF)

    def _assemble(self, f, out, own):
        """f's final words in their laid out order: the inserted LUIs, and branches moved past them"""
        t = self.t
        order, first = self.order_of[f]
        words = [own.get(i, 0) if lui else out[i] for i, lui in order]     # (a slot this layout needn't use stays a NOP)
        s = t.fa[f][1]
        for k, (i, lui) in enumerate(order):
            if not lui and _branch_offset(out[i]):
                target = s + 4 * (i + 1 + signed16(out[i] & 0xFFFF))
                g = t.function_at(target)
                if g not in self:
                    self.problems.append(f"{s + 4 * i:#x}: a branch into {target:#x}, a function not carried")
                    continue
                offset = (self.at(g, target) - (self.start[f] + 4 * (k + 1))) // 4
                if not -0x8000 <= offset < 0x8000:
                    self.problems.append(f"{s + 4 * i:#x}: a branch out of reach")
                    continue
                words[k] = (out[i] & 0xFFFF0000) | (offset & 0xFFFF)
        return words

    # ---- the whole
    def build(self, routines):
        t = self.t
        relocated = {f: self.relocate(f) for f in self.own}
        # the copies' bytes, with the pointers in them (to functions, to data the code names, into carried functions: a switch's jump table)
        contents, pointers = {}, []
        k = 0
        while k < len(self.order):
            a = self.order[k]
            k += 1
            data = bytearray(t.read(a, self.extent(a)))
            for j in range(0, len(data) - 3, 4):
                v = struct.unpack_from("<I", data, j)[0]
                # (a word whose low half is 0 is taken for a number: submain's 0x150000 and 0x1F0000 began functions of the executable that
                # nothing of the kinds' calls)
                if MEMORY[0] <= v < MEMORY[1] and v & 0xFFFF and (t.function_at(v) == v or v in t.label_set or t.function_at(v) in self):
                    r = self.resolve(v, f"pointer in data {a + j:#x}")
                    if r is not None:
                        pointers.append((a, j, r))
            contents[a] = data
        # their places: what the code reaches from gp below LOW's end, the rest after the code
        low_at, high_at = LOW[0], (self.code_end + 15) & ~15
        low, high = bytearray(), bytearray()
        for a in self.order:
            data = contents[a]
            if a in self.from_gp:
                self.copies[a] = low_at + len(low)
                low += data + bytes(-len(data) % 4)
            else:
                self.copies[a] = high_at + len(high)
                high += data + bytes(-len(data) % 4)
        if LOW[0] + len(low) > LOW[1]:
            self.problems.append(f"the globals reached from gp take {len(low)} bytes, more than LOW's {LOW[1] - LOW[0]}")
        for a, j, r in pointers:
            here = self.copies[a]
            block, start = (low, LOW[0]) if a in self.from_gp else (high, high_at)
            struct.pack_into("<I", block, here - start + j, self.place(r))
        code = bytearray()
        for f in self.own:
            out, pairs, gp_uses = relocated[f]
            own = {}
            for lis, targets in pairs:
                news = [(ui, self.place(r)) for ui, r in targets]
                if any(n is None for _, n in news):
                    continue
                page = lambda ui, n: n >> 16 if out[ui] >> 26 == 13 else (n + 0x8000) >> 16      # noqa: E731 (ORI's low half is unsigned)
                his = {}
                for ui, n in news:
                    his.setdefault(page(ui, n), []).append((ui, n))
                # the LUI keeps the page most of its uses are in; a use in another page gets a LUI of its own (kind 32's at 0x5D49A4 formed a
                # global File 1 has and one copied here)
                hi = max(his, key=lambda h: (len(his[h]), h))
                for li in lis:
                    out[li] = (out[li] & 0xFFFF0000) | (hi & 0xFFFF)
                for ui, n in news:
                    if page(ui, n) == hi:
                        out[ui] = (out[ui] & 0xFFFF0000) | ((n - (hi << 16)) & 0xFFFF)
                    else:
                        out[ui] = self._own_lui(f, ui, out[ui], n, own)
            for i, r in gp_uses:
                n = self.place(r)
                off = n - GP[t.dst]
                if -0x8000 <= off < 0x8000:
                    out[i] = (out[i] & 0xFFFF0000) | (off & 0xFFFF)
                elif 0 <= n < 0x8000:
                    out[i] = (out[i] & ~(31 << 21) & 0xFFFF0000) | n           # (from $zero instead of gp)
                else:
                    out[i] = self._own_lui(f, i, out[i], n, own)
            words = self._assemble(f, out, own)
            code += struct.pack(f"<{len(words)}I", *words)
        blob = bytes(code) + bytes(high_at - self.code_end) + bytes(high)
        here = {kind: self.start.get(r, r) for kind, r in routines.items()}
        return Built(self.base, blob, self.code_end, (LOW[0], bytes(low)),
                     [(self.start[f], self.start[f] + 4 * len(self.order_of[f][0])) for f in self.own], here, self.problems)


def write(built, kinds, out):
    """The build's files in folder `out`: transplant.bin and transplant.json (the recompiler's generated overlay: config "generated"), and
    transplant_blob.c (the block as the runtime loads it into memory, and each kind's routine there)"""
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    import json

    (out / "transplant.bin").write_bytes(built.blob)
    meta = {"text": [built.base, built.code_end], "extra": [s for s, _ in built.functions],
            "routines": {str(k): built.routines[k] for k in kinds}}
    (out / "transplant.json").write_text(json.dumps(meta, indent=1) + "\n")
    rows = [", ".join(f"0x{b:02x}" for b in built.blob[i:i + 16]) for i in range(0, len(built.blob), 16)]
    c = ["// Generated by tools/bh_transplant.py: do not edit", "#include <stdint.h>", "",
         f"const uint32_t transplant_base = 0x{built.base:08x}u;", f"const uint32_t transplant_size = {len(built.blob)}u;",
         "const uint8_t transplant_blob[] = {"] + [f"    {r}," for r in rows] + ["};", "",
         f"const uint32_t transplant_count = {len(kinds)}u;", "const uint32_t transplant_routines[][2] = {"]
    c += [f"    {{{k}u, 0x{built.routines[k]:08x}u}}," for k in kinds] + ["};", ""]
    (out / "transplant_blob.c").write_text("\n".join(c))


def main(argv):
    if len(argv) == 3 and argv[0] == "match":
        a, b = Code(argv[1]), Code(argv[2])
        pairs, fa, fb = match(a, b)
        for segment in a.functions:
            total = len(a.functions[segment])
            got = sum(1 for s, _, _ in a.functions[segment] if s in pairs)
            print(f"{segment}: {got} of {total} functions matched")
        print(f"data addresses mapped: {len(data_map(fa, fb, pairs))}")
        return 0
    if len(argv) >= 5 and argv[0] == "build":
        src, dst, out, kinds = argv[1], argv[2], argv[3], [int(k) for k in argv[4:]]
        t = Transplant(src, dst)
        built = build(t, kinds, BASE[dst])
        if built.problems:
            for p in built.problems:
                print(p, file=sys.stderr)
            return 1
        write(built, kinds, out)
        print(f"{src} kinds {kinds} for {dst}: {len(built.functions)} functions, {len(built.blob)} bytes at {built.base:#x}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
