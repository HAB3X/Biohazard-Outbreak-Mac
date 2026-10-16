"""The apps' icons, from the game's own title screen (a picture the build's own program takes of the disc you gave it: nothing
of the game's is kept in the repository): the logo cut out of a title-screen picture, its scan lines softened, on the same
picture darkened and blurred, in a rounded square. write_icns(picture, game, path) makes an .icns with iconutil."""
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

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


def write_icns(picture, game, path):
    image = draw(picture, game)
    with tempfile.TemporaryDirectory() as tmp:
        iconset = Path(tmp) / "AppIcon.iconset"
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            image.resize((size, size), Image.LANCZOS).save(iconset / f"icon_{size}x{size}.png")
            image.resize((size * 2, size * 2), Image.LANCZOS).save(iconset / f"icon_{size}x{size}@2x.png")
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(path)], check=True)
