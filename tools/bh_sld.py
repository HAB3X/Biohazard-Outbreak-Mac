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

As a library: unpack(data) -> bytes; pack(data) -> bytes (any even-sized data; greedy longest matches).
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
    through the positions of each pair of words) or zero run, else a literal."""
    if len(data) % 2:
        raise ValueError("the data must be whole words")
    words = array("H", data)
    if sys.byteorder != "little":
        words.byteswap()
    n = len(words)
    items = []  # (is token, words of the item)
    chains = {}  # pair of words -> positions where it starts, most recent last
    pos = 0
    while pos < n:
        best_count, best_offset = 0, 0
        zeros = 0
        while pos + zeros < n and words[pos + zeros] == 0 and zeros < 0xFFFF:
            zeros += 1
        if pos + 1 < n:
            for start in reversed(chains.get((words[pos], words[pos + 1]), ())):
                offset = pos - start
                if offset > WINDOW:
                    break
                length = 0
                limit = min(n - pos, 0xFFFF)
                while length < limit and words[start + length] == words[pos + length]:
                    length += 1
                if length > best_count:
                    best_count, best_offset = length, offset
                    if length == limit:
                        break
        if zeros >= 2 and zeros >= best_count:
            best_count, best_offset = zeros, 0
        if best_count >= 2:
            if best_count <= SHORT:
                items.append((1, [best_count << 11 | best_offset]))
            else:
                items.append((1, [best_offset, best_count]))
            step = best_count
        else:
            items.append((0, [words[pos]]))
            step = 1
        for p in range(pos, min(pos + step, n - 1)):
            chain = chains.setdefault((words[p], words[p + 1]), [])
            chain.append(p)
            if len(chain) > 64:  # only the nearest positions matter (the window is 2047 words)
                del chain[:32]
        pos += step
    items.append((1, [0, 0]))
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
        print(f"{argv[2]}: {len(data)} bytes packed, {len(raw)} unpacked, packed again {len(again)}, "
              f"round trip {'exact' if unpack(again) == raw else 'WRONG'}")
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
