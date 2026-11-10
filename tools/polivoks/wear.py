#!/usr/bin/env python3
"""The wear layer for the plug-in's Polivoks panel: R2Synth's studio-worn
finish (render-weathered.py), here at SampleDrum's nearly-new level, produced as a transparent RGBA overlay rather
than baked into a picture, so the plug-in can lay it over its live controls.

usage: wear.py panel-full.png panel-layout.json outdir - - 
writes outdir/pv_wear.png (2x)

Original R2Synth notes: mock-ups: dust, scratches, dents on the panel,
knobs, selectors and screen glass. Post-processes docs/panel.png (2x) using
the plug-in's own layout dump for where things are.

usage: weather.py panel.png layout.json outdir
"""
import json, math, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi

S = 2  # layout units -> pixels

src, layout, out = sys.argv[1:4]
L = json.load(open(layout))
BASE = np.asarray(Image.open(src).convert('RGB')).astype(np.float32) / 255.0
H, W = BASE.shape[:2]
YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)


# ---------------------------------------------------------------------------
# geometry (ForTran: window.LAYOUT from pv.js, 1x units)
# ---------------------------------------------------------------------------
def knobs():
    for k in L['knobs']:
        yield dict(cx=k['cx'] * S, cy=k['cy'] * S, body=k['body'] * S, cap=k['cap'] * S, label=k['label'])


def selectors():
    for s in L['sels']:
        yield dict(cx=s['cx'] * S, cy=s['cy'] * S, r=s['r'] * S, label=s['label'])


def screens():
    for r in L['screens']:
        yield [r[0] * S, r[1] * S, r[2] * S, r[3] * S]


def disc(cx, cy, r, soft=1.0):
    d = np.hypot(XX - cx, YY - cy)
    return np.clip((r - d) / soft + 0.5, 0, 1)


def rectmask(r, pad=0):
    m = np.zeros((H, W), np.float32)
    m[int(r[1] - pad):int(r[3] + pad), int(r[0] - pad):int(r[2] + pad)] = 1
    return m


KNOBS, SELS, SCREENS = list(knobs()), list(selectors()), list(screens())
M_KNOB = np.zeros((H, W), np.float32)
for k in KNOBS:
    M_KNOB = np.maximum(M_KNOB, disc(k['cx'], k['cy'], k['body'] + 1))
for f in L['faders']:
    M_KNOB = np.maximum(M_KNOB, rectmask([f['x0'] * S, f['y0'] * S, f['x1'] * S, f['y1'] * S], 1))
M_SEL = np.zeros((H, W), np.float32)
for s in SELS:
    M_SEL = np.maximum(M_SEL, disc(s['cx'], s['cy'], s['r'] + 7))
M_SCREEN = np.zeros((H, W), np.float32)
for r in SCREENS:
    M_SCREEN = np.maximum(M_SCREEN, rectmask(r, 0))
M_BEZEL = np.zeros((H, W), np.float32)
for r in SCREENS:
    M_BEZEL = np.maximum(M_BEZEL, rectmask(r, 7))
M_PANEL = np.clip(1 - np.maximum(np.maximum(M_KNOB, M_SEL), M_BEZEL), 0, 1)


# ---------------------------------------------------------------------------
# noise helpers
# ---------------------------------------------------------------------------
def noise(rng, sigma, octaves=1):
    acc = np.zeros((H, W), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        n = ndi.gaussian_filter(rng.standard_normal((H, W)).astype(np.float32), sigma / (2 ** o))
        n /= (n.std() + 1e-6)
        acc += amp * n
        tot += amp
        amp *= 0.5
    return acc / tot


def norm01(a):
    lo, hi = np.percentile(a, 1), np.percentile(a, 99)
    return np.clip((a - lo) / (hi - lo + 1e-6), 0, 1)


def over(img, colour, alpha):
    """composite a flat colour with a per-pixel alpha"""
    a = np.clip(alpha, 0, 1)[..., None]
    return img * (1 - a) + np.asarray(colour, np.float32)[None, None, :] / 255.0 * a


def luminance(img):
    return img[..., 0] * 0.299 + img[..., 1] * 0.587 + img[..., 2] * 0.114


# ---------------------------------------------------------------------------
# strokes drawn on a high-res mask
# ---------------------------------------------------------------------------
def stroke_layer(paths):
    """paths: list of (points, width, alpha). Returns float mask HxW."""
    ss = 2
    im = Image.new('L', (W * ss, H * ss), 0)
    d = ImageDraw.Draw(im)
    for pts, width, alpha in paths:
        p = [(x * ss, y * ss) for x, y in pts]
        n = len(p)
        for i in range(n - 1):
            # taper the ends
            t = i / max(1, n - 2)
            taper = math.sin(math.pi * min(1, max(0, t))) ** 0.5
            a = int(255 * alpha * (0.35 + 0.65 * taper))
            w = max(1, int(round(width * ss * (0.4 + 0.6 * taper))))
            d.line([p[i], p[i + 1]], fill=a, width=w)
    im = im.resize((W, H), Image.LANCZOS)
    return np.asarray(im).astype(np.float32) / 255.0


def scratch_path(rng, x, y, ang, length, bend=0.0, jitter=0.15, step=6):
    pts = [(x, y)]
    a = ang
    for _ in range(int(length / step)):
        a += bend + rng.normal(0, jitter) * 0.05
        x += math.cos(a) * step
        y += math.sin(a) * step
        pts.append((x, y))
    return pts


def scratch_cluster(rng, n, cx, cy, spread, ang, len_rng, width_rng, alpha_rng, bend=0.002):
    out = []
    for _ in range(n):
        x = cx + rng.normal(0, spread)
        y = cy + rng.normal(0, spread * 0.6)
        a = ang + rng.normal(0, 0.08)
        out.append((scratch_path(rng, x, y, a, rng.uniform(*len_rng), bend=rng.normal(0, bend)),
                    rng.uniform(*width_rng), rng.uniform(*alpha_rng)))
    return out


def turn_scuffs(rng, k, n, alpha_rng, rmax_frac=0.95):
    """concentric arcs on a knob top: the marks fingers leave turning it"""
    out = []
    for _ in range(n):
        rr = k['cap'] * rng.uniform(0.25, rmax_frac)
        a0 = rng.uniform(0, 2 * math.pi)
        span = rng.uniform(0.3, 1.6)
        pts = [(k['cx'] + rr * math.cos(a0 + span * t / 12), k['cy'] + rr * math.sin(a0 + span * t / 12))
               for t in range(13)]
        out.append((pts, rng.uniform(0.6, 1.3), rng.uniform(*alpha_rng)))
    return out


# ---------------------------------------------------------------------------
# relief: dents and grooves lit from the top left
# ---------------------------------------------------------------------------
def shade_from_height(Hm, strength):
    gy, gx = np.gradient(Hm)
    lx, ly, lz = -0.55, -0.65, 0.52
    n = np.sqrt(gx * gx + gy * gy + 1)
    lit = (-gx * lx - gy * ly + lz) / n - lz
    return lit * strength


def dent(Hm, cx, cy, r, depth, elong=1.0, ang=0.0):
    ca, sa = math.cos(ang), math.sin(ang)
    x0, x1 = int(max(0, cx - r * 3 * elong)), int(min(W, cx + r * 3 * elong))
    y0, y1 = int(max(0, cy - r * 3 * elong)), int(min(H, cy + r * 3 * elong))
    X, Y = XX[y0:y1, x0:x1] - cx, YY[y0:y1, x0:x1] - cy
    u = (X * ca + Y * sa) / elong
    v = -X * sa + Y * ca
    Hm[y0:y1, x0:x1] -= depth * np.exp(-(u * u + v * v) / (2 * r * r))


# ---------------------------------------------------------------------------
# dust
# ---------------------------------------------------------------------------
def specks(rng, n, region, rmin, rmax, alpha_rng, colour, fibres=0):
    im = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(im)
    ys, xs = np.nonzero(region > 0.5)
    idx = rng.integers(0, len(xs), n)
    for i in idx:
        x, y = xs[i], ys[i]
        r = rng.uniform(rmin, rmax) ** 1.0
        d.ellipse([x - r, y - r, x + r, y + r], fill=int(255 * rng.uniform(*alpha_rng)))
    for _ in range(fibres):
        i = rng.integers(0, len(xs))
        pts = scratch_path(rng, xs[i], ys[i], rng.uniform(0, 6.28), rng.uniform(20, 70), bend=rng.normal(0, 0.12), step=3)
        d.line(pts, fill=int(255 * rng.uniform(0.25, 0.5)), width=1)
    m = np.asarray(im.filter(ImageFilter.GaussianBlur(0.6))).astype(np.float32) / 255.0
    return m


def knob_base_ring():
    """dust gathers in a ring round each knob's foot and in the knurling"""
    m = np.zeros((H, W), np.float32)
    for k in KNOBS:
        d = np.hypot(XX - k['cx'], YY - k['cy'])
        m = np.maximum(m, np.exp(-((d - k['body'] - 3) / 5) ** 2))
    return m


def knurl_ring():
    m = np.zeros((H, W), np.float32)
    for k in KNOBS:
        d = np.hypot(XX - k['cx'], YY - k['cy'])
        m = np.maximum(m, ((d > k['cap'] + 1) & (d < k['body'])).astype(np.float32))
    for s in SELS:
        d = np.hypot(XX - s['cx'], YY - s['cy'])
        m = np.maximum(m, np.exp(-((d - s['r'] * 0.8) / 3) ** 2))
    return m


def edge_accumulation():
    """dust drifts into corners and along box edges: distance to bright strokes"""
    lum = luminance(BASE)
    lines = (lum > 0.55) & (M_PANEL > 0.5)
    dist = ndi.distance_transform_edt(~lines)
    return np.exp(-dist / 10.0) * (M_PANEL)


def fingerprint(cx, cy, rx, ry, ang, rng):
    ca, sa = math.cos(ang), math.sin(ang)
    X, Y = XX - cx, YY - cy
    u = (X * ca + Y * sa) / rx
    v = (-X * sa + Y * ca) / ry
    rr = np.sqrt(u * u + v * v)
    warp = noise(rng, 30) * 0.05
    ridges = 0.5 + 0.5 * np.sin((rr + warp) * 2 * math.pi * 14)
    env = np.exp(-(rr ** 2) * 2.2)
    return ridges * env


def lettering_mask():
    lum = luminance(BASE)
    sat = BASE.max(2) - BASE.min(2)
    return np.clip((lum - 0.45) / 0.3, 0, 1) * (sat < 0.12) * M_PANEL


def save(img, name):
    im = Image.fromarray((np.clip(img, 0, 1) * 255 + 0.5).astype(np.uint8))
    im.save(os.path.join(out, name), optimize=True)
    print('wrote', name)
    return im



# ===========================================================================
# The layer: premultiplied colour P and coverage A, composited with the same
# "over" the picture version uses. Additive sheen becomes white at that alpha.
# ===========================================================================
P = np.zeros((H, W, 3), np.float32)
A = np.zeros((H, W), np.float32)


def lover(colour, alpha):
    global P, A
    a = np.clip(alpha, 0, 1)
    c = np.asarray(colour, np.float32) / 255.0
    P = P * (1 - a)[..., None] + c[None, None, :] * a[..., None]
    A = A * (1 - a) + a


def nearly_new_layer():
    """A few months in a home studio: faint haze, a handful of hairlines,
    a little dust on the glass. Barely there (Designs/README, mode 0)."""
    rng = np.random.default_rng(14)
    haze = norm01(noise(rng, 90, 3)) * 0.6 + norm01(noise(rng, 6, 2)) * 0.4
    dust = (haze * 0.025 + edge_accumulation() * 0.02 + knob_base_ring() * 0.035) * M_PANEL
    lover((150, 146, 138), dust)
    paths = []
    for _ in range(2):
        paths += scratch_cluster(rng, int(rng.integers(2, 4)), rng.uniform(0, W), rng.uniform(0, H),
                                 50, rng.normal(0.15, 0.4), (30, 120), (0.45, 0.7), (0.08, 0.18))
    for _ in range(8):
        paths.append((scratch_path(rng, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0, 6.28),
                                   rng.uniform(15, 60), bend=rng.normal(0, 0.01)), 0.5, rng.uniform(0.06, 0.15)))
    sc = stroke_layer(paths) * (1 - M_SCREEN)
    lover((120, 120, 116), sc * 0.8)
    # knobs turn under this layer, so no turn scuffs - only a faint polish
    sheen = np.zeros((H, W), np.float32)
    for k in KNOBS:
        sheen = np.maximum(sheen, disc(k['cx'] - k['cap'] * 0.25, k['cy'] - k['cap'] * 0.3, k['cap'] * 0.55, 8) * 0.025)
    lover((255, 255, 255), sheen)
    sd = specks(rng, 90, M_SCREEN, 0.4, 1.0, (0.08, 0.25), None, fibres=1)
    lover((170, 170, 168), sd * M_SCREEN)
    pd = specks(rng, 450, M_PANEL + M_KNOB, 0.3, 1.0, (0.08, 0.25), None, fibres=3)
    lover((175, 170, 160), pd)
    # straight (un-premultiplied) RGBA
    a = np.clip(A, 0, 1)
    rgb = np.where(a[..., None] > 1e-4, P / np.maximum(a[..., None], 1e-4), 0)
    rgba = np.dstack([np.clip(rgb, 0, 1), a])
    Image.fromarray((rgba * 255 + 0.5).astype(np.uint8), 'RGBA').save(os.path.join(out, 'pv_wear.png'), optimize=True)
    # and a preview: the layer over the reference picture
    prev = BASE * (1 - a[..., None]) + np.clip(rgb, 0, 1) * a[..., None]
    Image.fromarray((np.clip(prev, 0, 1) * 255 + 0.5).astype(np.uint8)).save(os.path.join(out, 'wear-preview.png'))
    print('wrote pv_wear.png')


if __name__ == '__main__':
    os.makedirs(out, exist_ok=True)
    nearly_new_layer()
