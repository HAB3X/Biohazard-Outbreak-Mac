"""The apps' icons. draw(picture, game): from the game's own title screen (a picture the build's own program takes of the disc
you gave it: nothing of the game's is kept in the repository), the logo cut out of a title-screen picture, its scan lines
softened, on the same picture darkened and blurred, in a rounded square: for the apps made for this Mac. draw_plain(game,
font): the game's name in a font of the runtime's on a dark red square, with nothing of the game's art: for the release, which
is shared (the title logo is Capcom's). write_icns(image, path) makes an .icns with iconutil."""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont

SIZE = 1024
# Where the logo is in the 1536 x 1344 picture of each game's title screen: (left, top, right, bottom)
LOGO = {"file1": (150, 340, 1390, 720), "file2": (110, 250, 1420, 820)}


def draw(picture, game):
    shot = Image.open(picture).convert("RGB")
    # the background: the whole picture, cropped square, darkened and blurred
    side = min(shot.size)
    left = (shot.width - side) // 2
    bg = shot.crop((left, 0, left + side, side)).resize((SIZE, SIZE), Image.LANCZOS)
    bg = ImageEnhance.Brightness(bg.filter(ImageFilter.GaussianBlur(28))).enhance(0.7)
    # the logo: scan lines softened by a vertical blur, fitted to 88% of the width
    logo = shot.crop(LOGO[game]).filter(ImageFilter.BoxBlur((0, 2)))
    width = int(SIZE * 0.88)
    logo = logo.resize((width, int(logo.height * width / logo.width)), Image.LANCZOS)
    logo = ImageEnhance.Contrast(logo).enhance(1.15)
    canvas = bg.convert("RGBA")
    # feathered edges, so the logo melts into the background instead of showing its rectangle
    feather = Image.new("L", logo.size, 0)
    ImageDraw.Draw(feather).rectangle((36, 30, logo.width - 36, logo.height - 30), fill=255)
    feather = feather.filter(ImageFilter.GaussianBlur(26))
    canvas.paste(logo, ((SIZE - width) // 2, (SIZE - logo.height) // 2), feather)
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle((40, 40, SIZE - 40, SIZE - 40), radius=210, fill=255)
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    out.paste(canvas, (0, 0), mask)
    return out


def draw_plain(game, font, line=None):
    """The game's name, OUTBREAK over FILE 1 or FILE 2 (or `line`), in `font` (a .ttf path), on a dark red rounded square."""
    gradient = Image.linear_gradient("L").resize((SIZE, SIZE))
    canvas = Image.merge("RGB", (gradient.point(lambda v: 70 - v * 58 // 255), gradient.point(lambda v: 10 - v * 8 // 255),
                                 gradient.point(lambda v: 12 - v * 9 // 255))).convert("RGBA")
    pen = ImageDraw.Draw(canvas)
    pen.rounded_rectangle((92, 92, SIZE - 92, SIZE - 92), radius=150, outline=(178, 24, 24), width=14)
    for text, size, y, colour in (("BIOHAZARD", 92, 300, (200, 190, 180)), ("OUTBREAK", 146, 470, (240, 236, 228)),
                                  (line or f"FILE {game[-1]}", 120, 690, (205, 30, 30))):
        face = ImageFont.truetype(str(font), size)
        pen.text((SIZE // 2, y), text, font=face, fill=colour, anchor="mm")
    mask = Image.new("L", (SIZE, SIZE), 0)
    ImageDraw.Draw(mask).rounded_rectangle((40, 40, SIZE - 40, SIZE - 40), radius=210, fill=255)
    out = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    out.paste(canvas, (0, 0), mask)
    return out


def write_icns(image, path):
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "AppIcon.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            image.resize((size, size), Image.LANCZOS).save(iconset / f"icon_{size}x{size}.png")
            image.resize((size * 2, size * 2), Image.LANCZOS).save(iconset / f"icon_{size}x{size}@2x.png")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(path)], check=True)
