#!/usr/bin/env python3
"""Turns the art rendered from the browser game (res/*.png, res/art.json) into Mega Drive data: src/assets.c and src/assets.h.

Palettes (each 16 colours, index 0 is transparent):
  PAL0-PAL2  the kitchen and the toaster, packed so every 8x8 tile uses a single palette.
  PAL3       the HUD, text and toast: 1-8 the doneness bands, 9 label, 10 bracket yellow,
             11 the big word's colour (set per word), 12 white, 13 navy,
             14-15 the toast in the slots (body, crust), set every frame as it browns.
"""
import json, os, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, '..', 'res')
SRC = os.path.join(HERE, '..', 'src')
W, H = 320, 224
SCENE_H = 208
NAVY = (0, 0, 36)

art = json.load(open(os.path.join(RES, 'art.json')))
g = art['geom']
OY = g['oy']

def load(name):
    return Image.open(os.path.join(RES, name)).convert('RGBA')

kitchen, t_up, t_down = load('kitchen.png'), load('toasterUp.png'), load('toasterDown.png')

# ---------- the toaster without its lever knob (the knob is a sprite) ----------
def clean_toaster(up, down, mg):
    """The toaster with its lever knob painted out, using where the up and down renders differ."""
    tx_, top_, tw_, th_ = mg['x'], mg['top'] + OY, mg['w'], mg['h']
    lx_ = tx_ + tw_ - 9
    out = up.copy()
    pu, pd, po = up.load(), down.load(), out.load()
    for y in range(SCENE_H):
        for x in range(W):
            if pu[x, y] != pd[x, y]:
                po[x, y] = pu[x, top_ + 7] if pu[x, top_ + 7][3] else (0, 0, 0, 0)
    for y in range(top_ + 9, top_ + th_ - 2 - 6):  # the lever's channel
        for x in (lx_, lx_ + 1):
            po[x, y] = NAVY + (255,)
    return out

def front_of(toaster_img, mg):
    """Plane A's copy: the toaster with its slots open above the slot line, so the toast shows."""
    out = toaster_img.copy()
    fp = out.load()
    top_, slot_y_ = mg['top'] + OY, mg['slotY'] + OY
    for s_ in mg['slots']:
        x0 = round(s_ - 7)
        for y in range(top_ - 1, slot_y_ + 2):
            for x in range(x0, x0 + 14):
                fp[x, y] = (0, 0, 0, 0)
    return out

tx, ttop, tw, th = g['x'], g['top'] + OY, g['w'], g['h']
lx = tx + tw - 9
toaster = clean_toaster(t_up, t_down, g)

# ---------- plane B: the whole scene, opaque ----------
scene = Image.new('RGBA', (W, H), NAVY + (255,))
scene.alpha_composite(kitchen)
scene.alpha_composite(toaster)

# ---------- plane A: the toaster's front, in front of the toast ----------
front = front_of(toaster, g)
slot_y = g['slotY'] + OY

# ---------- palettes ----------
def tile_colours(img, tx_, ty_):
    p = img.load()
    return frozenset(p[tx_ * 8 + i, ty_ * 8 + j][:3] for j in range(8) for i in range(8) if p[tx_ * 8 + i, ty_ * 8 + j][3])

KNOB = [(36, 36, 36), (109, 109, 109), (0, 0, 0)]
CAPS = [15, 15, 15]
WALL = {(219, 219, 182), (182, 182, 146), (255, 255, 255)}

def pack(merge):
    def m(c):
        while c in merge: c = merge[c]
        return c
    sets = []
    for img in (scene, front):
        for ty in range((SCENE_H if img is front else H) // 8):
            for tx_ in range(W // 8):
                s = frozenset(m(c) for c in tile_colours(img, tx_, ty))
                if s: sets.append(s)
    sets.append(frozenset(m(c) for c in KNOB))
    uniq = sorted(set(sets), key=len, reverse=True)
    uniq = [s for s in uniq if not any(s < t for t in uniq)]  # sets inside bigger ones fit for free
    # Depth-first search: put each tile's colours in a palette, backtracking when one overflows.
    pals = [set() for _ in CAPS]
    budget = [200000]
    def dfs(k):
        if k == len(uniq): return True
        budget[0] -= 1
        if budget[0] < 0: return False
        s, tried = uniq[k], set()
        for i in sorted(range(len(pals)), key=lambda i: len(pals[i] | s) - len(pals[i])):
            key = frozenset(pals[i])
            if key in tried or len(pals[i] | s) > CAPS[i]: continue
            tried.add(key)
            old = set(pals[i]); pals[i] |= s
            if dfs(k + 1): return True
            pals[i] = old
        return False
    return pals if dfs(0) else None

# Colour merges chosen by eye, the way an artist would tidy art for the palettes.
H_ = lambda h: tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
merge = {H_(a): H_(b) for a, b in [
    ('000000', '000024'), ('242424', '000024'), ('24246d', '000024'),  # blacks and the HUD line into navy
    ('dbdbff', 'dbdbdb'), ('9292b6', '929292'), ('b6b6b6', 'dbdbdb'),  # plate and smoke alarm greys
    ('db2424', 'b64924'), ('6ddb6d', '499249'), ('49b692', '6ddbb6'),  # clock hand, leaf and lamp highlights
    ('246d49', '006d49'), ('ffffdb', 'ffffff'),                        # lamp shade, loaf's cut face
]}
pals = pack(merge)
counts = {}
for c in scene.get_flattened_data(): counts[c[:3]] = counts.get(c[:3], 0) + 1
while pals is None:  # merge the rarest colour into its nearest neighbour until it fits
    live = [c for c in counts if c not in merge]
    dist = lambda c, d: sum((a - b) ** 2 for a, b in zip(c, d))
    c, near = min(((c, min((d for d in live if d != c), key=lambda d: dist(c, d))) for c in live),
                  key=lambda cn: counts[cn[0]] * dist(*cn))  # the merge that changes the fewest pixels the least
    merge[c] = near
    print('merged', c, '->', near, file=sys.stderr)
    pals = pack(merge)

def mc(c):
    while c in merge: c = merge[c]
    return c

pal_lists = [sorted(p) for p in pals]
def md_colour(c):
    r, gg, b = (round(v / 255 * 7) for v in c)
    return (b << 9) | (gg << 5) | (r << 1)

# ---------- tiles ----------
tiles, tile_index = [], {}
def add_tile(pix):  # pix: 64 palette indices
    key = tuple(pix)
    if key not in tile_index:
        tile_index[key] = len(tiles)
        tiles.append(key)
    return tile_index[key]
add_tile([0] * 64)  # tile 0: empty

def img_tile(img, tx_, ty_):
    cols = tile_colours(img, tx_, ty_)
    if not cols: return 0, 0
    cols = frozenset(mc(c) for c in cols)
    pi = next(i for i, p in enumerate(pals) if cols <= p)
    p = img.load()
    pix = []
    for j in range(8):
        for i in range(8):
            q = p[tx_ * 8 + i, ty_ * 8 + j]
            pix.append(pal_lists[pi].index(mc(q[:3])) + 1 if q[3] else 0)
    return add_tile(pix), pi

def plane(img, rows):
    out = []
    for ty in range(28):
        for tx_ in range(40):
            if ty >= rows: out.append(0); continue
            t, pi = img_tile(img, tx_, ty)
            out.append((pi << 13) | t if t else 0)
    return out

plane_b = plane(scene, 28)
plane_a = [(e | 0x8000) if e else 0 for e in plane(front, SCENE_H // 8)]  # high priority
knob_pal = next(i for i, p in enumerate(pals) if set(mc(c) for c in KNOB) <= p)

# ---------- every toaster model, as a patch of tiles over the kitchen ----------
# Only the kitchen and Nan's toaster were packed into the palettes; the others use each tile's best-fitting palette.
PATCH_X, PATCH_Y, PATCH_W, PATCH_H = 14, 13, 10, 6
dist = lambda c, d: sum((a - b) ** 2 for a, b in zip(c, d))

def nearest_tile(img, tx_, ty_):
    p = img.load()
    pix = [p[tx_ * 8 + i, ty_ * 8 + j] for j in range(8) for i in range(8)]
    if not any(q[3] for q in pix): return 0
    best = None
    for pi, pl in enumerate(pal_lists):
        err, idx = 0, []
        for q in pix:
            if not q[3]: idx.append(0); continue
            c = mc(q[:3])
            k = min(range(len(pl)), key=lambda k: dist(pl[k], c))
            err += dist(pl[k], c); idx.append(k + 1)
        if best is None or err < best[0]: best = (err, pi, idx)
    return (best[1] << 13) | add_tile(best[2])

models = []
for m in art['models']:
    mg = m['geom']
    tm = clean_toaster(load('toaster-%s-up.png' % m['id']), load('toaster-%s-down.png' % m['id']), mg)
    bbox = tm.getbbox()
    assert bbox[0] >= PATCH_X * 8 and bbox[2] <= (PATCH_X + PATCH_W) * 8 and bbox[1] >= PATCH_Y * 8 and bbox[3] <= (PATCH_Y + PATCH_H) * 8, (m['id'], bbox)
    sm = Image.new('RGBA', (W, H), NAVY + (255,)); sm.alpha_composite(kitchen); sm.alpha_composite(tm)
    fm = front_of(tm, mg)
    cells = [(PATCH_X + i, PATCH_Y + j) for j in range(PATCH_H) for i in range(PATCH_W)]
    models.append(dict(id=m['id'], geom=mg,
                       b=[nearest_tile(sm, x, y) for x, y in cells],
                       a=[(e | 0x8000) if e else 0 for e in (nearest_tile(fm, x, y) for x, y in cells)]))
# ---------- the loaf on the board, for each bread ----------
BREAD_X, BREAD_Y, BREAD_W, BREAD_H = 2, 15, 5, 3
BREAD_IDS = ['white', 'brown', 'sourdough', 'crumpet', 'brioche', 'rye', 'teacake']
bread_patches = []
for bid in BREAD_IDS:
    kb = Image.new('RGBA', (W, H), NAVY + (255,)); kb.alpha_composite(load('kitchen-bread-%s.png' % bid))
    bread_patches.append([nearest_tile(kb, BREAD_X + i, BREAD_Y + j) for j in range(BREAD_H) for i in range(BREAD_W)])
scene_tiles = len(tiles)

# ---------- sprites and HUD art (palette indices drawn by hand) ----------
def sprite_tiles(pix, wt, ht):
    """pix: rows of indices, wt x ht tiles; returns the first tile, tiles in column-major order."""
    first = None
    for cx in range(wt):
        for cy in range(ht):
            t = [pix[cy * 8 + j][cx * 8 + i] for j in range(8) for i in range(8)]
            tiles.append(tuple(t))
            if first is None: first = len(tiles) - 1
    return first

def blank(w, h): return [[0] * w for _ in range(h)]

def slice_img(body, crust):
    """The loaf slice from drawSlice, at (2, 0) in a 16x16 sprite."""
    p = blank(16, 16)
    def r(x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                if 0 <= i < 16 and 0 <= j < 16: p[j][i] = c
    x, y = 2, 0
    r(x + 3, y, 6, 1, crust); r(x + 1, y + 1, 10, 1, crust); r(x, y + 2, 12, 14, crust)
    r(x + 3, y + 1, 6, 1, body); r(x + 1, y + 2, 10, 13, body)
    return p

def rot90(p):  # clockwise
    n = len(p)
    return [[p[n - 1 - i][j] for i in range(n)] for j in range(n)]

slot_slice = sprite_tiles(slice_img(14, 15), 2, 2)
fly = []  # per band, four quarter turns
for b in range(1, 9):
    img = slice_img(b, min(8, b + 2))
    for k in range(4):
        fly.append(sprite_tiles(img, 2, 2)); img = rot90(img)

knob = blank(16, 8)
kc = [pal_lists[knob_pal].index(mc(c)) + 1 for c in KNOB]
for i in range(9): knob[0][i] = kc[1]
for j in range(1, 4):
    for i in range(9): knob[j][i] = kc[0]
    knob[j][8] = kc[2]
knob_tile = sprite_tiles(knob, 2, 1)

# plate slices: one per doneness band, crust two bands darker
plate_tiles = []
for b in range(1, 9):
    p = blank(16, 8)
    crust = min(8, b + 2)
    for i in range(16): p[0][i] = p[2][i] = crust
    p[1] = [crust] + [b] * 14 + [crust] + [0] * 0
    plate_tiles.append(sprite_tiles(p, 2, 1))

# doneness meter: 29 tiles from x=80; bands by doneness
BAND_EDGES = [18, 36, 50, 62, 70, 80, 90]  # band i covers up to edge i
def band_of(d):
    return 1 + sum(1 for e in BAND_EDGES if d > e)
bar = blank(232, 8)
for x in range(232):
    d = x / 231 * 100
    for y in range(1, 7): bar[y][x] = band_of(d)
    bar[0][x] = bar[7][x] = 13
bar_first = len(tiles)
for cx in range(29):
    tiles.append(tuple(bar[j][cx * 8 + i] for j in range(8) for i in range(8)))

bracket = blank(8, 16)
for y in range(14):
    bracket[y][0] = bracket[y][1] = 10
for x in range(4): bracket[0][x] = bracket[13][x] = 10
bracket_tile = sprite_tiles(bracket, 1, 2)

needle = blank(8, 16)
for y in range(12): needle[y][2] = 12
for i in range(5): needle[13][i] = 12
for i in range(1, 4): needle[14][i] = 12
needle[15][2] = 12
needle_tile = sprite_tiles(needle, 1, 2)

crumb = blank(8, 8)
crumb[0][0] = crumb[0][1] = crumb[1][0] = crumb[1][1] = 15  # the toast's crust colour
star = blank(8, 8)
star[0][1] = star[1][0] = star[1][1] = star[1][2] = star[2][1] = 12
crumb_tile, star_tile = sprite_tiles(crumb, 1, 1), sprite_tiles(star, 1, 1)

# the spreads on the shelf: one 16 x 16 sprite each, in whichever kitchen palette fits it best
SPREAD_IDS = ['butter', 'marmalade', 'jam', 'honey', 'truffle']
JAR_X, JAR_Y = 202, 77
jar_tiles, jar_pals = [], []
for sid in SPREAD_IDS:
    im = load('kitchen-jar-%s.png' % sid)
    a_, b_ = kitchen.load(), im.load()
    px16 = [[b_[JAR_X + i, JAR_Y + j][:3] if b_[JAR_X + i, JAR_Y + j] != a_[JAR_X + i, JAR_Y + j] else None for i in range(16)] for j in range(16)]
    best = None
    for pi, pl in enumerate(pal_lists):
        err = 0
        rows = []
        for row in px16:
            out = []
            for c in row:
                if c is None: out.append(0); continue
                c = mc(c)
                k = min(range(len(pl)), key=lambda k: dist(pl[k], c))
                err += dist(pl[k], c); out.append(k + 1)
            rows.append(out)
        if best is None or err < best[0]: best = (err, pi, rows)
    jar_tiles.append(sprite_tiles(best[2], 2, 2)); jar_pals.append(best[1])

# the toast in the slots, shaped by the bread (body 14, crust 15)
def shaped_slice(shape):
    p = blank(16, 16)
    def r(x, y, w, h, c):
        for j in range(y, y + h):
            for i in range(x, x + w):
                if 0 <= i < 16 and 0 <= j < 16: p[j][i] = c
    x, y, C, B = 2, 0, 15, 14
    if shape in ('crumpet', 'teacake'):
        r(x + 2, y + 4, 8, 1, C); r(x + 1, y + 5, 10, 1, C); r(x, y + 6, 12, 8, C); r(x + 1, y + 14, 10, 1, C); r(x + 2, y + 15, 8, 1, C)
        r(x + 2, y + 5, 8, 1, B); r(x + 1, y + 6, 10, 8, B); r(x + 2, y + 14, 8, 1, B)
        dot = 13 if shape == 'teacake' else 15  # raisins, or holes
        for a, c in [(3, 7), (7, 7), (5, 9), (2, 11), (8, 10), (5, 12)]: r(x + a, y + c, 1, 1, dot)
        return p
    dome = 2 if shape == 'brioche' else 0
    y = 2  # room for the brioche's dome
    r(x + 3, y - dome, 6, 1, C); r(x + 1, y + 1 - dome, 10, 1 + dome, C); r(x, y + 2, 12, 12, C)
    if shape == 'sour': r(x - 1, y + 3, 1, 3, C); r(x + 12, y + 4, 1, 3, C)
    r(x + 3, y + 1 - dome, 6, 1 + dome, B); r(x + 1, y + 2, 10, 11, B)
    if shape in ('sour', 'seeded'):
        for a, c in [(3, 5), (8, 7), (5, 10), (9, 12)]: r(x + a, y + c, 1, 1, C)
    return p
SLICE_SHAPES = ['loaf', 'seeded', 'sour', 'crumpet', 'brioche', 'seeded', 'teacake']  # per bread
slice_tiles = {sh: sprite_tiles(shaped_slice(sh), 2, 2) for sh in dict.fromkeys(SLICE_SHAPES)}

# the lamp at night: a dithered cone of light (bracket yellow, every fourth pixel) and the bulb
LAMP_X, CONE_Y0, CONE_Y1 = 236, 40 + OY, 133 + OY
CONE_X0 = LAMP_X - 64
cone = blank(128, 96)
for y in range(CONE_Y0, CONE_Y1):
    hw = 4 + (y - CONE_Y0) * 0.55
    for x in range(round(LAMP_X - hw), round(LAMP_X + hw)):
        if (x % 2 == 0) and (y % 2 == 0) and ((x // 2 + y // 2) % 2 == 0):
            cone[y - CONE_Y0][x - CONE_X0] = 10
cone_tiles = []
for cy in range(3):
    for cx in range(4):
        part = [row[cx * 32:(cx + 1) * 32] for row in cone[cy * 32:(cy + 1) * 32]]
        cone_tiles.append(sprite_tiles(part, 4, 4) if any(any(r) for r in part) else 0)
bulb = blank(8, 8)
for i in range(5): bulb[0][1 + i] = 10; bulb[1][1 + i] = 10
for i in range(3): bulb[2][2 + i] = 12
bulb_tile = sprite_tiles(bulb, 1, 1)

# panels for the shop and the results: navy, with a lavender rule along the top
panel_tile = sprite_tiles([[13] * 8 for _ in range(8)], 1, 1)
panel_top = sprite_tiles([[10] * 8] + [[13] * 8 for _ in range(7)], 1, 1)

def life_icon(body, edge):
    rows = ['..cccc..', '.cbbbbc.', 'cbbbbbbc', 'cbbbbbbc', 'cbbbbbbc', 'cbbbbbbc', 'cbbbbbbc', 'cccccccc']
    return [[{'c': edge, 'b': body, '.': 0}[ch] for ch in r] for r in rows]
life_tile = sprite_tiles(life_icon(6, 8), 1, 1)
life_gone_tile = sprite_tiles(life_icon(0, 9), 1, 1)

# the black cat: a silhouette with a lavender sheen and yellow eyes; two walking frames, 32 x 16
CAT = [
  '.oo.................o....o..', 'okko...............oko..oko.', 'oko................okkkkkko.',
  'oko...............okkkkeekko', '.oko..............okkkkeokko', '..okooooooooooooookkkkkkkkkp',
  '...okkkhhhhhhhhkkkkkkkkkkko.', '...okkkkkkkkkkkkkkkkkkkkko..', '...okkkkkkkkkkkkkkkkkkko.....',
  '...okkkkkkkkkkkkkkkkkkko.....', '...okkkkkkkkkkkkkkkkkkko.....', '....okkkkkkkkkkkkkkkkko......',
  '.....oooooooooooooooo.......',
]
LEGS = [['.....oko.........oko........'] * 2 + ['.....ooo.........ooo........'],
        ['.......oko.....oko..........'] * 2 + ['.......ooo.....ooo..........']]
CAT_COL = {'.': 0, 'o': 13, 'k': 13, 'h': 9, 'e': 10, 'p': 9}
cat_tiles = []
for legs in LEGS:
    img = blank(32, 16)
    for y, row in enumerate(CAT + legs):
        for x, ch in enumerate(row): img[y][x] = CAT_COL[ch]
    cat_tiles.append(sprite_tiles(img, 4, 2))

# ---------- fonts (Press Start 2P, from the browser) ----------
chars = art['chars']
def glyph_bits(k):
    data = art['glyphs'][k]
    return [[data[(y * 9 + x) * 4 + 3] > 100 for x in range(8)] for y in range(8)]

small_first, label_first, big_first = len(tiles), None, None
for k in range(len(chars)):  # white, with a navy shadow
    gb = glyph_bits(k)
    t = []
    for y in range(8):
        for x in range(8):
            t.append(12 if gb[y][x] else 13 if (x > 0 and y > 0 and gb[y - 1][x - 1]) else 0)
    tiles.append(tuple(t))
label_first = len(tiles)
for k in range(len(chars)):  # the dim label colour, no shadow
    gb = glyph_bits(k)
    tiles.append(tuple(9 if gb[y][x] else 0 for y in range(8) for x in range(8)))
panel_font = len(tiles)
for k in range(len(chars)):  # on a navy panel: white, then the dim lavender, both opaque
    gb = glyph_bits(k)
    tiles.append(tuple(12 if gb[y][x] else 13 for y in range(8) for x in range(8)))
for k in range(len(chars)):
    gb = glyph_bits(k)
    tiles.append(tuple(9 if gb[y][x] else 13 for y in range(8) for x in range(8)))
for k in range(len(chars)):  # and yellow, for headings
    gb = glyph_bits(k)
    tiles.append(tuple(10 if gb[y][x] else 13 for y in range(8) for x in range(8)))
big_first = len(tiles)
for k in range(len(chars)):  # twice the size, word colour with a 2px shadow, 2x2 tiles column-major
    gb = glyph_bits(k)
    p = blank(16, 16)
    for y in range(16):
        for x in range(16):
            if gb[y // 2][x // 2]: p[y][x] = 11
            elif x > 1 and y > 1 and gb[(y - 2) // 2][(x - 2) // 2]: p[y][x] = 13
    sprite_tiles(p, 2, 2)

assert len(tiles) * 32 <= 0xC000, 'tiles overflow into the name tables'  # 0xB000-0xBFFF is free: the window plane is off

# ---------- write C ----------
def words(t):
    out = []
    for j in range(8):
        v = 0
        for i in range(8): v = (v << 4) | t[j * 8 + i]
        out.append('0x%08X' % v)
    return ','.join(out)

hud_pal = [0] * 16
named = {9: (146, 146, 219), 10: (255, 219, 73), 11: (255, 219, 73), 12: (255, 255, 255), 13: NAVY}
for i, c in named.items(): hud_pal[i] = md_colour(c)

with open(os.path.join(SRC, 'assets.c'), 'w') as f:
    f.write('// Generated by tools/build_assets.py from the browser game\'s art. Do not edit.\n#include "assets.h"\n\n')
    f.write('const u32 tile_data[%d * 8] = {\n' % len(tiles))
    for t in tiles: f.write('  %s,\n' % words(t))
    f.write('};\n\n')
    f.write('const u16 scene_pal[3][16] = {\n')
    for pl in pal_lists:
        f.write('  { 0x0000, %s },\n' % ', '.join('0x%04X' % md_colour(c) for c in pl + [(0, 0, 0)] * (15 - len(pl))))
    f.write('};\n')
    f.write('const u16 hud_pal[16] = { %s };\n\n' % ', '.join('0x%04X' % c for c in hud_pal))
    for name, data in (('plane_b_map', plane_b), ('plane_a_map', plane_a)):
        f.write('const u16 %s[28 * 40] = {\n' % name)
        for r in range(28): f.write('  %s,\n' % ', '.join('0x%04X' % e for e in data[r * 40:(r + 1) * 40]))
        f.write('};\n\n')
    f.write('const char font_chars[] = "%s";\n' % chars.replace('\\', '\\\\').replace('"', '\\"'))
    f.write('const u8 band_edges[7] = { %s };\n' % ', '.join(map(str, BAND_EDGES)))
    f.write('const u16 plate_tiles[8] = { %s };\n' % ', '.join(map(str, plate_tiles)))
    f.write('const u16 fly_tiles[32] = { %s };\n' % ', '.join(map(str, fly)))
    f.write('const u16 model_b[%d][%d] = {\n' % (len(models), PATCH_W * PATCH_H))
    for m in models: f.write('  { %s },\n' % ', '.join('0x%04X' % e for e in m['b']))
    f.write('};\nconst u16 model_a[%d][%d] = {\n' % (len(models), PATCH_W * PATCH_H))
    for m in models: f.write('  { %s },\n' % ', '.join('0x%04X' % e for e in m['a']))
    f.write('};\n')
    f.write('const ToasterArt model_art[%d] = {\n' % len(models))
    for m in models:
        mg = m['geom']
        f.write('  { %d, %d, %d, %d, %d, %d, %d },\n' % (mg['x'], mg['top'] + OY, mg['w'], mg['h'], mg['slotY'] + OY,
                                                       round(mg['slots'][0]), round(mg['slots'][1])))
    f.write('};\n')
    def slot_of(c):
        for pi, pl in enumerate(pal_lists):
            if c in pl: return pi * 16 + pl.index(c) + 1
        return 0
    f.write('const u16 bread_b[%d][%d] = {\n' % (len(bread_patches), BREAD_W * BREAD_H))
    for bp in bread_patches: f.write('  { %s },\n' % ', '.join('0x%04X' % e for e in bp))
    f.write('};\n')
    f.write('const u16 jar_tiles[5] = { %s };\nconst u8 jar_pals[5] = { %s };\n' % (', '.join(map(str, jar_tiles)), ', '.join(map(str, jar_pals))))
    f.write('const u16 slice_tiles[7] = { %s };\n' % ', '.join(str(slice_tiles[sh]) for sh in SLICE_SHAPES))
    f.write('const u16 cone_tiles[12] = { %s };\n' % ', '.join(map(str, cone_tiles)))
    f.write('const u8 sky_slots[3] = { %d, %d, %d };  // sky, sky top band, sun\n' % (
        slot_of(H_('6db6ff')), slot_of(H_('4992db')), slot_of(H_('ffff92'))))
    import math
    f.write('const s8 sin_lut[256] = { %s };\n' % ', '.join(str(round(math.sin(i / 256 * 2 * math.pi) * 127)) for i in range(256)))

defs = dict(TILE_COUNT=len(tiles), SCENE_TILES=scene_tiles, SLOT_SLICE_TILE=slot_slice, KNOB_TILE=knob_tile, KNOB_PAL=knob_pal,
            BAR_TILE=bar_first, BRACKET_TILE=bracket_tile, NEEDLE_TILE=needle_tile, CRUMB_TILE=crumb_tile, STAR_TILE=star_tile,
            BREAD_X=BREAD_X, BREAD_Y=BREAD_Y, BREAD_W=BREAD_W, BREAD_H=BREAD_H, JAR_X=JAR_X, JAR_Y=JAR_Y,
            CONE_X=CONE_X0, CONE_Y=CONE_Y0, BULB_TILE=bulb_tile, LAMP_X=LAMP_X, LAMP_Y=37 + OY,
            FONT_SMALL=small_first, FONT_PANEL=panel_font, FONT_PANEL_DIM=panel_font + len(chars), FONT_PANEL_GOLD=panel_font + 2 * len(chars), PANEL_TILE=panel_tile, PANEL_TOP=panel_top, LIFE_TILE=life_tile, LIFE_GONE_TILE=life_gone_tile,
            CAT_TILE0=cat_tiles[0], CAT_TILE1=cat_tiles[1], MODEL_COUNT=len(models),
            PATCH_X=PATCH_X, PATCH_Y=PATCH_Y, PATCH_W=PATCH_W, PATCH_H=PATCH_H, FONT_LABEL=label_first, FONT_BIG=big_first, FONT_COUNT=len(chars),
            TOASTER_X=tx, TOASTER_TOP=ttop, TOASTER_W=tw, TOASTER_H=th, SLOT_Y=slot_y, SLOT0_X=round(g['slots'][0]), SLOT1_X=round(g['slots'][1]),
            LEVER_X=lx, SCENE_OY=OY)
with open(os.path.join(SRC, 'assets.h'), 'w') as f:
    f.write('// Generated by tools/build_assets.py. Do not edit.\n#pragma once\n#include "md.h"\n\n')
    for k, v in defs.items(): f.write('#define %s %d\n' % (k, v))
    f.write('\ntypedef struct { s16 x, top, w, h, slot_y, slot0, slot1; } ToasterArt;\n')
    f.write('extern const u16 model_b[][%d];\nextern const u16 model_a[][%d];\nextern const ToasterArt model_art[];\nextern const u8 sky_slots[3];\n' % (PATCH_W * PATCH_H, PATCH_W * PATCH_H))
    f.write('extern const u16 bread_b[][%d];\nextern const u16 jar_tiles[5];\nextern const u8 jar_pals[5];\nextern const u16 slice_tiles[7];\nextern const u16 cone_tiles[12];\n' % (BREAD_W * BREAD_H))
    f.write('\nextern const u32 tile_data[];\nextern const u16 scene_pal[3][16];\nextern const u16 hud_pal[16];\n')
    f.write('extern const u16 plane_b_map[28 * 40];\nextern const u16 plane_a_map[28 * 40];\n')
    f.write('extern const char font_chars[];\nextern const u8 band_edges[7];\nextern const u16 plate_tiles[8];\nextern const u16 fly_tiles[32];\nextern const s8 sin_lut[256];\n')
print('tiles', len(tiles), 'scene tiles', scene_tiles, 'palettes', [len(p) for p in pal_lists], 'merged', len(merge))
