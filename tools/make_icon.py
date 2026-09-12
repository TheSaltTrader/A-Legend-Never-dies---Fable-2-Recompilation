"""Draw the port's icon and write fable2.ico (16..256 px) plus a PNG preview.

A guild-seal motif: a dark blue disc with a gold ring, an inner ring of
notches like the seal's edge, and a bold serif "II" in gold. No game assets
are used - it is drawn here - so it can live in the repository.

    python tools/make_icon.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_ICO = os.path.join(ROOT, "resources", "fable2.ico")
OUT_PNG = os.path.join(ROOT, "resources", "fable2_icon_preview.png")

GOLD = (222, 178, 74)
GOLD_DARK = (150, 112, 36)
GOLD_LIGHT = (250, 222, 140)
BLUE = (22, 36, 66)
BLUE_DARK = (10, 16, 32)


def draw(size):
    """The icon at one size, drawn at 4x and downsampled for clean edges."""
    s = size * 4
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = s / 2
    r = s * 0.47
    # Disc with a subtle radial shade: darker rim.
    for i in range(12, 0, -1):
        k = i / 12.0
        col = tuple(int(BLUE_DARK[j] * (1 - k) + BLUE[j] * k) for j in range(3)) + (255,)
        rr = r * (0.55 + 0.45 * k)
        d.ellipse([c - rr, c - rr, c + rr, c + rr], fill=col)
    # Outer gold ring.
    w = max(2, s * 0.045)
    d.ellipse([c - r, c - r, c + r, c + r], outline=GOLD, width=int(w))
    d.ellipse([c - r + w, c - r + w, c + r - w, c + r - w], outline=GOLD_DARK, width=max(1, int(w * 0.35)))
    # Seal notches around an inner ring.
    ri = r * 0.80
    d.ellipse([c - ri, c - ri, c + ri, c + ri], outline=GOLD_DARK, width=max(1, int(s * 0.012)))
    n = 24
    for i in range(n):
        a = 2 * math.pi * i / n
        x0, y0 = c + math.cos(a) * ri * 0.94, c + math.sin(a) * ri * 0.94
        x1, y1 = c + math.cos(a) * ri * 1.04, c + math.sin(a) * ri * 1.04
        d.line([x0, y0, x1, y1], fill=GOLD, width=max(1, int(s * 0.018)))
    # The numeral.
    font = None
    for name in ("georgiab.ttf", "timesbd.ttf", "georgia.ttf", "times.ttf"):
        try:
            font = ImageFont.truetype(name, int(s * 0.62))
            break
        except OSError:
            continue
    if font is None:
        font = ImageFont.load_default()
    text = "II"
    bbox = d.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx, ty = c - tw / 2 - bbox[0], c - th / 2 - bbox[1] - s * 0.02
    # Shadow, dark outline, gold fill with a light top.
    d.text((tx + s * 0.02, ty + s * 0.025), text, font=font, fill=BLUE_DARK)
    d.text((tx, ty), text, font=font, fill=GOLD_DARK, stroke_width=max(1, int(s * 0.02)), stroke_fill=GOLD_DARK)
    d.text((tx, ty), text, font=font, fill=GOLD)
    # A light glint on the upper half of the numeral.
    glint = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glint)
    gd.text((tx, ty), text, font=font, fill=GOLD_LIGHT + (110,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rectangle([0, 0, s, int(c - s * 0.04)], fill=255)
    im.paste(glint, (0, 0), Image.composite(glint.split()[3], Image.new("L", (s, s), 0), mask))
    return im.resize((size, size), Image.LANCZOS)


def main():
    os.makedirs(os.path.dirname(OUT_ICO), exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = [draw(sz) for sz in sizes]
    images[-1].save(OUT_ICO, format="ICO", sizes=[(sz, sz) for sz in sizes],
                    append_images=images[:-1])
    # Preview sheet: the sizes side by side on a neutral ground.
    sheet = Image.new("RGBA", (sum(sizes) + 10 * (len(sizes) + 1), 276), (60, 60, 60, 255))
    x = 10
    for im in images:
        sheet.paste(im, (x, (276 - im.height) // 2), im)
        x += im.width + 10
    sheet.save(OUT_PNG)
    print("wrote", OUT_ICO, "and", OUT_PNG)


if __name__ == "__main__":
    main()
