#!/usr/bin/env python3
"""Generate 1-bit test pages (PBM, P4) for the encoder comparison tests.

Usage: gen_pages.py WIDTH HEIGHT OUTDIR [XSCALE [PAGE...]]

XSCALE is 2 for 1200x600 dpi pages (pixels are twice as dense horizontally),
otherwise 1.  Every page is deterministic so runs are reproducible.  Large
pages are computed in strips to keep the memory use modest.
"""
import sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, OUT = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3]
XS = int(sys.argv[4]) if len(sys.argv) > 4 else 1
STRIP = 256

BAYER8 = np.array([
    [0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26],
    [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
    [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
    [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]])


def strips():
    for y0 in range(0, H, STRIP):
        yield y0, min(H, y0 + STRIP)


def save(name, black):
    """black: bool array H x W, True = toner."""
    assert black.shape == (H, W), (name, black.shape)
    with open('%s/%s.pbm' % (OUT, name), 'wb') as out:
        out.write(b'P4\n%d %d\n' % (W, H))
        out.write(np.packbits(black, axis=1).tobytes())


def widen(black):
    """Content is laid out for square pixels; stretch it for 1200x600."""
    if XS > 1:
        black = np.repeat(black, XS, axis=1)
    if black.shape[1] < W:
        black = np.pad(black, ((0, 0), (0, W - black.shape[1])))
    return black[:, :W]


def font(size, name='DejaVuSans.ttf'):
    try:
        return ImageFont.truetype('/usr/share/fonts/truetype/dejavu/' + name, size)
    except OSError:
        return ImageFont.load_default()


def text_page():
    w = W // XS
    img = Image.new('L', (w, H), 255)
    d = ImageDraw.Draw(img)
    lorem = ("The quick brown fox jumps over the lazy dog. Pack my box with five "
             "dozen liquor jugs. Sphinx of black quartz, judge my vow. 0123456789 ")
    y = min(300, H // 20)
    d.text((min(300, w // 10), y), "Haiku SPL driver test page",
           font=font(160, 'DejaVuSans-Bold.ttf'), fill=0)
    y += 260
    for size, face in ((83, 'DejaVuSans.ttf'), (100, 'DejaVuSerif.ttf'),
                       (67, 'DejaVuSansMono.ttf'), (125, 'DejaVuSerif-Bold.ttf'),
                       (50, 'DejaVuSans.ttf')):
        f = font(size, face)
        per_line = max(10, int((w - 600) / (size * 0.55)))
        txt = lorem * 12
        for i in range(0, min(len(txt), per_line * 9), per_line):
            if y > H - 400:
                break
            d.text((min(300, w // 10), y), txt[i:i + per_line], font=f, fill=0)
            y += int(size * 1.3)
        y += 60
    save('text', widen(np.asarray(img) < 128))


def graphics_page():
    w = W // XS
    black = np.zeros((H, w), bool)
    xx = np.arange(w)[None, :]
    for y0, y1 in strips():
        yy = np.arange(y0, y1)[:, None]
        grey = np.full((y1 - y0, w), 255.0, np.float32)
        bar = (yy > H * 0.08) & (yy < H * 0.2)
        grey = np.where(bar, xx / float(w) * 255, grey)
        r = np.hypot(xx - w * 0.5, yy - H * 0.45) / (w * 0.3)
        grey = np.where(r < 1, r * 255, grey)
        box = (yy > H * 0.7) & (yy < H * 0.9) & (xx > w * 0.1) & (xx < w * 0.9)
        grey = np.where(box, (yy - H * 0.7) / (H * 0.2) * 255, grey)
        black[y0:y1] = grey < (BAYER8[yy % 8, xx % 8] + 0.5) * 4
    img = Image.fromarray(np.where(black, 0, 255).astype(np.uint8), 'L')
    d = ImageDraw.Draw(img)
    for i in range(12):
        d.line((w * 0.04, H * 0.03 + i * 40, w * 0.96, H * 0.04 + i * 55),
               fill=0, width=1 + i % 4)
    d.rectangle((w * 0.1, H * 0.6, w * 0.3, H * 0.66), fill=0)
    d.ellipse((w * 0.6, H * 0.58, w * 0.85, H * 0.68), outline=0, width=6)
    save('graphics', widen(np.asarray(img) < 128))


def fs_page():
    """Error-diffused photo-like content: much harder to compress."""
    w = W // XS
    grey = np.empty((H, w), np.uint8)
    xx = np.arange(w, dtype=np.float32)[None, :]
    for y0, y1 in strips():
        yy = np.arange(y0, y1, dtype=np.float32)[:, None]
        g = (0.5 + 0.25 * np.sin(xx / 311.0) * np.cos(yy / 173.0)
             + 0.25 * np.sin((xx + yy) / 97.0)) * 255
        grey[y0:y1] = np.clip(g, 0, 255)
    img = Image.fromarray(grey, 'L').convert('1')      # Floyd-Steinberg
    save('fsdither', widen(~np.asarray(img)))


def edges_page():
    b = np.zeros((H, W), bool)
    for inset in (0, 1, 7, 8, 64, 96, 97, 125, 126, 200):
        if inset * 2 + 2 >= min(W, H):
            continue
        b[inset, inset:W - inset] = True
        b[H - 1 - inset, inset:W - inset] = True
        b[inset:H - inset, inset] = True
        b[inset:H - inset, W - 1 - inset] = True
    b[H // 2, :] = True
    b[:, W // 2] = True
    save('edges', b)


def fine_page():
    b = np.zeros((H, W), bool)
    xx = np.arange(W)[None, :]
    q = max(1, H // 5)
    for y0, y1 in strips():
        yy = np.arange(y0, y1)[:, None]
        zone = np.minimum(yy // q, 4)
        patterns = (
            (xx + yy) % 2 == 0,                         # checkerboard
            (xx % 2 == 0) & (yy >= 0),                  # vertical hairlines
            (yy % 2 == 0) & (xx >= 0),                  # horizontal hairlines
            (xx // 16 + yy // 16) % 2 == 0,             # coarse checkerboard
            (xx * 7 + yy * 13) % 31 < 3,                # diagonal texture
        )
        out = np.zeros((y1 - y0, W), bool)
        for k, pattern in enumerate(patterns):
            out |= (zone == k) & pattern
        b[y0:y1] = out
    save('fine', b)


def random_page(name, seed, density):
    rng = np.random.default_rng(seed)
    b = np.zeros((H, W), bool)
    for y0, y1 in strips():
        b[y0:y1] = rng.random((y1 - y0, W), dtype=np.float32) < density
    save(name, b)


GENERATORS = {
    'text': text_page, 'graphics': graphics_page, 'fsdither': fs_page,
    'edges': edges_page, 'fine': fine_page,
    'noise': lambda: random_page('noise', 12345, 0.5),
    'sparse': lambda: random_page('sparse', 777, 0.0005),
    'blank': lambda: save('blank', np.zeros((H, W), bool)),
    'black': lambda: save('black', np.ones((H, W), bool)),
}

for page in (sys.argv[5:] or sorted(GENERATORS)):
    GENERATORS[page]()
