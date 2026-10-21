#!/usr/bin/env python3
"""The games' text fonts and their PS2 button glyphs: the text's ○ × △ □ (in menus, messages and the English's {circle}
{cross} {triangle} {square}, see bh_text.english_bytes) are characters of the font, so for the keyboard and mouse the font's
cells for them show the menus' keys instead (a variant-1 range of the disc patch; the gamepad's variant 2 keeps the PS2
symbols). A button can mean another key in play (✕ is Esc, back, in the menus but Left Shift, run, in play), so the English's
{play:circle}... are four other cells (∈ ∋ ⊆ ⊇, which the games' text never uses) drawn as play's keys, or as the PS2's
symbols for the gamepad.

    bh_font.py GAME show OUT.png      each font's button glyphs as they are, with the menus' keys, with play's keys (8x)

The fonts (config/translate.toml's `fonts`, romdata.afs members): 7808 glyphs of 20x20 pixels, 2 bits each (4 levels, the
first pixel in a byte's top bits), 100 bytes a glyph, in JIS X 0208 order (row - 1) * 94 + (cell - 1); evidence: glyph 84 is
'&' (JIS 0x2175) and the symbols around it are JIS row 1's and 2's (docs/research/renderer.md 2.5 has the font's descriptor).
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

import bh_archive as archive
import bh_pictures as pictures
import bh_sources as sources

CELL, GLYPH = 20, 100
SYMBOLS = {"circle": 0x217B, "cross": 0x215F, "triangle": 0x2224, "square": 0x2222}  # ○ × △ □ in JIS X 0208
PLAY_SYMBOLS = {"circle": 0x223A, "cross": 0x223B, "triangle": 0x223C, "square": 0x223D}  # ∈ ∋ ⊆ ⊇


def index(jis):
    return ((jis >> 8) - 0x21) * 94 + (jis & 0xFF) - 0x21


def decode(raw):
    img = Image.new("L", (CELL, CELL))
    img.putdata([(raw[k // 4] >> (6 - 2 * (k % 4)) & 3) * 85 for k in range(CELL * CELL)])
    return img


def encode(img):
    levels = [min(3, (v + 42) // 85) for v in img.getdata()]
    return bytes(levels[i] << 6 | levels[i + 1] << 4 | levels[i + 2] << 2 | levels[i + 3] for i in range(0, len(levels), 4))


def key_glyph(label):
    """A key cap in a cell: a solid rounded cap at full brightness, the cell's whole width, with the key's name (or its
    symbol) cut out of it, drawn 4x and averaged down. The games show these cells at about 16 pixels: a frame with the name
    inside, or margins around the cap, leave a three-letter name (Esc, Spc, Tab) too few pixels to read, and a sharper
    filter than the plain average turns the letters' edges into the 4 levels' noise."""
    big = Image.new("L", (CELL * 4, CELL * 4), 0)
    d = ImageDraw.Draw(big)
    # A pixel left clear at each side: the games sample a column past a glyph's cell, so a cap to the cell's edge showed a
    # sliver of the next glyph's cap beside it and lost its own right corners (seen in File 2's pause menu)
    d.rounded_rectangle([4, 6, CELL * 4 - 5, CELL * 4 - 7], radius=12, fill=255)
    if label in pictures.KEY_SYMBOLS:
        d.polygon(pictures.KEY_SYMBOLS[label], fill=0)
        return big.resize((CELL, CELL), Image.BOX)
    # A single letter keeps clear of the cap's sides (a stroke near the edge leaves a sliver of cap that reads as a separate
    # bar); a longer name needs the whole width
    box = (6, 10, CELL * 4 - 6, CELL * 4 - 10) if len(label) > 1 else (16, 12, CELL * 4 - 16, CELL * 4 - 12)
    rgba = pictures.draw_text(Image.new("RGBA", big.size, (0, 0, 0, 0)), box, label,
                              "arialnarrow" if len(label) > 1 else "arial", None, (255, 255, 255, 255), None, "center")
    big.paste(0, (0, 0), rgba.split()[3].point(lambda a: 255 if a > 100 else 0))
    return big.resize((CELL, CELL), Image.BOX)


def xbox_glyph(button):
    """An Xbox pad's button in a cell: a solid disc with its letter cut out (the font's 4 levels have no colour), drawn 4x and
    averaged down as key_glyph is. The buttons by where they sit (bh_pictures.XBOX_FACE)"""
    letter = pictures.XBOX_FACE[button][0]
    big = Image.new("L", (CELL * 4, CELL * 4), 0)
    ImageDraw.Draw(big).ellipse([4, 4, CELL * 4 - 5, CELL * 4 - 5], fill=255)
    rgba = pictures.draw_text(Image.new("RGBA", big.size, (0, 0, 0, 0)), (18, 16, CELL * 4 - 18, CELL * 4 - 16), letter, "arial",
                              None, (255, 255, 255, 255), None, "center")
    big.paste(0, (0, 0), rgba.split()[3].point(lambda a: 255 if a > 100 else 0))
    return big.resize((CELL, CELL), Image.BOX)


def fonts(arc, game):
    return [(name, arc.open("/NETBIO00.DAT/" + name)) for name in sources.settings(game).get("fonts", [])]


def patches(arc, game):
    """[(disc offset, bytes, variant)]: each font's button glyphs, the keyboard's keys (1), the PS2's symbols (2) and an Xbox
    pad's buttons (3): the four symbols' own cells with the menus' keys, PLAY_SYMBOLS's with play's."""
    keys = pictures.key_names(game)
    out = []
    for name, node in fonts(arc, game):
        if node.size != 7808 * GLYPH:
            raise ValueError(f"{name}: {node.size} bytes, not a 7808-glyph font")
        for button, jis in SYMBOLS.items():
            symbol = node.read(index(jis) * GLYPH, GLYPH)
            for mode, cell in (("menus", jis), ("play", PLAY_SYMBOLS[button])):
                at = node.offset + index(cell) * GLYPH
                out.append((at, encode(key_glyph(keys[(mode, button)])), 1))
                out.append((at, symbol, 2))
                out.append((at, encode(xbox_glyph(button)), 3))
    return out


def main(argv):
    if len(argv) != 4 or argv[2] != "show":
        print(__doc__)
        return 1
    arc = archive.Archive(argv[1])
    keys = pictures.key_names(argv[1])
    found = fonts(arc, argv[1])
    sheet = Image.new("L", (CELL * 8 * 12, CELL * 8 * len(found)), 40)
    for row, (name, node) in enumerate(found):
        for col, (button, jis) in enumerate(SYMBOLS.items()):
            original = decode(node.read(index(jis) * GLYPH, GLYPH))
            menus, play = (decode(encode(key_glyph(keys[(mode, button)]))) for mode in ("menus", "play"))
            for k, img in enumerate((original, menus, play)):
                sheet.paste(img.resize((CELL * 8, CELL * 8), Image.NEAREST), ((col * 3 + k) * CELL * 8, row * CELL * 8))
    sheet.save(argv[3])
    print(f"{len(found)} fonts -> {argv[3]}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
