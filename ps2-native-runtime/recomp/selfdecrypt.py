"""Overlays that decrypt parts of themselves as they run (Sony's DNAS libraries): the plain code worked out ahead of time by
running the overlay's own decryption routines on its bytes, so the recompiler sees the code the console runs.

Such an overlay keeps each encrypted block of code behind a call to one of its decryption routines: the call's arguments
(the block's address and its keys) are loaded right before it from words kept beside the block, and the routine decrypts the
block in place (its argument a3's bit 0 clear) or encrypts it again (bit 0 set, a0 then the block's end). Nothing comes from
outside the overlay, so the plain code is the same on every run.

`decrypt(data, base, routines)` finds every call to the routines, works out its arguments by running the few instructions
before it, runs each decrypting call through a small R5900 interpreter (integer instructions only: anything else stops
with its address), and repeats over the code each round uncovers until nothing new is found. It returns the plain bytes and
the blocks it decrypted. Calls the routines make outside the overlay (FlushCache) are skipped: they don't change memory.
"""

from r5900 import decode

MASK64 = (1 << 64) - 1
MASK32 = 0xFFFFFFFF
# How far before a call its arguments are looked for, and how many instructions one decryption may take
ARG_WINDOW = 24
MAX_STEPS = 50_000_000
# The return address calls are run with: the interpreter stops when it's jumped to
RETURN_SENTINEL = 0xFFFFFFF0


class DecryptError(Exception):
    pass


def sext32(v):
    v &= MASK32
    return v - (1 << 32) if v & 0x80000000 else v


def to64(v):
    return v & MASK64


class Interp:
    """The integer part of the R5900, over one overlay's bytes. Registers hold 64 bits (the 128-bit LQ/SQ aren't done)."""

    def __init__(self, mem, base):
        self.mem = mem
        self.base = base
        self.r = [0] * 32
        self.hi = self.lo = 0
        self.skipped_calls = set()

    def inside(self, address, size=4):
        return self.base <= address and address + size <= self.base + len(self.mem)

    def load(self, address, size, signed):
        address &= MASK32
        if not self.inside(address, size):
            raise DecryptError(f"load of {size} bytes at {address:#x}, outside the overlay")
        v = int.from_bytes(self.mem[address - self.base:address - self.base + size], "little")
        if signed and v >> (size * 8 - 1):
            v -= 1 << (size * 8)
        return to64(v)

    def store(self, address, size, value):
        address &= MASK32
        if not self.inside(address, size):
            raise DecryptError(f"store of {size} bytes at {address:#x}, outside the overlay")
        off = address - self.base
        self.mem[off:off + size] = (value & ((1 << (size * 8)) - 1)).to_bytes(size, "little")

    def set(self, n, v):
        if n:
            self.r[n] = to64(v)

    def call(self, address, args):
        """Runs the function at `address` with a0-a3 = args until it returns"""
        for k, v in enumerate(args):
            self.r[4 + k] = to64(v)
        self.r[31] = RETURN_SENTINEL
        self.r[29] = self.base + len(self.mem) - 16   # a stack: the room past the overlay's end is added by the caller
        pc = address
        for _ in range(MAX_STEPS):
            if pc == RETURN_SENTINEL:
                return
            pc = self.step(pc)
        raise DecryptError(f"function {address:#x} ran {MAX_STEPS} instructions without returning")

    def fetch(self, pc):
        if not self.inside(pc):
            raise DecryptError(f"ran to {pc:#x}, outside the overlay")
        return decode(int.from_bytes(self.mem[pc - self.base:pc - self.base + 4], "little"), pc)

    def step(self, pc):
        """One instruction (with its delay slot if it's a branch or jump): the next pc"""
        i = self.fetch(pc)
        if i.kind in ("branch", "branch_likely"):
            taken = self.condition(i)
            if i.link:
                raise DecryptError(f"{i.addr:#x}: {i.name} isn't done")
            if taken:
                self.execute(self.fetch(pc + 4))
                return i.target
            if i.kind == "branch":
                self.execute(self.fetch(pc + 4))
            return pc + 8
        if i.kind in ("jump", "call", "jr", "jalr"):
            target = i.target if i.kind in ("jump", "call") else self.r[i.rs] & MASK32
            self.execute(self.fetch(pc + 4))
            if i.kind == "call" or i.kind == "jalr":
                if not self.inside(target):
                    # A call out of the overlay (FlushCache): nothing of the overlay's memory changes
                    self.skipped_calls.add(target)
                    self.set(2, 0)
                    return pc + 8
                self.set(31 if i.kind == "call" else i.rd, pc + 8)
            return target
        self.execute(i)
        return pc + 4

    def condition(self, i):
        s = sext64(self.r[i.rs])
        t = sext64(self.r[i.rt])
        name = i.name[:-1] if i.kind == "branch_likely" else i.name
        return {"beq": s == t, "bne": s != t, "blez": s <= 0, "bgtz": s > 0, "bltz": s < 0, "bgez": s >= 0}[name]

    def execute(self, i):
        n, r = i.name, self.r
        s, t = r[i.rs], r[i.rt]
        if n in ("nop", "sync", "pref", "cache") or i.word == 0:
            return
        if n == "sll":
            self.set(i.rd, sext32(t << i.sa))
        elif n == "srl":
            self.set(i.rd, sext32((t & MASK32) >> i.sa))
        elif n == "sra":
            self.set(i.rd, sext32(t) >> i.sa)
        elif n == "sllv":
            self.set(i.rd, sext32(t << (s & 31)))
        elif n == "srlv":
            self.set(i.rd, sext32((t & MASK32) >> (s & 31)))
        elif n == "srav":
            self.set(i.rd, sext32(t) >> (s & 31))
        elif n == "dsll":
            self.set(i.rd, t << i.sa)
        elif n == "dsrl":
            self.set(i.rd, t >> i.sa)
        elif n == "dsra":
            self.set(i.rd, sext64(t) >> i.sa)
        elif n == "dsll32":
            self.set(i.rd, t << (i.sa + 32))
        elif n == "dsrl32":
            self.set(i.rd, t >> (i.sa + 32))
        elif n == "dsra32":
            self.set(i.rd, sext64(t) >> (i.sa + 32))
        elif n == "dsllv":
            self.set(i.rd, t << (s & 63))
        elif n == "dsrlv":
            self.set(i.rd, t >> (s & 63))
        elif n == "dsrav":
            self.set(i.rd, sext64(t) >> (s & 63))
        elif n in ("addu", "add"):
            self.set(i.rd, sext32(s + t))
        elif n in ("subu", "sub"):
            self.set(i.rd, sext32(s - t))
        elif n in ("daddu", "dadd"):
            self.set(i.rd, s + t)
        elif n in ("dsubu", "dsub"):
            self.set(i.rd, s - t)
        elif n == "and":
            self.set(i.rd, s & t)
        elif n == "or":
            self.set(i.rd, s | t)
        elif n == "xor":
            self.set(i.rd, s ^ t)
        elif n == "nor":
            self.set(i.rd, ~(s | t))
        elif n == "slt":
            self.set(i.rd, int(sext64(s) < sext64(t)))
        elif n == "sltu":
            self.set(i.rd, int(s < t))
        elif n == "movz":
            if t == 0:
                self.set(i.rd, s)
        elif n == "movn":
            if t != 0:
                self.set(i.rd, s)
        elif n in ("mult", "multu"):
            a, b = (sext32(s), sext32(t)) if n == "mult" else (s & MASK32, t & MASK32)
            p = a * b
            self.lo, self.hi = to64(sext32(p)), to64(sext32(p >> 32))
            self.set(i.rd, self.lo)
        elif n in ("div", "divu"):
            a, b = (sext32(s), sext32(t)) if n == "div" else (s & MASK32, t & MASK32)
            if b == 0:
                raise DecryptError(f"{i.addr:#x}: division by zero")
            q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
            self.lo, self.hi = to64(sext32(q)), to64(sext32(a - q * b))
        elif n == "mfhi":
            self.set(i.rd, self.hi)
        elif n == "mflo":
            self.set(i.rd, self.lo)
        elif n == "mthi":
            self.hi = s
        elif n == "mtlo":
            self.lo = s
        elif n in ("addiu", "addi"):
            self.set(i.rt, sext32(s + i.imm))
        elif n in ("daddiu", "daddi"):
            self.set(i.rt, s + i.imm)
        elif n == "slti":
            self.set(i.rt, int(sext64(s) < i.imm))
        elif n == "sltiu":
            self.set(i.rt, int(s < to64(i.imm)))
        elif n == "andi":
            self.set(i.rt, s & i.uimm)
        elif n == "ori":
            self.set(i.rt, s | i.uimm)
        elif n == "xori":
            self.set(i.rt, s ^ i.uimm)
        elif n == "lui":
            self.set(i.rt, sext32(i.uimm << 16))
        elif n in LOADS:
            size, signed = LOADS[n]
            self.set(i.rt, self.load(s + i.imm, size, signed))
        elif n in STORES:
            self.store(s + i.imm, STORES[n], t)
        else:
            raise DecryptError(f"{i.addr:#x}: {n} ({i.word:08x}) isn't done by the decryption's interpreter")


LOADS = {"lb": (1, True), "lbu": (1, False), "lh": (2, True), "lhu": (2, False), "lw": (4, True), "lwu": (4, False),
         "ld": (8, False)}
STORES = {"sb": 1, "sh": 2, "sw": 4, "sd": 8}


def sext64(v):
    v &= MASK64
    return v - (1 << 64) if v >> 63 else v


def call_arguments(mem, base, site):
    """a0-a3 at the call at `site`: the instructions before it (from the last jump, branch or ARG_WINDOW back) and its delay
    slot run with every register unknown, loads read from the overlay. None for an argument they don't set."""
    first = site
    for k in range(1, ARG_WINDOW + 1):
        a = site - 4 * k
        if a < base:
            break
        prev = decode(int.from_bytes(mem[a - base:a - base + 4], "little"), a)
        if prev.kind in ("jump", "call", "jr", "jalr", "branch_likely", "syscall", "eret"):
            break
        first = a
    known = [False] * 32
    known[0] = True
    interp = Interp(mem, base)
    for a in list(range(first, site, 4)) + [site + 4]:
        i = decode(int.from_bytes(mem[a - base:a - base + 4], "little"), a)
        if i.kind == "branch":
            continue  # a branch before the call (over it, when the block is already decrypted): its delay slot runs on
        writes = written_register(i)
        reads = [] if i.name == "lui" else [i.rs] if i.name in LOADS or i.name in IMMEDIATE else [i.rs, i.rt]
        try:
            ok = all(known[x] for x in reads) and writes is not None
            if ok:
                interp.execute(i)
        except DecryptError:
            ok = False
        if writes is not None:
            known[writes] = ok
    return [interp.r[4 + k] & MASK32 if known[4 + k] else None for k in range(4)]


IMMEDIATE = {"addiu", "addi", "daddiu", "daddi", "slti", "sltiu", "andi", "ori", "xori"}


def written_register(i):
    if i.name in LOADS or i.name in IMMEDIATE or i.name == "lui":
        return i.rt
    if i.name in ("sll", "srl", "sra", "addu", "subu", "daddu", "dsubu", "and", "or", "xor", "nor", "slt", "sltu",
                  "sllv", "srlv", "srav", "dsll", "dsrl", "dsra", "dsll32", "dsrl32", "dsra32"):
        return i.rd
    return None


def decrypt(data, base, routines, text):
    """The overlay's plain bytes and the (start, end) blocks its routines decrypt, the blocks in the text (start, end)"""
    stack_room = 0x10000
    mem = bytearray(data) + bytearray(stack_room)
    calls = {0x0C000000 | ((r >> 2) & 0x03FFFFFF): r for r in routines}
    done = set()
    blocks = []
    while True:
        found = []
        for a in range(text[0], text[1], 4):
            w = int.from_bytes(mem[a - base:a - base + 4], "little")
            if w in calls and a not in done:
                found.append((a, calls[w]))
        if not found:
            break
        for site, routine in found:
            done.add(site)
            args = call_arguments(mem, base, site)
            if None in args:
                raise DecryptError(f"call at {site:#x} to {routine:#x}: arguments a0-a3 not worked out ({args})")
            a0, a1, a2, a3 = args
            if a3 & 1:
                continue  # an encryption again after the block ran: the plain code is what's wanted
            size = (a1 ^ a2) & MASK32
            if not (text[0] <= a0 and a0 + size <= text[1]):
                raise DecryptError(f"call at {site:#x}: block {a0:#x}+{size:#x} isn't inside the text")
            if any(s == a0 for s, _ in blocks):
                continue  # decrypted already (another call for the same block)
            interp = Interp(mem, base)
            interp.call(routine, args)
            blocks.append((a0, a0 + size))
    return bytes(mem[:len(data)]), sorted(blocks)
