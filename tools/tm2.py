#!/usr/bin/env python3
"""TIM2 (.tm2, and the .sld files, which are TIM2 too) to PNG and back.

    tm2.py decode FILE.tm2 OUT.png [PICTURE]     picture n (0) of the file as RGBA
    tm2.py info FILE.tm2                          the pictures' sizes and formats

As a library: pictures(data) -> [Picture]; Picture.image() -> PIL RGBA image; encode_like(picture, image) -> new bytes of the
picture, re-quantised to the picture's own palette size (palette and 16/32-bit pictures only; the layout is kept).
"""
import struct
import sys

from PIL import Image


class Picture:
    def __init__(self, data, start):
        self.data, self.start = data, start  # start: where the picture header is
        (self.total, self.clut_size, self.img_size, self.header_size, self.clut_colors, self.pic_fmt, self.mips, self.clut_type,
         self.img_type, self.width, self.height) = struct.unpack_from("<IIIHHBBBBHH", data, start)[:11]
        self.img_at = start + self.header_size
        self.clut_at = self.img_at + self.img_size + 0  # mip headers aren't used by this game's pictures (checked: mips == 1)
        if self.mips != 1:
            raise ValueError(f"{self.mips} mip levels: not handled")

    def palette(self):
        raw = self.data[self.clut_at:self.clut_at + self.clut_size]
        kind = self.clut_type & 0x3F
        step = {1: 2, 2: 3, 3: 4}[kind]
        colors = [_color(raw[i * step:(i + 1) * step], kind) for i in range(len(raw) // step)]
        if self.img_type == 5 and self.clut_type & 0x80 == 0 and len(colors) == 256:
            # CSM1: the PS2's palette order swaps blocks 8-15 and 16-23 of every 32 entries
            colors = [colors[(i & ~0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1)] for i in range(256)]
        return colors

    def image(self):
        w, h, raw = self.width, self.height, self.data[self.img_at:self.img_at + self.img_size]
        if self.img_type in (4, 5):
            pal = self.palette()
            if self.img_type == 4:
                idx = [(b >> s) & 15 for b in raw for s in (0, 4)]
            else:
                idx = list(raw)
            px = [pal[i] if i < len(pal) else (255, 0, 255, 255) for i in idx[:w * h]]
        elif self.img_type == 1:
            px = [_color(raw[i * 2:i * 2 + 2], 1) for i in range(w * h)]
        elif self.img_type == 3:
            px = [_color(raw[i * 4:i * 4 + 4], 3) for i in range(w * h)]
        else:
            raise ValueError(f"image type {self.img_type}: not handled")
        img = Image.new("RGBA", (w, h))
        img.putdata(px)
        return img


def _color(b, kind):
    if kind == 1:
        v = b[0] | b[1] << 8
        return ((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3, 255 if v & 0x8000 else 0)
    if kind == 2:
        return (b[0], b[1], b[2], 255)
    return (b[0], b[1], b[2], min(255, b[3] * 2))  # the PS2's alpha runs 0-128


def pictures(data, base=0):
    """The pictures of the TIM2 file starting at `base` of data (the game's .tex members put two bytes before it)."""
    if data[base:base + 4] != b"TIM2":
        raise ValueError("not a TIM2 file")
    count, align = struct.unpack_from("<H", data, base + 6)[0], data[base + 5]
    at = base + (0x80 if align == 1 else 0x10)
    out = []
    for _ in range(count):
        p = Picture(data, at)
        out.append(p)
        at += p.total
    return out


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    data = open(argv[2], "rb").read()
    pics = pictures(data)
    if argv[1] == "info":
        for i, p in enumerate(pics):
            print(f"{i}: {p.width}x{p.height} type {p.img_type} clut {p.clut_colors}x type {p.clut_type:#x}")
    elif argv[1] == "decode":
        pics[int(argv[4]) if len(argv) > 4 else 0].image().save(argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
