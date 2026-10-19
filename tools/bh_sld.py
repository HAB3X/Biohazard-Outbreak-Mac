#!/usr/bin/env python3
"""The games' LZ compression of pictures (the .tex and .sld files, and the textures in models): a TIM2 file compressed as a
stream of 16-bit little-endian words.

    bh_sld.py unpack FILE OUT     the TIM2 file inside
    bh_sld.py check FILE          unpacks and packs again: the sizes, and that the round trip is exact

The stream: a control word, whose 16 bits (most significant first) say what each of the next 16 items is: 0 a literal word,
1 a token word T: count = T >> 11, offset = T & 0x7FF; a count of 0 means the count is the next word. Offset 0 is a run of
`count` zero words; otherwise `count` words are copied from `offset` words back (the copy may overlap what it writes, which
repeats a pattern). A token of 0 with a following count of 0 ends the stream. (docs/research/renderer.md has the sources:
reo's decompress_ob_lz77 and REOF2's port of the game's 0x1AEE20 describe the same codec; checked here by unpacking every
picture of both discs into TIM2 files whose sizes add up.)

As a library: unpack(data) -> bytes; pack(data) -> bytes (any even-sized data; longest matches, lazily);
pack_smallest(data) -> bytes (the cheapest parse of the matches: slower).
"""
import struct
import sys
from array import array

WINDOW = 0x7FF  # the farthest a copy reaches back, in words
SHORT = 31  # the longest count that fits in a token word


def unpack(data):
    """The words the stream at the start of `data` makes, as bytes. Bytes after the end token are ignored."""
    src = array("H", data[:len(data) & ~1])
    if sys.byteorder != "little":
        src.byteswap()
    out = array("H")
    pos = 0
    while True:
        if pos >= len(src):
            raise ValueError("the stream runs out before its end token")
        control = src[pos]
        pos += 1
        for bit in range(15, -1, -1):
            if not control >> bit & 1:
                out.append(src[pos])
                pos += 1
                continue
            token = src[pos]
            pos += 1
            count, offset = token >> 11, token & WINDOW
            if count == 0:
                count = src[pos]
                pos += 1
                if count == 0 and offset == 0:
                    out_bytes = out.tobytes() if sys.byteorder == "little" else _swapped(out)
                    return out_bytes
            if offset == 0:
                out.extend([0] * count)
            else:
                if offset > len(out):
                    raise ValueError(f"a copy reaches {offset} words back, only {len(out)} are written")
                start = len(out) - offset
                for i in range(count):
                    out.append(out[start + i])


def _swapped(words):
    copy = array("H", words)
    copy.byteswap()
    return copy.tobytes()


def pack(data):
    """A stream that unpack() turns back into `data` (even-sized): at each word the longest copy within the window (found
    through the positions of each pair of words) or zero run, else a literal; a copy is put off by a word when the next word
    starts a longer one (lazy matching: it saves a few percent, which edited pictures need to fit where they were)."""
    if len(data) % 2:
        raise ValueError("the data must be whole words")
    words = array("H", data)
    if sys.byteorder != "little":
        words.byteswap()
    n = len(words)
    chains = {}  # pair of words -> positions where it starts, most recent last
    indexed = 0  # positions below this are in the chains

    def index_to(limit):
        nonlocal indexed
        for p in range(indexed, min(limit, n - 1)):
            chain = chains.setdefault((words[p], words[p + 1]), [])
            chain.append(p)
            if len(chain) > 128:  # only the nearest positions matter (the window is 2047 words)
                del chain[:64]
        indexed = max(indexed, min(limit, n - 1))

    def best(pos):
        """(count, offset) of the longest copy or zero run at pos (offset 0: zeros)."""
        index_to(pos)
        count, offset = 0, 0
        limit = min(n - pos, 0xFFFF)
        if pos + 1 < n:
            for start in reversed(chains.get((words[pos], words[pos + 1]), ())):
                if pos - start > WINDOW:
                    break
                length = 0
                while length < limit and words[start + length] == words[pos + length]:
                    length += 1
                if length > count:
                    count, offset = length, pos - start
                    if length == limit:
                        break
        zeros = 0
        while zeros < limit and words[pos + zeros] == 0:
            zeros += 1
        if zeros >= 2 and zeros >= count:
            count, offset = zeros, 0
        return count, offset

    items = []  # (is token, words of the item)
    pos = 0
    pending = None  # the copy found at pos, when it was looked up as the next one
    while pos < n:
        count, offset = pending or best(pos)
        pending = None
        if count >= 2 and pos + 1 < n:
            following = best(pos + 1)
            if following[0] > count + 1:
                items.append((0, [words[pos]]))
                pos += 1
                pending = following
                continue
        if count >= 2:
            items.append((1, [count << 11 | offset] if count <= SHORT else [offset, count]))
            pos += count
        else:
            items.append((0, [words[pos]]))
            pos += 1
    items.append((1, [0, 0]))
    return _stream(items)


def pack_smallest(data):
    """A stream like pack()'s, as small as the codec allows for the matches found: the longest copy at every word, then the
    cheapest way through them (a literal costs a word and a control bit, a copy of up to 31 words the same, a longer one a word
    more), worked out from the end. Several times slower than pack(); for a picture that doesn't fit otherwise."""
    if len(data) % 2:
        raise ValueError("the data must be whole words")
    words = array("H", data)
    if sys.byteorder != "little":
        words.byteswap()
    n = len(words)
    chains = {}
    longest = [(0, 0)] * n
    zeros = 0
    for pos in range(n - 1, -1, -1):  # zero runs, from the end
        zeros = zeros + 1 if words[pos] == 0 else 0
        if zeros >= 2:
            longest[pos] = (min(zeros, 0xFFFF), 0)
    previous = (0, 0)
    for pos in range(n):
        limit = min(n - pos, 0xFFFF)
        count, offset = longest[pos]
        if previous[0] - 1 > count and previous[1]:  # the copy found a word back goes on here
            count, offset = previous[0] - 1, previous[1]
        if pos + 1 < n:
            key = (words[pos], words[pos + 1])
            for start in (() if count > SHORT else reversed(chains.get(key, ()))):  # a long copy needs no better one
                if pos - start > WINDOW:
                    break
                length = 0
                while length < limit and words[start + length] == words[pos + length]:
                    length += 1
                if length > count:
                    count, offset = length, pos - start
                    if length == limit:
                        break
            chain = chains.setdefault(key, [])
            chain.append(pos)
            if len(chain) > 1024:  # deeper than pack(): measured on cockpit.tex, 128 deep 58388 bytes, 512 58186, 2048 58104
                del chain[:512]
        longest[pos] = previous = (count, offset)
    cost = [0] * (n + 1)  # bits from here to the end
    choice = [1] * n
    for pos in range(n - 1, -1, -1):
        best, pick = 17 + cost[pos + 1], 1
        count = longest[pos][0]
        for length in range(2, min(count, SHORT) + 1):
            c = 17 + cost[pos + length]
            if c < best:
                best, pick = c, length
        if count > SHORT:
            c = 33 + cost[pos + count]
            if c < best:
                best, pick = c, count
        cost[pos], choice[pos] = best, pick
    items = []
    pos = 0
    while pos < n:
        length = choice[pos]
        if length == 1:
            items.append((0, [words[pos]]))
        else:
            offset = longest[pos][1]
            items.append((1, [length << 11 | offset] if length <= SHORT else [offset, length]))
        pos += length
    items.append((1, [0, 0]))
    return _stream(items)


def _stream(items):
    """The words of a stream of items (is token, its words): a control word before each sixteen."""
    out = array("H")
    for i in range(0, len(items), 16):
        group = items[i:i + 16]
        control = 0
        for j, (token, _) in enumerate(group):
            control |= token << (15 - j)
        out.append(control)
        for _, item_words in group:
            out.extend(item_words)
    if sys.byteorder != "little":
        out.byteswap()
    return out.tobytes()


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    data = open(argv[2], "rb").read()
    raw = unpack(data)
    if argv[1] == "unpack":
        open(argv[3], "wb").write(raw)
    elif argv[1] == "check":
        again = pack(raw)
        smallest = pack_smallest(raw)
        print(f"{argv[2]}: {len(data)} bytes packed, {len(raw)} unpacked, packed again {len(again)} (smallest "
              f"{len(smallest)}), round trip {'exact' if unpack(again) == raw and unpack(smallest) == raw else 'WRONG'}")
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
