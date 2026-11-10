#!/usr/bin/env python3
"""Weathered ForTran panel (ported from R2Synth's render-weathered.py) mock-ups: dust, scratches, dents on the panel,
knobs, selectors and screen glass. Post-processes docs/panel.png (2x) using
the plug-in's own layout dump for where things are.

usage: weather.py panel.png layout.json outdir <0|1|2|3> name.png  (0 = nearly new)
"""
import json, math, sys, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage as ndi

S = 2  # layout units -> pixels

src, layout, out, MODE, OUTNAME = sys.argv[1:6]
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
# 0  NEARLY NEW  - a few months in a home studio: faint haze, a handful of
#    hairlines, a little dust on the glass. Barely there.
# ===========================================================================
def nearly_new():
    rng = np.random.default_rng(int(os.environ.get('SEED', '5')))
    img = BASE.copy()
    haze = norm01(noise(rng, 90, 3)) * 0.6 + norm01(noise(rng, 6, 2)) * 0.4
    dust = (haze * 0.025 + edge_accumulation() * 0.02 + knob_base_ring() * 0.035) * M_PANEL
    img = over(img, (150, 146, 138), dust)
    paths = []
    for _ in range(2):
        paths += scratch_cluster(rng, int(rng.integers(2, 4)), rng.uniform(0, W), rng.uniform(0, H),
                                 50, rng.normal(0.15, 0.4), (30, 120), (0.45, 0.7), (0.08, 0.18))
    for _ in range(8):
        paths.append((scratch_path(rng, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0, 6.28),
                                   rng.uniform(15, 60), bend=rng.normal(0, 0.01)), 0.5, rng.uniform(0.06, 0.15)))
    sc = stroke_layer(paths) * (1 - M_SCREEN)
    img = over(img, (120, 120, 116), sc * 0.8)
    kp = []
    for k in KNOBS:
        kp += turn_scuffs(rng, k, int(rng.integers(1, 4)), (0.05, 0.14))
    img = over(img, (150, 150, 148), stroke_layer(kp) * M_KNOB) if kp else img
    sheen = np.zeros((H, W), np.float32)
    for k in KNOBS:
        sheen = np.maximum(sheen, disc(k['cx'] - k['cap'] * 0.25, k['cy'] - k['cap'] * 0.3, k['cap'] * 0.55, 8) * 0.025)
    img = img + sheen[..., None]
    sd = specks(rng, 90, M_SCREEN, 0.4, 1.0, (0.08, 0.25), None, fibres=1)
    img = over(img, (170, 170, 168), sd * M_SCREEN)
    pd = specks(rng, 450, M_PANEL + M_KNOB, 0.3, 1.0, (0.08, 0.25), None, fibres=3)
    img = over(img, (175, 170, 160), pd)
    return save(img, OUTNAME)



# ===========================================================================
# 1  STUDIO WORN  - a decade on a desk: fine haze, hairlines, polished knobs
# ===========================================================================
def studio_worn():
    rng = np.random.default_rng(11)
    img = BASE.copy()
    # haze: patchy, a little heavier where hands don't reach (top + right)
    haze = norm01(noise(rng, 90, 3)) * 0.55 + norm01(noise(rng, 6, 2)) * 0.45
    reach = np.clip(0.5 + (XX / W) * 0.3 - (YY / H) * 0.25, 0, 1)
    dust = (haze * 0.07 * reach + edge_accumulation() * 0.06 + knob_base_ring() * 0.10) * M_PANEL
    img = over(img, (150, 146, 138), dust)
    # hairline scratches, mostly from things slid across the top
    paths = []
    for _ in range(9):
        paths += scratch_cluster(rng, int(rng.integers(3, 9)), rng.uniform(0, W), rng.uniform(0, H),
                                 60, rng.normal(0.15, 0.4), (40, 220), (0.5, 0.9), (0.15, 0.4))
    for _ in range(40):
        paths.append((scratch_path(rng, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(0, 6.28),
                                   rng.uniform(15, 90), bend=rng.normal(0, 0.01)), 0.6, rng.uniform(0.1, 0.3)))
    sc = stroke_layer(paths) * (1 - M_SCREEN)
    img = over(img, (120, 120, 116), sc * 0.9)
    # knobs: tops polished by fingers (soft sheen) + turn scuffs; knurl dusty
    kp = []
    for k in KNOBS:
        kp += turn_scuffs(rng, k, int(rng.integers(8, 18)), (0.12, 0.35))
    ks = stroke_layer(kp)
    img = over(img, (150, 150, 148), ks * M_KNOB)
    sheen = np.zeros((H, W), np.float32)
    for k in KNOBS:
        sheen = np.maximum(sheen, disc(k['cx'] - k['cap'] * 0.25, k['cy'] - k['cap'] * 0.3, k['cap'] * 0.55, 8) * 0.06)
    img = img + sheen[..., None]
    knurl = knurl_ring() * M_KNOB * (0.18 + 0.15 * norm01(noise(rng, 2)))
    img = over(img, (140, 136, 128), knurl)
    # screens: fine dust on the glass and one smudge
    sd = specks(rng, 260, M_SCREEN, 0.4, 1.3, (0.15, 0.5), None, fibres=4)
    img = over(img, (170, 170, 168), sd * M_SCREEN)
    fp = sum(fingerprint((r[0] + r[2]) / 2 + rng.normal(0, 40), (r[1] + r[3]) / 2, 26, 34, rng.uniform(-0.5, 0.5), rng) * 0.06 for r in SCREENS[1:4])
    img = over(img, (200, 200, 205), fp * M_SCREEN)
    # panel specks
    pd = specks(rng, 1400, M_PANEL + M_KNOB, 0.3, 1.2, (0.15, 0.45), None, fibres=10)
    img = over(img, (175, 170, 160), pd)
    return save(img, OUTNAME)


# ===========================================================================
# 2  ROAD CASE  - twenty years of gigs: gouges to bare metal, dents, chips
# ===========================================================================
def road_case():
    rng = np.random.default_rng(23)
    img = BASE.copy()
    # relief: dents on the panel (a few hard knocks, many small dings)
    Hm = np.zeros((H, W), np.float32)
    for cx, cy, r, d in [(1180, 1210, 26, 9), (2290, 640, 20, 7), (95, 1180, 16, 6), (1460, 880, 14, 5),
                         (760, 120, 18, 6), (2050, 1150, 22, 8)]:
        dent(Hm, cx, cy, r, d, elong=rng.uniform(1, 1.8), ang=rng.uniform(0, 3))
    for _ in range(45):
        dent(Hm, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(3, 8), rng.uniform(1, 3))
    # grooves for the deep scratches
    deep = []
    for _ in range(7):
        deep += scratch_cluster(rng, int(rng.integers(1, 4)), rng.uniform(100, W - 100), rng.uniform(150, H - 80),
                                25, rng.uniform(-0.6, 0.6), (120, 520), (1.6, 2.6), (0.7, 1.0), bend=0.004)
    groove = stroke_layer(deep)
    Hm -= ndi.gaussian_filter(groove, 1.2) * 4
    lit = shade_from_height(Hm, 1.0)
    panel_relief = M_PANEL + M_BEZEL * (1 - M_SCREEN)
    img = img + (np.clip(lit, -0.5, 0.5) * 0.55 * panel_relief)[..., None] * np.array([1, 1, 1.02])
    # deep scratches: bright aluminium core, dark burr
    core = stroke_layer([(p, w * 0.45, a) for p, w, a in deep])
    img = over(img, (40, 40, 40), np.clip(groove - core, 0, 1) * 0.6 * (1 - M_SCREEN) * (1 - M_KNOB) * (1 - M_SEL))
    raised = (1 - M_SCREEN) * (1 - M_KNOB) * (1 - M_SEL)
    img = over(img, (196, 198, 196), core * raised * (0.7 + 0.3 * norm01(noise(rng, 3))))
    # lighter scuffs everywhere, and swirl marks
    paths = []
    for _ in range(18):
        paths += scratch_cluster(rng, int(rng.integers(4, 14)), rng.uniform(0, W), rng.uniform(0, H),
                                 80, rng.uniform(-3, 3), (30, 260), (0.5, 1.1), (0.2, 0.55))
    sc = stroke_layer(paths)
    img = over(img, (125, 125, 122), sc * (1 - M_SCREEN) * 0.9)
    # chipped paint: along the bottom edge, round screws, at box corners
    chips = np.zeros((H, W), np.float32)
    rough = noise(rng, 3, 2)
    seeds = [(16, 16), (2384, 16), (16, 1264), (2384, 1264)]
    seeds += [(rng.uniform(0, W), rng.uniform(1250, 1280)) for _ in range(10)]
    seeds += [(rng.uniform(0, 20), rng.uniform(0, H)) for _ in range(5)]
    seeds += [(rng.uniform(2380, 2400), rng.uniform(0, H)) for _ in range(5)]
    for s in L['sections']:
        for x, y in [(s[0], s[1]), (s[2], s[3]), (s[0], s[3])]:
            if rng.random() < 0.6:
                seeds.append((x * S + rng.normal(0, 4), y * S + rng.normal(0, 4)))
    for x, y in seeds:
        r = rng.uniform(3, 9)
        d = np.hypot(XX - x, YY - y) * rng.uniform(0.8, 1.3) + rough * 3.5
        chips = np.maximum(chips, np.clip((r - d) / 1.2, 0, 1))
    chip_edge = np.clip(ndi.gaussian_filter(chips, 1.5) - chips, 0, 1)
    metal = 0.36 + 0.14 * norm01(noise(rng, 1.5))
    img = over(img, (0, 0, 0), chip_edge * 1.2)
    img = img * (1 - chips[..., None]) + (metal[..., None] * np.array([0.98, 0.99, 1.0])) * chips[..., None]
    # worn legends: patchy loss where hands go (the lower half most)
    let = lettering_mask()
    wear = np.clip(norm01(noise(rng, 5, 2)) * 1.6 - 0.75, 0, 1) * (0.4 + 0.6 * YY / H)
    img = img - (let * wear * 0.6)[..., None] * img
    # knobs: knurl gouges, nicks out of the rim, pointer paint chipped
    kp = []
    for k in KNOBS:
        kp += turn_scuffs(rng, k, int(rng.integers(14, 26)), (0.25, 0.6))
        for _ in range(int(rng.integers(1, 4))):  # straight gouges across the top
            a = rng.uniform(0, 6.28)
            x = k['cx'] + rng.normal(0, k['cap'] * 0.3)
            y = k['cy'] + rng.normal(0, k['cap'] * 0.3)
            kp.append((scratch_path(rng, x, y, a, rng.uniform(0.5, 1.2) * k['cap'], step=3), rng.uniform(0.8, 1.5),
                       rng.uniform(0.35, 0.7)))
    ks = stroke_layer(kp)
    img = over(img, (160, 160, 158), ks * M_KNOB)
    nicks = np.zeros((H, W), np.float32)
    for k in KNOBS:
        for _ in range(int(rng.integers(0, 3))):
            a = rng.uniform(0, 6.28)
            x, y = k['cx'] + math.cos(a) * k['body'], k['cy'] + math.sin(a) * k['body']
            nicks = np.maximum(nicks, disc(x, y, rng.uniform(2.5, 5), 1))
    nicks *= M_KNOB
    img = over(img, (70, 70, 68), nicks * 0.9)
    # pointer paint: bright pixels on knob tops lose patches
    ptr = np.clip((luminance(BASE) - 0.6) / 0.2, 0, 1) * (M_KNOB + M_SEL).clip(0, 1)
    pw = np.clip(norm01(noise(rng, 2.5)) * 2 - 1.05, 0, 1)
    img = img - (ptr * pw * 0.75)[..., None] * img
    # screen glass: scratches catch the light, one star crack on the amp OLED
    gp = []
    for r in SCREENS:
        for _ in range(int(rng.integers(3, 8))):
            gp.append((scratch_path(rng, rng.uniform(r[0], r[2]), rng.uniform(r[1], r[3]), rng.uniform(-0.6, 0.6),
                                    rng.uniform(30, 200)), rng.uniform(0.5, 1.0), rng.uniform(0.25, 0.6)))
    cx, cy = CRACK
    for _ in range(9):
        a = rng.uniform(0, 6.28)
        gp.append((scratch_path(rng, cx, cy, a, rng.uniform(20, 110), bend=rng.normal(0, 0.03), jitter=0.6, step=4),
                   1.0, 0.8))
    gl = stroke_layer(gp) * M_SCREEN
    img = over(img, (215, 220, 225), gl * 0.75)
    img = over(img, (230, 235, 240), disc(cx, cy, 4, 2) * 0.6)
    # grime over it all, lighter than the barn find
    dust = (norm01(noise(rng, 70, 3)) * 0.06 + edge_accumulation() * 0.05 + knob_base_ring() * 0.12) * M_PANEL
    img = over(img, (130, 124, 112), dust)
    img = over(img, (120, 115, 105), knurl_ring() * M_KNOB * 0.25)
    pd = specks(rng, 1800, np.ones((H, W)), 0.3, 1.4, (0.15, 0.5), None, fibres=8)
    img = over(img, (165, 160, 150), pd)
    return save(img, OUTNAME)


# ===========================================================================
# 3  BARN FIND  - years in a loft: a coat of dust, wiped where touched,
#    yellowed legends, rust at the screws, a mug ring
# ===========================================================================
def barn_find():
    rng = np.random.default_rng(37)
    img = BASE.copy()
    # yellowed silkscreen first
    let = lettering_mask()
    tint = np.array([0.93, 0.86, 0.66])
    img = img * (1 - let[..., None] * 0.55) + (img * tint) * (let[..., None] * 0.55)
    # a coat of dust; finger wipes clear it on the knobs most used, and a
    # sleeve has dragged a band across the filter section
    coat = 0.20 + 0.10 * norm01(noise(rng, 120, 3)) + 0.06 * norm01(noise(rng, 4, 2))
    coat += edge_accumulation() * 0.10 + knob_base_ring() * 0.18
    wipe = np.zeros((H, W), np.float32)
    used = {'CUTOFF', 'Q', 'LEVEL', 'RATE', 'RELEASE', 'R'}
    for k in KNOBS:
        if k['label'] in used:
            wipe = np.maximum(wipe, disc(k['cx'], k['cy'], k['cap'] * 0.95, 4) * 0.85)
    # sleeve drag: a soft diagonal band with streaks along it
    t = (XX - SLEEVE[0]) * 0.28 + (YY - SLEEVE[1])
    band = np.exp(-(t / 70) ** 2) * ((XX > SLEEVE[0] - 240) & (XX < SLEEVE[0] + 700)).astype(np.float32)
    band = ndi.gaussian_filter(band, 20)
    streak = norm01(ndi.gaussian_filter(rng.standard_normal((H, W)).astype(np.float32), (1, 40)))
    rot = ndi.rotate(streak, -15, reshape=False, order=1, mode='reflect')
    wipe = np.maximum(wipe, band * (0.55 + 0.4 * rot))
    # a finger swipe across the amp screen's glass
    t2 = (YY - SWIPE[1]) - (XX - SWIPE[0]) * 0.12
    sw = np.exp(-(t2 / 26) ** 2) * ((XX > SWIPE[0] - 110) & (XX < SWIPE[0] + 370)).astype(np.float32)
    wipe = np.maximum(wipe, ndi.gaussian_filter(sw, 10) * (0.35 + 0.45 * rot))
    coat = coat * (1 - np.clip(wipe, 0, 1))
    img = over(img, (148, 140, 124), coat * (M_PANEL + M_BEZEL * (1 - M_SCREEN)))
    img = over(img, (150, 144, 130), coat * 1.25 * M_SCREEN)
    img = over(img, (140, 132, 116), coat * 0.9 * (M_KNOB + M_SEL).clip(0, 1))
    img = over(img, (125, 116, 98), knurl_ring() * (0.4 + 0.2 * norm01(noise(rng, 2))) * M_KNOB)
    # mug ring on the modulation section
    for cx, cy, r, a in [(MUG[0], MUG[1], 92, 0.45), (MUG[0] + 30, MUG[1] - 35, 88, 0.18)]:
        d = np.hypot(XX - cx, YY - cy) + noise(rng, 12) * 3
        ring = np.exp(-((d - r) / 2.5) ** 2) + 0.18 * (d < r) * norm01(noise(rng, 20))
        img = over(img, (96, 70, 38), ring * a)
    # rust blooms at the screws and a little along the bottom edge
    rust = np.zeros((H, W), np.float32)
    rn = norm01(noise(rng, 2.5, 2))
    for x, y in L['screws']:
        d = np.hypot(XX - x * S, YY - y * S)
        rust = np.maximum(rust, np.exp(-(d / 12) ** 2) * (0.5 + rn))
    rust = np.clip(rust, 0, 1)
    img = over(img, (128, 62, 26), rust * 0.85)
    # cobweb wisp in the top-right corner
    wp = []
    for i in range(7):
        a = math.pi / 2 + i * (math.pi / 2) / 6
        wp.append(([(2398, 2), (2398 + math.cos(a) * 130, 2 + math.sin(a) * 130)], 0.6, 0.35))
    for j in range(1, 6):
        rr = j * 22
        wp.append(([(2398 + math.cos(math.pi / 2 + i * math.pi / 12) * rr * (1 + 0.05 * math.sin(i * 3)),
                     2 + math.sin(math.pi / 2 + i * math.pi / 12) * rr) for i in range(7)], 0.5, 0.25))
    img = over(img, (200, 196, 188), stroke_layer(wp))
    # scratches: fewer and softer than the road case, half-hidden by dust
    paths = []
    for _ in range(10):
        paths += scratch_cluster(rng, int(rng.integers(2, 7)), rng.uniform(0, W), rng.uniform(0, H),
                                 60, rng.uniform(-3, 3), (40, 200), (0.5, 1.0), (0.15, 0.4))
    img = over(img, (110, 106, 98), stroke_layer(paths) * (1 - M_SCREEN))
    Hm = np.zeros((H, W), np.float32)
    for _ in range(14):
        dent(Hm, rng.uniform(0, W), rng.uniform(0, H), rng.uniform(4, 12), rng.uniform(1.5, 4))
    img = img + (np.clip(shade_from_height(Hm, 1.0), -0.4, 0.4) * 0.45 * M_PANEL)[..., None]
    # specks, grit and hairs
    pd = specks(rng, 4200, np.ones((H, W)) * (1 - np.clip(wipe, 0, 1) * 0.7), 0.3, 1.8, (0.2, 0.6), None, fibres=26)
    img = over(img, (178, 170, 152), pd)
    grit = specks(rng, 300, np.ones((H, W)), 0.8, 2.2, (0.4, 0.8), None)
    img = over(img, (70, 62, 50), grit * 0.8)
    return save(img, OUTNAME)


if __name__ == '__main__':
    os.makedirs(out, exist_ok=True)
    sc = SCREENS
    CRACK = (sc[-1][0] + (sc[-1][2] - sc[-1][0]) * 0.7, sc[-1][1] + (sc[-1][3] - sc[-1][1]) * 0.35)
    SLEEVE = json.loads(os.environ.get('SLEEVE', '[1000, 600]'))
    SWIPE = json.loads(os.environ.get('SWIPE', '[1900, 330]'))
    MUG = json.loads(os.environ.get('MUG', '[1530, 1095]'))
    {'0': nearly_new, '1': studio_worn, '2': road_case, '3': barn_find}[MODE]()
