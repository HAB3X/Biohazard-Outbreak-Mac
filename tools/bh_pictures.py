#!/usr/bin/env python3
"""The pictures with Japanese in them, redrawn in English from your disc at build time (nothing of the game's art is in this
repository: translations/<game>/pictures.toml says where on a picture to erase and what English to draw, in which font;
translations/pictures-shared.toml the same for the pictures both discs have alike).

    bh_pictures.py GAME render OUTDIR [FILE]     the edited pictures as PNG, each device's variant, to look at
    bh_pictures.py GAME show FILE OUTDIR [ENTRY]  every picture of a container as PNG, 3x with a 16 pixel grid (to find boxes)
    bh_pictures.py GAME boxes FILE TIM [ENTRY]    the boxes of what's drawn on a transparent picture, row by row

FILE is a member of NETBIO00.DAT's archives ("romdata.afs/select.tex"). The containers: a TIM2 file (.tm2), a compressed
stream of TIM2 files (File 1's .tex, the .itt and .sld; bh_sld), or a MOMO container of such streams (File 2's .tex; ENTRY says
which). TIM is the index of the TIM2 file in the stream (tm2.files).

pictures.toml: [[picture]] with file, entry (MOMO only), tim, pic (default 0), note, and [[picture.edit]] entries:
  box = [x0, y0, x1, y1]     the area, x1/y1 excluded
  erase = "clear" (transparent), "inpaint" (the text's bright pixels filled from around them: text on a photo; threshold and
          blue tune which pixels: see inpaint()), "bg" (the box's most common colour), "#RRGGBB" or
          "#RRGGBBAA" (a fill), "keep" (draw over); default "clear"
  text = "..."               drawn in the box (\\n for lines); font, size (pixels; default: what fits), color, outline
                             ("#RRGGBB"; "auto": sampled from the box before it's erased; "" none), stroke (the outline's
                             width, 1), align (left, center, right)
  key = "circle"             a PS2 button drawn in the box (circle cross triangle square l1 r1 l2 r2 start select up down
                             left right dpad): left as it is for a gamepad, a key cap with the keyboard's key for the keyboard
                             and mouse; mode = "menus" (default) or "play" says which controls
A picture with a key edit gets two variants (laid by the runtime for the device used last: disc patch version 2); the others one.

The keyboard's keys come from the settings folder's bindings.txt when it exists (the runtime writes it; $BH_SETTINGS names the
folder, default ~/Library/Application Support/PS2 Native Runtime/<executable>), else from src/controls.cpp's defaults, so a
rebinding shows on the next build.
"""
import os
import re
import struct
import sys
import tomllib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

import bh_archive as archive
import bh_sld as sld
import tm2

FONTS = {
    "din": "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf",
    "dinalt": "/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf",
    "arialnarrow": "/System/Library/Fonts/Supplemental/Arial Narrow Bold.ttf",
    "arial": "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    "avenir": ("/System/Library/Fonts/Avenir Next.ttc", 2),  # Demi Bold
    "avenircond": ("/System/Library/Fonts/Avenir Next Condensed.ttc", 2),
    "helvetica": ("/System/Library/Fonts/HelveticaNeue.ttc", 0),
    "fredoka": "ps2-runtime:runtime/fonts/Fredoka-SemiBold.ttf",
    "luckiest": "ps2-runtime:runtime/fonts/LuckiestGuy-Regular.ttf",
    "unicode": "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",  # the arrows
}
BUTTONS = {"circle", "cross", "triangle", "square", "l1", "r1", "l2", "r2", "start", "select", "up", "down", "left", "right",
           "updown", "leftright", "dpad"}
KEY_LABELS = {"Space": "Spc", "Escape": "Esc", "Return": "Enter", "Left Shift": "Shift", "Right Shift": "Shift", "Left Ctrl": "Ctrl",
              "Right Ctrl": "Ctrl", "Mouse Left": "LMB", "Mouse Right": "RMB", "Mouse Middle": "MMB", "Page Up": "PgUp",
              "Page Down": "PgDn", "Backspace": "Bksp", "Left Alt": "Alt", "Up": "\u2191", "Down": "\u2193",
              "Left": "\u2190", "Right": "\u2192"}


class DoesNotFit(Exception):
    pass


def spec(game):
    """The game's pictures.toml and translations/pictures-shared.toml (the pictures both discs have the same)."""
    out = []
    for path in (archive.REPO / "translations" / game / "pictures.toml", archive.REPO / "translations" / "pictures-shared.toml"):
        if path.exists():
            out += tomllib.loads(path.read_text()).get("picture", [])
    return out


# --- the keyboard's keys -------------------------------------------------------------------------------------------------

def _default_actions():
    """{action name: (PS2 button, [keys], mode)} from src/controls.cpp's table: {"name", "description", "button", "keys", Mode}."""
    text = (archive.REPO / "src" / "controls.cpp").read_text()
    out = {}
    for name, button, keys, mode in re.findall(r'\{"(\w+)", "[^"]*", "(\w+)", "([^"]*)", (\w+)\}', text):
        out[name] = (button, [k.strip() for k in keys.split(",")], mode.lower())
    if not out:
        raise ValueError("src/controls.cpp: no controls table found")
    return out


def bindings_path(game):
    folder = os.environ.get("BH_SETTINGS")
    if not folder:
        exe = tomllib.loads((archive.REPO / "config" / "translate.toml").read_text())[game]["executable"]
        folder = os.path.expanduser(f"~/Library/Application Support/PS2 Native Runtime/{exe}")
    return Path(folder) / "bindings.txt"


def key_names(game):
    """{(mode, PS2 button): the key's label} for the key caps: the first key of the action for that button in that mode."""
    actions = _default_actions()
    path = bindings_path(game)
    if path.exists():
        for line in path.read_text().splitlines():
            if "=" in line and not line.startswith("#"):
                name, keys = line.split("=", 1)
                name = name.strip()
                if name in actions and keys.strip():
                    actions[name] = (actions[name][0], [k.strip() for k in keys.split(",") if k.strip()], actions[name][2])
    out = {}
    for name, (button, keys, mode) in actions.items():
        mode = "play" if mode == "play" else "menus" if mode == "menus" else "title"
        if keys and (mode, button) not in out:
            out[(mode, button)] = KEY_LABELS.get(keys[0], keys[0])
    for (mode, button), label in list(out.items()):  # a button the menus have no key of (L2, R2): play's key for it
        if mode == "play" and ("menus", button) not in out:
            out[("menus", button)] = label
    for mode in ("play", "menus"):  # the D-pad's pairs and the whole of it, as their keys together
        out[(mode, "updown")] = out[(mode, "up")] + out[(mode, "down")]
        out[(mode, "leftright")] = out[(mode, "left")] + out[(mode, "right")]
        out[(mode, "dpad")] = "WASD" if mode == "play" else "Arrows"
    return out


# --- drawing ---------------------------------------------------------------------------------------------------------------

def font(name, size):
    path = FONTS[name]
    index = 0
    if isinstance(path, tuple):
        path, index = path
    if path.startswith("ps2-runtime:"):
        path = str(archive.runtime_tools().parent / path.split(":", 1)[1])
    return ImageFont.truetype(path, size, index=index)


def _color(value):
    v = value.lstrip("#")
    rgba = tuple(int(v[i:i + 2], 16) for i in range(0, len(v), 2))
    return rgba + (255,) * (4 - len(rgba))


def sampled_colors(image, box):
    """(text colour, outline colour or None) of a label: the most common bright and dark opaque colours in the box."""
    px = np.asarray(image.crop(box).convert("RGBA")).reshape(-1, 4)
    px = px[px[:, 3] > 160]
    if len(px) == 0:
        return (255, 255, 255, 255), None
    lum = (px[:, :3] * np.array([0.299, 0.587, 0.114])).sum(1)
    order = np.argsort(lum)
    def common(sel):
        vals, counts = np.unique(sel, axis=0, return_counts=True)
        return tuple(int(c) for c in vals[counts.argmax()])
    bright = common(px[order[int(len(order) * 0.75):]])
    dark = common(px[order[:max(1, len(order) // 4)]])
    contrast = abs(sum(bright[:3]) - sum(dark[:3]))
    return bright, (dark if contrast > 150 else None)


def inpaint(image, box, threshold=None, blue=None):
    """Fills the bright (text) pixels in the box from their surroundings: the mask is the pixels brighter than the box's
    threshold (default: halfway between its median and its brightest), and with `blue` those whose blue exceeds their red and
    green by that much (a blue glow around the text), grown by two pixels; each masked pixel becomes the mean of its unmasked
    neighbours, repeatedly from the edges in."""
    x0, y0, x1, y1 = box
    a = np.asarray(image, dtype=np.float32).copy()
    region = (a[y0:y1, x0:x1, :3] * np.array([0.299, 0.587, 0.114], np.float32)).sum(2)
    if threshold is None:
        threshold = (np.median(region) + region.max()) / 2
    mask = np.zeros(a.shape[:2], bool)
    mask[y0:y1, x0:x1] = region > threshold
    if blue is not None:
        rgb = a[y0:y1, x0:x1, :3]
        mask[y0:y1, x0:x1] |= rgb[..., 2] - np.maximum(rgb[..., 0], rgb[..., 1]) > blue
    m = Image.fromarray(mask.astype(np.uint8) * 255).filter(ImageFilter.MaxFilter(5))
    mask = np.asarray(m) > 0
    known = ~mask
    for _ in range(400):
        if known.all():
            break
        total = np.zeros_like(a)
        count = np.zeros(a.shape[:2], np.float32)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            shifted_known = np.roll(known, (dy, dx), (0, 1))
            total += np.roll(a, (dy, dx), (0, 1)) * shifted_known[..., None]
            count += shifted_known
        fill = ~known & (count > 0)
        a[fill] = total[fill] / count[fill][:, None]
        known = known | fill
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")


def draw_text(image, box, text, name="din", size=None, color=(255, 255, 255, 255), outline=None, align="left", stroke=1):
    """Draws text in the box, the size the largest (up to `size`) whose lines fit, centred vertically."""
    x0, y0, x1, y1 = box
    lines = text.split("\n")
    size = size or (y1 - y0) // len(lines) + 2
    while True:
        f = font(name, size)
        boxes = [f.getbbox(line or " ") for line in lines]
        width = max(b[2] - b[0] for b in boxes) + (2 * stroke if outline else 0)
        asc, desc = f.getmetrics()
        line_h = max(1, int((asc + desc) * 0.85)) if len(lines) > 1 else asc + desc
        height = line_h * (len(lines) - 1) + (boxes[-1][3] - min(b[1] for b in boxes))
        if (width <= x1 - x0 and height <= y1 - y0 + 2) or size <= 6:
            break
        size -= 1
    if width > x1 - x0 + 1:
        raise DoesNotFit(f"text {text!r} needs {width} pixels, the box has {x1 - x0}")
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    top = y0 + (y1 - y0 - height) // 2 - min(b[1] for b in boxes)
    for n, (line, b) in enumerate(zip(lines, boxes)):
        w = b[2] - b[0]
        x = x0 + (0 if align == "left" else (x1 - x0 - w) // 2 if align == "center" else x1 - x0 - w) - b[0]
        y = top + n * line_h
        if outline:
            d.text((x, y), line, font=f, fill=outline, stroke_width=stroke, stroke_fill=outline)
        d.text((x, y), line, font=f, fill=color)
    clipped = Image.new("RGBA", image.size, (0, 0, 0, 0))
    clipped.paste(layer.crop(box), box[:2])
    return Image.alpha_composite(image, clipped)


# Keys whose name doesn't fit a small cap, drawn as the symbol their key caps have (polygons in an 80 x 80 square): Shift's
# arrow, Return's bent arrow
KEY_SYMBOLS = {
    "Shift": [(40, 18), (61, 41), (49, 41), (49, 62), (31, 62), (31, 41), (19, 41)],
    "Enter": [(54, 20), (64, 20), (64, 52), (36, 52), (36, 62), (16, 46), (36, 30), (36, 40), (54, 40)],
}
KEY_FACE, KEY_EDGE, KEY_INK = (238, 238, 244, 255), (70, 70, 84, 255), (24, 24, 32, 255)
SUPERSAMPLE = 4


def draw_key(image, box, label):
    """A key cap filling the box: a light face over a darker edge (the key's side, below it), the key's name or symbol in
    dark ink on the face. Drawn 4x and scaled down: the caps are 16 to 26 pixels, where a font's own small sizes and a
    one-pixel frame look rough, and the games tint the pictures, so dark ink on a light face keeps its contrast."""
    x0, y0, x1, y1 = box
    w, h, s = x1 - x0, y1 - y0, SUPERSAMPLE
    big = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    radius = min(w, h) * s // 4
    d.rounded_rectangle([0, s, w * s - 1, h * s - 1], radius=radius, fill=KEY_EDGE)
    d.rounded_rectangle([0, 0, w * s - 1, h * s - 1 - 2 * s], radius=radius, fill=KEY_FACE)
    face = (2 * s, s, w * s - 2 * s, h * s - 3 * s)
    if label in KEY_SYMBOLS:
        side = min(face[2] - face[0], face[3] - face[1])
        left, top = (face[0] + face[2] - side) / 2, (face[1] + face[3] - side) / 2
        d.polygon([(left + x * side / 80, top + y * side / 80) for x, y in KEY_SYMBOLS[label]], fill=KEY_INK)
    else:
        name = "unicode" if any(ord(c) > 0x7F for c in label) else "arialnarrow" if len(label) > 1 else "arial"
        # Two keys in a tall cap (W S for up and down) stand one over the other, as large as the cap allows
        text = label.replace(" ", "\n") if h > w * 1.3 else label
        big = draw_text(big, face, text, name, None, KEY_INK, None, "center")
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    layer.paste(big.resize((w, h), Image.LANCZOS), (x0, y0))
    return Image.alpha_composite(image, layer)


def apply_edits(image, edits, variant, keys):
    """The picture with its edits for a variant (0 or 2: the gamepad's buttons kept; 1: the keyboard's keys drawn)."""
    image = image.convert("RGBA")
    mask = np.zeros((image.size[1], image.size[0]), bool)
    for e in edits:
        box = tuple(e["box"])
        if "key" in e:
            if e["key"] not in BUTTONS:
                raise ValueError(f"key {e['key']!r}: not a PS2 button ({', '.join(sorted(BUTTONS))})")
            if variant == 1:
                image = draw_key(image, box, keys[(e.get("mode", "menus"), e["key"])])
                mask[box[1]:box[3], box[0]:box[2]] = True
            continue
        color, outline = sampled_colors(image, box)
        if e.get("color", "auto") != "auto":
            color = _color(e["color"])
        if e.get("outline", "auto") != "auto":
            outline = _color(e["outline"]) if e["outline"] else None
        erase = e.get("erase", "clear")
        if erase == "clear":
            image.paste((0, 0, 0, 0), box)
        elif erase == "inpaint":
            image = inpaint(image, box, e.get("threshold"), e.get("blue"))
        elif erase == "bg":
            px = np.asarray(image.crop(box)).reshape(-1, 4)
            vals, counts = np.unique(px, axis=0, return_counts=True)
            image.paste(tuple(int(c) for c in vals[counts.argmax()]), box)
        elif erase.startswith("#"):
            image.paste(_color(erase), box)
        elif erase != "keep":
            raise ValueError(f"erase {erase!r}: clear, inpaint, bg, keep or a colour")
        if e.get("text"):
            image = draw_text(image, box, e["text"], e.get("font", "din"), e.get("size"), color, outline,
                              e.get("align", "left"), e.get("stroke", 1))
        mask[box[1]:box[3], box[0]:box[2]] = True
        if erase == "inpaint":  # the fill reaches past the box by the mask's growth
            mask[max(0, box[1] - 3):box[3] + 3, max(0, box[0] - 3):box[2] + 3] = True
    return image, mask


# --- the containers ---------------------------------------------------------------------------------------------------------

class Container:
    """A file of pictures: its node, its blobs (one, or a MOMO container's entries) and whether they're compressed."""

    def __init__(self, arc, path):
        self.path = path
        self.node = arc.open("/NETBIO00.DAT/" + path)
        self.data = self.node.read()
        if self.data[:4] == b"MOMO":
            count = struct.unpack_from("<I", self.data, 4)[0]
            self.entries = [struct.unpack_from("<II", self.data, 8 + 8 * i) for i in range(count)]
        else:
            self.entries = [(0, len(self.data))]

    def blob(self, entry):
        """(offset of the entry in the file, its bytes, the TIM2 stream's bytes, whether compressed)."""
        offset, size = self.entries[entry]
        raw = self.data[offset:offset + size]
        if raw[:4] == b"TIM2":
            return offset, raw, raw, False
        return offset, raw, sld.unpack(raw), True


def build(arc, game, only=None):
    """{(file, entry): {variant: (disc offset, bytes, [(name, PIL image)])}} for the pictures of the spec."""
    keys = key_names(game)
    groups = {}
    for p in spec(game):
        if only and p["file"] != only:
            continue
        groups.setdefault((p["file"], p.get("entry", 0)), []).append(p)
    out = {}
    containers = {}
    for (path, entry), pics in groups.items():
        c = containers.setdefault(path, Container(arc, path))
        offset, raw, stream, packed = c.blob(entry)
        files = tm2.files(stream)
        has_keys = any("key" in e for p in pics for e in p.get("edit", []))
        out[(path, entry)] = {}
        for variant in ((1, 2) if has_keys else (0,)):
            new = bytearray(stream)
            images = []
            for p in pics:
                pic = files[p["tim"]][1][p.get("pic", 0)]
                image, mask = apply_edits(pic.image(), p.get("edit", []), variant, keys)
                new[pic.img_at:pic.img_at + pic.img_size] = pic.encoded(image, mask)
                images.append((f"{Path(path).name}_{entry}_{p['tim']}_{p.get('pic', 0)}_v{variant}", image))
            data = sld.pack(bytes(new)) if packed else bytes(new)
            if packed and len(data) > len(raw):
                data = sld.pack_smallest(bytes(new))
            if len(data) > len(raw):
                raise DoesNotFit(f"{path} entry {entry} variant {variant}: compressed to {len(data)} bytes, the original has "
                                 f"{len(raw)}")
            data += bytes(len(raw) - len(data))
            out[(path, entry)][variant] = (c.node.offset + offset, data, images)
    return out


def patches(arc, game):
    """[(disc offset, bytes, variant)] of the pictures: a picture's whole container entry (its compressed stream can change
    from the first edited byte on), the same offset and size in both of a picture's device variants."""
    out = []
    for variants in build(arc, game).values():
        for variant, (offset, data, _) in variants.items():
            if variant == 0:
                old = arc.disc.read_sectors(offset // 2048, (offset % 2048 + len(data) + 2047) // 2048)[offset % 2048:]
                out += [(offset + o, d, 0) for o, d in _runs(old[:len(data)], data)]
            else:
                out.append((offset, data, variant))
    return out


def _runs(old, new, gap=16):
    runs, i = [], 0
    while i < len(old):
        if old[i] == new[i]:
            i += 1
            continue
        j = i
        while j < len(old) and any(old[k] != new[k] for k in range(j, min(j + gap, len(old)))):
            j += 1
        runs.append((i, new[i:j]))
        i = j
    return runs


# --- looking at pictures ----------------------------------------------------------------------------------------------------

def show(arc, path, outdir, entry=None):
    c = Container(arc, path)
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    for e in ([entry] if entry is not None else range(len(c.entries))):
        if not c.entries[e][1]:
            continue
        _, _, stream, _ = c.blob(e)
        for t, (_, pics) in enumerate(tm2.files(stream)):
            for n, pic in enumerate(pics):
                im = pic.image()
                big = Image.new("RGBA", (im.width * 3, im.height * 3), (90, 90, 100, 255))
                big.alpha_composite(im.resize((im.width * 3, im.height * 3), Image.NEAREST))
                d = ImageDraw.Draw(big)
                for x in range(0, im.width, 16):
                    d.line([(x * 3, 0), (x * 3, big.height)], fill=(255, 255, 0, 90 if x % 64 else 200))
                for y in range(0, im.height, 16):
                    d.line([(0, y * 3), (big.width, y * 3)], fill=(255, 255, 0, 90 if y % 64 else 200))
                big.save(out / f"{Path(path).name}_{e}_{t}_{n}_{im.width}x{im.height}.png")


def boxes(arc, path, tim, entry=0):
    """Rows of drawn pixels (alpha > 0) and, in each, the runs of columns: the labels of a strip picture."""
    _, _, stream, _ = Container(arc, path).blob(entry)
    im = np.asarray(tm2.files(stream)[tim][1][0].image())
    alpha = im[:, :, 3] > 40
    rows = alpha.any(1)
    y = 0
    while y < len(rows):
        if not rows[y]:
            y += 1
            continue
        y1 = y
        while y1 < len(rows) and rows[y1]:
            y1 += 1
        cols = alpha[y:y1].any(0)
        runs, x = [], 0
        while x < len(cols):
            if cols[x]:
                x1 = x
                while x1 < len(cols) and (cols[x1:x1 + 4].any()):
                    x1 += 1
                runs.append((x, x1))
                x = x1
            x += 1
        print(f"y {y}-{y1}: " + " ".join(f"[{a},{b}]" for a, b in runs))
        y = y1


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    game, cmd = argv[1], argv[2]
    arc = archive.Archive(game)
    if cmd == "render":
        out = Path(argv[3])
        out.mkdir(parents=True, exist_ok=True)
        for variants in build(arc, game, argv[4] if len(argv) > 4 else None).values():
            for _, (_, data, images) in variants.items():
                for name, image in images:
                    image.save(out / f"{name}.png")
        print(f"-> {out}")
    elif cmd == "show":
        show(arc, argv[3], argv[4], int(argv[5]) if len(argv) > 5 else None)
    elif cmd == "boxes":
        boxes(arc, argv[3], int(argv[4]), int(argv[5]) if len(argv) > 5 else 0)
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
