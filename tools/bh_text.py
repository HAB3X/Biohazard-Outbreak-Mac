#!/usr/bin/env python3
"""The Biohazard Outbreak games' text: the string encodings, and the two containers the strings live in (docs/translation.md
describes the formats and their evidence).

Strings are Shift-JIS (cp932) with ASCII control tags (<p1>, <c2>, <I>, <P>, %t...) and \\n in the byte stream. A translation
unit is a string; its id is the first 10 hex digits of the SHA-1 of its original bytes (so the same line in many files is one
unit, and the id never needs the Japanese to be kept).

  ExePool(elf, region, base, images)   the executable's string pool: strings reached through tables of pointers
  MsgBin(data)                          a stage's message file (rNNN.bin, dmNNN.bin, spmNNNNNN.bin)
  HtmlPage(data)                        a page of the network screens' browser
All give `texts()` ({unit id: original bytes}) and `rebuilt(new)` (the new bytes, given {unit id: English bytes}); what
doesn't fit is a DoesNotFit error that says where.
"""
import hashlib
import re
import struct

LINE_MARKERS = (b"\x81\xa5", b"\x81\x40")  # ▼ and the ideographic space, as Shift-JIS
# Control tags and format codes: the stage text's <p1> <c2> <I>, the network screens' markup (<BODY><SIZE=3>, HTML) and
# printf codes (%s %2d, the network's %1 %v); <words in brackets> without an attribute's = are text. A line break in markup (<br>) is layout: the English sets its own.
TAG = re.compile(rb"</?[A-Za-z][0-9A-Za-z-]*(?:=[^<>\s\x80-\xff]*|\s[^<>\x80-\xff]*=[^<>\x80-\xff]*)?>"
                 rb"|%[-0-9.]*[A-Za-z]|%[0-9]")
LAYOUT_TAG = re.compile(rb"<br>", re.I)


def unit_id(raw):
    return hashlib.sha1(raw).hexdigest()[:10]


def has_japanese(raw):
    return any(b >= 0x80 for b in raw)


def tags(raw):
    return sorted(t for t in TAG.findall(raw) if not LAYOUT_TAG.fullmatch(t))


class DoesNotFit(Exception):
    pass


def looks_like_text(raw, at):
    """Whether bytes between NULs in a data section are a string (not a pointer or number that happens to decode): Shift-JIS
    with a full-width character, no control characters but newlines, no half-width katakana (which the games' text doesn't
    use and the address words that decode do: an address's high bytes read as ASCII after one), and not a word-aligned run of
    three bytes or fewer (an address with its zero top byte)."""
    if at % 4 == 0 and len(raw) <= 3:
        return False
    try:
        chars = raw.decode("cp932")
    except UnicodeDecodeError:
        return False
    if any((ord(c) < 0x20 and c not in "\r\n") or 0xFF61 <= ord(c) <= 0xFF9F for c in chars):
        return False
    return any(ord(c) > 0xFF for c in chars)


def cells(raw):
    """Columns a line takes in the game's fixed-pitch font: a full-width character is two, a half-width one, one; the control
    tags none."""
    return sum(2 if ord(c) > 0xFF else 1 for c in TAG.sub(b"", raw).decode("cp932"))


# The PS2 buttons the English may name as the font's own symbols (Shift-JIS ○ × △ □), which show the keyboard's keys when the
# keyboard is used (bh_font.py): written {circle} {cross} {triangle} {square} in the translations. Those are the menus' keys
# (✕ is Esc, back); a line about play names the button as play has it, {play:cross} (✕ is Left Shift, run): four symbols the
# games' text never uses (∈ ∋ ⊆ ⊇, checked over every unit of both discs) drawn as play's keys, or as the PS2's symbols
BUTTON_GLYPHS = {"{circle}": b"\x81\x9b", "{cross}": b"\x81\x7e", "{triangle}": b"\x81\xa2", "{square}": b"\x81\xa0",
                 "{play:circle}": b"\x81\xb8", "{play:cross}": b"\x81\xb9", "{play:triangle}": b"\x81\xba",
                 "{play:square}": b"\x81\xbb"}


def english_bytes(text, original):
    """The bytes of an English line: plain ASCII (and the button symbols of BUTTON_GLYPHS), with the control tags and the end
    marker of the Japanese it replaces. A Japanese line that ends in spaces (a field's width: the choices' place, a column)
    keeps its width: the English is padded to it."""
    parts = re.split(r"(\{(?:play:)?(?:circle|cross|triangle|square)\})", text.replace("\\n", "\n"))
    raw = b"".join(BUTTON_GLYPHS[p] if p in BUTTON_GLYPHS else p.encode("ascii") for p in parts)
    if tags(raw) != tags(original):
        raise ValueError(f"control tags {tags(raw)} differ from the original's {tags(original)}")
    for marker in LINE_MARKERS:  # ▼ (wait for a button) or a full-width space ends a line: kept, so the English leaves it out
        if original.endswith(marker):
            raw += marker
    if original.endswith(b" ") and not raw.endswith(b" "):
        raw += b" " * max(0, cells(original) - cells(raw))
    return raw


class ExePool:
    """The executable's pool of strings. The game reaches every string there through a pointer in a table (890 of the 903
    strings in File 2's pool have pointers in the data, no code builds their addresses), so the strings can be packed again
    anywhere in the pool's free room with every pointer to them rewritten.

    An overlay's pool (File 1's PROG/*.BIN) is the same, with the overlay as `elf` and `own` its name; there many strings are
    reached by code (an address built in registers, no pointer in the data). With `in_place`, those are translated too, each
    in its own slot (they can't move): the English must fit where the Japanese was.

    elf: the image's bytes; region: (first, last+1) file offsets of the pool; base: address - file offset; images:
    {name: (bytes, address of its first byte, [(first, last+1) data ranges as addresses])}, where pointers are looked for;
    own: the name the changes to `elf` are given under."""

    def __init__(self, elf, region, base, images, own="elf", in_place=False):
        self.elf, self.region, self.base, self.own, self.in_place = elf, region, base, own, in_place
        self.starts = []
        pos = region[0]
        while pos < region[1]:
            if elf[pos] == 0:
                pos += 1
                continue
            self.starts.append(pos)
            pos = elf.index(b"\0", pos) + 1
        self.raw = [elf[s:elf.index(b"\0", s)] for s in self.starts]
        at = {s + base: i for i, s in enumerate(self.starts)}
        self.refs = [[] for _ in self.starts]  # (image name, offset of the u32) of every pointer to each string
        # Empty strings: NULs of the region that pointers point at. The compiler keeps "" in another string's padding or
        # end (File 1's NETWK.BIN: the blank line of 70 message tables is 0x5F5348, in the padding after the string at
        # 0xB5320), so a slot ends there and the NUL stays: packed into, it showed the end of another line ('id "Your')
        self.empties = set()
        for name, (data, first_byte, ranges) in images.items():
            for lo, hi in ranges:
                for addr in range(lo, hi, 4):
                    value = struct.unpack_from("<I", data, addr - first_byte)[0]
                    i = at.get(value)
                    if i is not None:
                        self.refs[i].append((name, addr - first_byte))
                    elif region[0] <= value - base < region[1] and elf[value - base] == 0:
                        self.empties.add(value - base)

    def slot_end(self, i):
        """Where string i's slot ends: the next string's start (the NULs between are its padding), or just after the first
        empty string in its padding (that NUL is kept: a string written into the slot ends at it at the latest)."""
        end = self.starts[i + 1] if i + 1 < len(self.starts) else self.region[1]
        return min([e + 1 for e in self.empties if self.starts[i] < e < end] + [end])

    def translatable(self):
        return [i for i, raw in enumerate(self.raw) if has_japanese(raw) and (self.refs[i] or self.fixed(i))]

    def fixed(self, i):
        """String i is reached by code only and is text (see looks_like_text): translated in its own slot."""
        return self.in_place and not self.refs[i] and looks_like_text(self.raw[i], self.starts[i])

    def texts(self):
        return {unit_id(self.raw[i]): self.raw[i] for i in self.translatable()}

    def rebuilt(self, new):
        """{image name: [(offset, bytes)]}, the pool's own image's under `own`: the changes for the strings in `new`. Strings
        in their own slots (fixed) are written there. The others' old slots become free runs, and the new strings are packed into them longest first, each into the run with most room;
        every pointer to a moved string is rewritten."""
        changed = [i for i in self.translatable() if unit_id(self.raw[i]) in new]
        out = {self.own: []}
        for i in [i for i in changed if self.fixed(i)]:
            raw, room = new[unit_id(self.raw[i])] + b"\0", self.slot_end(i) - self.starts[i]
            if len(raw) > room:
                raise DoesNotFit(f"{self.own} string at {self.starts[i]:#x} ({raw[:30]!r}) stays in its place: {len(raw)} "
                                 f"bytes with its NUL, the slot has {room}")
            out[self.own].append((self.starts[i], raw + bytes(room - len(raw))))
        changed = [i for i in changed if not self.fixed(i)]
        runs = []
        for i in changed:
            lo, hi = self.starts[i], self.slot_end(i)
            if runs and runs[-1][1] == lo:
                runs[-1][1] = hi
            else:
                runs.append([lo, hi])
        free = [list(r) for r in runs]
        out[self.own] += [(lo, bytes(hi - lo)) for lo, hi in runs]
        for i in sorted(changed, key=lambda i: -len(new[unit_id(self.raw[i])])):
            raw = new[unit_id(self.raw[i])] + b"\0"
            room = max(free, key=lambda r: r[1] - r[0])
            if room[1] - room[0] < len(raw):
                raise DoesNotFit(f"pool string at {self.starts[i]:#x}: {len(raw)} bytes with its NUL, "
                                 f"the largest free run has {room[1] - room[0]}")
            out[self.own].append((room[0], raw))
            for name, off in self.refs[i]:
                out.setdefault(name, []).append((off, struct.pack("<I", room[0] + self.base)))
            room[0] += len(raw)
        return out


class MsgBin:
    """A stage's message file: u32 end of the strings, u32 start of the pointer arrays; from offset 8 the records (u32 line
    count, u32 offset of that record's line pointers; one record is one message, the game asks for it by its number); the
    line pointers, then the strings: the text, CR LF, a NUL, padded with NULs to an even length; then 0xFF to the file's
    end. The last record is a stop: its slot reads as an empty line's bytes (CR LF NUL NUL) right before the first string."""

    def __init__(self, data):
        self.data = data
        self.end, arrays = struct.unpack_from("<II", data, 0)
        records = [struct.unpack_from("<II", data, 8 + 8 * i) for i in range((arrays - 8) // 8)]
        self.strings, self.slots = [], []
        if not records:  # some stages' speech files are empty (a header and nothing else)
            return
        # The line pointers are one list, the records windows on it (a pointer may be in no window: a message's next line is
        # the next pointer). The list ends where the first string begins, and no pointer points into the list
        pos, lowest = min(off for _, off in records), len(data)
        while pos < lowest:
            value = struct.unpack_from("<I", data, pos)[0]
            if pos < value < self.end and value % 2 == 0:  # strings start at even offsets: the stop slot's CR LF is odd
                self.slots.append(pos)
                lowest = min(lowest, value)
            pos += 4
        self.first_string = lowest
        for start in sorted({self._ptr(s) for s in self.slots}):
            self.strings.append((start, data[start:data.index(b"\r\n\0", start)]))

    def _ptr(self, at):
        return struct.unpack_from("<I", self.data, at)[0]

    def texts(self):
        return {unit_id(raw): raw for _, raw in self.strings if has_japanese(raw)}

    def rebuilt(self, new):
        if not self.strings:
            return self.data
        body = bytearray()
        address = {}
        for start, raw in self.strings:
            address[start] = self.first_string + len(body)
            body += new.get(unit_id(raw), raw) + b"\r\n\0"
            body += b"\0" * (len(body) % 2)
        end = self.first_string + len(body)
        if end > len(self.data):
            raise DoesNotFit(f"the strings end at {end:#x}, the file is {len(self.data):#x} bytes")
        out = bytearray(self.data)
        for s in self.slots:
            struct.pack_into("<I", out, s, address[self._ptr(s)])
        out[self.first_string:end] = body
        out[end:self.end] = b"\xff" * (self.end - end)
        struct.pack_into("<I", out, 0, end)
        return bytes(out)


class HtmlPage:
    """A page of the network screens' browser (File 2's rfff.afs *.HTM): Shift-JIS HTML. A unit is the text between two
    tags, without its leading and trailing white space; a line break (<br>) belongs to the text, so the English breaks its
    own lines. Tags and the browser's own comments (ASCII keywords it reads: <GAME-STYLE>, INPUT-SFIELD, CHG-IMG-BUTTON) stay
    as they are; a comment with Japanese in it is a note of the page's authors, dropped in the English page to give the text
    room. The page keeps its size: the archive's table and the sectors after it are left alone, so a shorter page is made up
    with spaces at the end, and a longer one does not fit."""

    SPLIT = re.compile(rb"<!--.*?-->|<(?!br>)[^<>]*>", re.S | re.I)

    def __init__(self, data):
        self.entry = data
        self.units = []  # (first, last+1) offsets of the units' bytes
        pos = 0
        for m in [*self.SPLIT.finditer(data), None]:
            lo, hi = pos, (m.start() if m else len(data))
            seg = data[lo:hi]
            lo, hi = lo + len(seg) - len(seg.lstrip()), hi - len(seg) + len(seg.rstrip())
            if lo < hi and has_japanese(data[lo:hi]):
                self.units.append((lo, hi))
            pos = m.end() if m else len(data)

    def texts(self):
        return {unit_id(self.entry[lo:hi]): self.entry[lo:hi] for lo, hi in self.units}

    def rebuilt(self, new):
        out, pos = bytearray(), 0
        for lo, hi in self.units:
            out += self.entry[pos:lo] + new.get(unit_id(self.entry[lo:hi]), self.entry[lo:hi])
            pos = hi
        out += self.entry[pos:]
        out = re.sub(rb"<!--(?:(?!-->).)*[\x80-\xff](?:(?!-->).)*-->", b"", bytes(out), flags=re.S)
        if len(out) > len(self.entry):
            raise DoesNotFit(f"the page needs {len(out)} bytes, it has {len(self.entry)}")
        return bytes(out + b" " * (len(self.entry) - len(out)))
