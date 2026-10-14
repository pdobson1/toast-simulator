// Toast Simulator for the Sega Mega Drive.
// Push the lever, watch the needle, pop the toast in the yellow brackets.
#include "md.h"
#include "assets.h"

#define CENTER 72
#define ROUNDS_PER_LEVEL 6
#define MAX_LEVEL 10
#define LIVES 3
#define PX 263                  // the plate, screen pixels
#define PY (129 + SCENE_OY)
// Positions are 16.16 fixed point: GCC's 68000 code reads the top half of a long at an aligned address,
// but turns an 8.8 value's ">> 8" into a misaligned word read, which crashes a real 68000.
#define GRAVITY 9466            // pixels per frame per frame, 16.16 (520 px/s/s)

#define PAL_HUD 3
#define COL_WORD (PAL_HUD * 16 + 11)
#define COL_BODY (PAL_HUD * 16 + 14)
#define COL_CRUST (PAL_HUD * 16 + 15)

typedef struct { u8 r, g, b, unused; } __attribute__((aligned(2))) RGB3;  // 0-7 per channel; padded so GCC never copies it at an odd address
#define RGB(r, g, b) (((b) << 9) | ((g) << 5) | ((r) << 1))

// ---------- what you can buy ----------
typedef struct { const char *name; u32 cost; u16 speed, win, wobble, launch; u8 beep; const char *blurb; } Toaster;
static const Toaster TOASTERS[MODEL_COUNT] = {  // speed and wobble in 8.8; win in doneness
    {"NAN'S POP-UP", 0, 256, 9, 115, 165, 0, "YOUR NAN'S. IT'S SEEN THINGS. THE HEAT COMES AND GOES."},
    {"KWIK-KRISP 2000", 60, 307, 11, 64, 200, 0, "FROM A CATALOGUE, 1994. TWO SETTINGS: OFF AND YES."},
    {"CHROMELINER DELUXE", 300, 346, 14, 20, 240, 0, "GLEAMS. STEADY HEAT, AND THE TOAST REALLY FLIES."},
    {"SMARTTOAST PRO", 1200, 384, 16, 0, 265, 1, "BEEPS WHEN IT'S NEARLY THERE. HAS AN APP. YOU WON'T DOWNLOAD IT."},
    {"TOASTOMATIC 9000", 5000, 435, 18, 0, 300, 1, "OFF A HOTEL BREAKFAST BUFFET. DON'T ASK."},
    {"RETRO ROCKET", 15000, 474, 20, 0, 325, 1, "FINS, A PORTHOLE, AND A LAUNCH YOU CAN HEAR FROM THE HALL."},
    {"THE GOLDEN CRUMB", 50000, 512, 22, 0, 345, 1, "SOLID GOLD, APPARENTLY. THE INSURANCE COMPANY HAS QUESTIONS."},
};

typedef struct { const char *name; u32 cost; u16 score10, pay10, rate, win; u8 surge, accel; RGB3 raw; const char *blurb; } Bread;
#define BREAD_COUNT 7
static const Bread BREADS[BREAD_COUNT] = {  // rate and win in 8.8
    {"WHITE", 0, 10, 10, 256, 256, 0, 0, {7, 7, 6}, "SOFT, FORGIVING, A BIT SAD. EVERYONE STARTS HERE."},
    {"BROWN", 40, 12, 16, 218, 256, 0, 0, {6, 5, 4}, "HAS BITS IN. PROBABLY GOOD FOR YOU."},
    {"SOURDOUGH", 200, 15, 30, 256, 230, 179, 0, {7, 6, 5}, "THREE DAYS TO MAKE, NINETY SECONDS TO RUIN. TOASTS IN FITS AND STARTS."},
    {"CRUMPET", 700, 20, 50, 192, 205, 0, 1, {7, 6, 4}, "HOLES FOR THE MARGARINE. SLOW, THEN SUDDENLY NOT."},
    {"BRIOCHE", 2500, 25, 100, 410, 154, 0, 0, {7, 6, 3}, "ALL THAT SUGAR. BLINK AND IT'S CHARCOAL."},
    {"DARK RYE", 8000, 30, 160, 230, 128, 230, 0, {5, 4, 3}, "DENSE, DARK AND IN NO HURRY. THEN IN A VERY BIG HURRY."},
    {"TEACAKE", 25000, 40, 300, 333, 128, 0, 1, {6, 5, 3}, "FRUITY, SPICED, AND THE RAISINS BURN FIRST."},
};

typedef struct { const char *name; u32 cost; u16 mult10; const char *blurb; } Spread;
#define SPREAD_COUNT 5
static const Spread SPREADS[SPREAD_COUNT] = {
    {"MARGARINE", 150, 15, "SOFT, STRAIGHT FROM THE FRIDGE."},
    {"THICK-CUT MARMALADE", 900, 15, "BITTER, CHUNKY, CORRECT."},
    {"STRAWBERRY JAM", 3000, 20, "THE GOOD STUFF, WITH WHOLE STRAWBERRIES."},
    {"ALMOND BUTTER", 10000, 20, "SMOOTH, ROASTED, STIRRED UNTIL YOUR ARM HURTS."},
    {"TRUFFLE SPREAD", 40000, 25, "PLANT-BASED, BUT IT WENT TO PRIVATE SCHOOL."},
};

// ---------- the save, kept in the cartridge's battery-backed RAM ----------
#define SAVE_MAGIC 0x544F5354UL  // "TOST"
typedef struct { u32 score; u8 level, won; } HiScore;
typedef struct {
    u32 magic;
    u32 tp, rounds, perfects;
    u8 owned_toasters, owned_breads, owned_spreads, toaster, bread, pad;
    u16 best, clock;
    HiScore hi[5];
    u16 sum;
} Save;
static Save save;

#define SRAM ((volatile u8 *)0x200001)
#define SRAM_CTRL (*(volatile u8 *)0xA130F1)

static u16 save_sum(void) {
    const u8 *b = (const u8 *)&save;
    u16 s = 0x5A5A;
    for (u16 i = 0; i < sizeof(Save) - 2; i++) s = (s << 1 | s >> 15) ^ b[i];
    return s;
}

static void save_write(void) {
    save.sum = save_sum();
    const u8 *b = (const u8 *)&save;
    SRAM_CTRL = 1;
    for (u16 i = 0; i < sizeof(Save); i++) SRAM[i * 2] = b[i];
}

static void save_load(void) {
    u8 *b = (u8 *)&save;
    SRAM_CTRL = 1;
    for (u16 i = 0; i < sizeof(Save); i++) b[i] = SRAM[i * 2];
    if (save.magic != SAVE_MAGIC || save.sum != save_sum()) {
        memset(&save, 0, sizeof(Save));
        save.magic = SAVE_MAGIC;
        save.owned_toasters = save.owned_breads = 1;
        save.clock = 4200;  // 7am
    }
    if (save.toaster >= MODEL_COUNT) save.toaster = 0;
    if (save.bread >= BREAD_COUNT) save.bread = 0;
}

// ---------- state ----------
enum { TITLE, PLAY, SHOP, RESULTS };
enum { READY, TOASTING, FLYING };

static u16 mode, state, frame;
static s32 doneness;               // 0-100, 8.8 fixed point
static s16 lever, lever_to;        // 0-256
static u16 phase1, phase2, phase3;
static u16 level = 1, level_round, lives = LIVES, streak, run_best;
static u32 score;
static u16 pad, pad_prev, pressed;
static u8 hud_dirty = 1, beeped, game_over_pending, won_pending;
static u16 rnd_state = 12345;
static const Toaster *T;
static const Bread *B;
static const ToasterArt *A;

static u16 rnd(void) { rnd_state ^= rnd_state << 7; rnd_state ^= rnd_state >> 9; rnd_state ^= rnd_state << 8; return rnd_state; }

// ---------- colour ----------
static const struct { u8 d; RGB3 c; } STOPS[5] = {
    {45, {7, 6, 4}}, {CENTER, {6, 4, 2}}, {86, {4, 2, 1}}, {96, {2, 1, 0}}, {100, {1, 1, 1}},
};

static u16 md_colour(RGB3 c) { return RGB(c.r, c.g, c.b); }

static RGB3 mix(RGB3 a, RGB3 b, s32 t) {  // t: 0-256
    RGB3 out;
    out.r = (a.r * 256 + (b.r - a.r) * t + 128) >> 8;
    out.g = (a.g * 256 + (b.g - a.g) * t + 128) >> 8;
    out.b = (a.b * 256 + (b.b - a.b) * t + 128) >> 8;
    return out;
}

// the toast's colour at a doneness (8.8), as in the browser version
static RGB3 toast_col(s32 d) {
    RGB3 prev = B->raw;
    s32 prev_d = 0;
    for (int i = 0; i < 5; i++) {
        s32 sd = (s32)STOPS[i].d << 8;
        if (d <= sd) return mix(prev, STOPS[i].c, ((d - prev_d) << 8) / (sd - prev_d));
        prev = STOPS[i].c;
        prev_d = sd;
    }
    return STOPS[4].c;
}

static RGB3 darker(RGB3 c) {
    c.r = c.r > 2 ? c.r - 2 : 0; c.g = c.g > 2 ? c.g - 2 : 0; c.b = c.b > 2 ? c.b - 2 : 0;
    return c;
}

static u16 band_of(s32 d) {
    u16 b = 1;
    for (int i = 0; i < 7; i++) if (d > ((s32)band_edges[i] << 8)) b++;
    return b;
}

static void set_bands(void) {
    static const u8 centres[8] = {9, 27, 43, 56, 66, 75, 85, 95};
    for (int i = 0; i < 8; i++) vdp_set_colour(PAL_HUD * 16 + 1 + i, md_colour(toast_col((s32)centres[i] << 8)));
}

// ---------- day and night: a day every four minutes, the kitchen's palettes multiplied by the light ----------
typedef struct { u16 m; RGB3 c; } Key;
static const Key LIGHT[] = {{0, {3, 3, 4}}, {330, {3, 3, 4}}, {400, {6, 5, 5}}, {480, {7, 7, 7}}, {1080, {7, 7, 7}},
                            {1160, {7, 6, 5}}, {1230, {4, 3, 4}}, {1290, {3, 3, 4}}, {1440, {3, 3, 4}}};
static const Key SKY[] = {{0, {0, 0, 1}}, {300, {0, 0, 1}}, {360, {2, 1, 2}}, {420, {7, 4, 3}}, {480, {3, 5, 7}}, {1080, {3, 5, 7}},
                          {1140, {7, 4, 2}}, {1200, {4, 2, 2}}, {1260, {1, 1, 2}}, {1440, {0, 0, 1}}};

static RGB3 keyed(const Key *k, u16 m) {
    while (m > k[1].m) k++;
    return mix(k[0].c, k[1].c, ((s32)(m - k[0].m) << 8) / (k[1].m - k[0].m));
}

static void apply_light(void) {
    u16 m = save.clock / 10;
    RGB3 L = keyed(LIGHT, m), sky = keyed(SKY, m);
    for (u16 p = 0; p < 3; p++) {
        for (u16 i = 1; i < 16; i++) {
            u16 c = scene_pal[p][i];
            RGB3 o = {(c >> 1) & 7, (c >> 5) & 7, (c >> 9) & 7};
            o.r = (o.r * L.r + 3) / 7; o.g = (o.g * L.g + 3) / 7; o.b = (o.b * L.b + 3) / 7;
            vdp_set_colour(p * 16 + i, md_colour(o));
        }
    }
    if (sky_slots[0]) vdp_set_colour(sky_slots[0], md_colour(sky));
    if (sky_slots[1]) vdp_set_colour(sky_slots[1], md_colour(darker(sky)) | 0);
    if (sky_slots[2] && L.r + L.g + L.b < 15) vdp_set_colour(sky_slots[2], md_colour(sky));  // the sun sets
}

// ---------- levels ----------
static u16 lvl(void) { return level - 1; }
static u16 level_speed(void) { return 256 + 38 * lvl(); }
static s32 window(void) {  // the perfect window, doneness 8.8
    s32 lw = 256 - 13 * lvl();
    if (lw < 141) lw = 141;
    return (s32)T->win * B->win * lw >> 8;
}

// ---------- sound ----------
typedef struct { u16 start, len, p0, p1; u8 ch, vol; } Note;
#define MAX_NOTES 24
static Note notes[MAX_NOTES];

static void note(u16 ch, u16 hz0, u16 hz1, u16 len, u16 vol, u16 delay) {
    for (int i = 0; i < MAX_NOTES; i++) {
        Note *n = &notes[i];
        if (n->len && (s16)(n->start + n->len - frame) > 0) continue;  // still waiting or playing
        n->ch = ch; n->start = frame + delay; n->len = len; n->vol = vol;
        n->p0 = HZ(hz0); n->p1 = HZ(hz1);
        return;
    }
}

static void sound_update(void) {
    for (u16 ch = 0; ch < 3; ch++) {
        Note *cur = 0;
        for (int i = 0; i < MAX_NOTES; i++) {
            Note *n = &notes[i];
            if (n->len && n->ch == ch && (u16)(frame - n->start) < n->len && (!cur || (s16)(n->start - cur->start) > 0)) cur = n;
        }
        if (!cur) { psg_vol(ch, 15); continue; }
        u16 t = frame - cur->start;
        psg_tone(ch, cur->p0 + (s16)((s32)((s16)cur->p1 - (s16)cur->p0) * t / cur->len));
        psg_vol(ch, cur->vol + (15 - cur->vol) * t / cur->len);
    }
}

static void jingle(const u16 *hz, u16 n, u16 step, u16 delay) {
    for (u16 i = 0; i < n; i++) note(2, hz[i], hz[i], step + 2, 3, delay + i * step);
}
static void sfx_meow(void) { note(2, 520, 820, 7, 2, 0); note(2, 820, 430, 18, 2, 7); }

// ---------- text ----------
static u16 glyph(char c) {
    for (u16 i = 0; font_chars[i]; i++) if (font_chars[i] == c) return i;
    return 0;
}

typedef struct { char buf[24]; s32 y; s16 x; u16 life, big; } Text;  // rising sprite text
#define MAX_TEXTS 5
static Text texts[MAX_TEXTS];

static void say(const char *s, s16 cx, s16 y, u16 big, u16 colour) {
    Text *t = &texts[0];
    for (int i = 0; i < MAX_TEXTS; i++) if (!texts[i].life) { t = &texts[i]; break; }
    u16 n = 0;
    while (s[n] && n < 23) { t->buf[n] = s[n]; n++; }
    t->buf[n] = 0;
    t->x = cx - n * (big ? 8 : 4);
    t->y = (s32)y << 16;
    t->life = 84;
    t->big = big;
    if (big) vdp_set_colour(COL_WORD, colour);
}

static void sprite_text(const char *s, s16 x, s16 y, u16 big) {
    for (u16 k = 0; s[k]; k++) {
        if (s[k] == ' ') continue;
        u16 g = glyph(s[k]);
        if (big) spr_add(x + k * 16, y, 2, 2, ATTR(PAL_HUD, 1, 0, 0, FONT_BIG + g * 4));
        else spr_add(x + k * 8, y, 1, 1, ATTR(PAL_HUD, 1, 0, 0, FONT_SMALL + g));
    }
}

static u16 slen(const char *s) { u16 n = 0; while (s[n]) n++; return n; }

static void draw_texts(void) {
    for (int i = 0; i < MAX_TEXTS; i++) {
        Text *t = &texts[i];
        if (!t->life) continue;
        t->life--;
        t->y -= 60 << 8;
        if (t->life < 15 && (t->life / 2) % 2) continue;
        sprite_text(t->buf, t->x, t->y >> 16, t->big);
    }
}

static char *num(u32 n, char *end) {  // with thousands commas, written backwards from end
    *end = 0;
    int digits = 0;
    do {
        if (digits && digits % 3 == 0) *--end = ',';
        *--end = '0' + n % 10;
        n /= 10;
        digits++;
    } while (n);
    return end;
}

static char *num_short(u32 n, char *end) {  // 123,456 as 123K for the status line
    if (n < 100000) return num(n, end);
    if (n < 10000000) { char *p = num(n / 1000, end - 1); end[-1] = 'K'; return p; }
    char *p = num(n / 1000000, end - 1); end[-1] = 'M'; return p;
}

static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }

static void print(u16 x, u16 y, const char *s, u16 font) {  // plane A text; spaces are empty or panel
    for (; *s && x < 40; s++, x++) {
        u16 t = *s == ' ' ? (font == FONT_SMALL ? 0 : PANEL_TILE) : font + glyph(*s);
        vdp_set_tile(VRAM_PLANE_A, x, y, ATTR(PAL_HUD, 1, 0, 0, t));
    }
}

static void print_right(u16 right, u16 y, const char *s, u16 font) { print(right - slen(s), y, s, font); }

static void draw_status(void) {
    char line[41], b[16], *p = line;
    p = cat(p, " SCORE "); p = cat(p, num_short(score, b + 15));
    p = cat(p, "  TP "); p = cat(p, num_short(save.tp, b + 15));
    p = cat(p, "  LV "); p = cat(p, num(level, b + 15)); p = cat(p, " ");
    p = cat(p, num(level_round, b + 15)); p = cat(p, "/6");
    while (p < line + 36) *p++ = ' ';
    *p = 0;
    print(0, 26, line, FONT_SMALL);
    for (u16 i = 0; i < LIVES; i++)
        vdp_set_tile(VRAM_PLANE_A, 36 + i, 26, ATTR(PAL_HUD, 1, 0, 0, i < lives ? LIFE_TILE : LIFE_GONE_TILE));
}

// ---------- the toaster on the worktop ----------
static void restore_plane_a(u16 y0, u16 y1) {  // the kitchen's plane A rows, with the current toaster's front
    for (u16 y = y0; y <= y1; y++) {
        vram_write_addr(VRAM_PLANE_A + y * 128);
        for (u16 x = 0; x < 40; x++) {
            u16 e = plane_a_map[y * 40 + x];
            if (x >= PATCH_X && x < PATCH_X + PATCH_W && y >= PATCH_Y && y < PATCH_Y + PATCH_H)
                e = model_a[save.toaster][(y - PATCH_Y) * PATCH_W + x - PATCH_X];
            VDP_DATA = e;
        }
    }
}

static void use_toaster(u16 i) {
    save.toaster = i;
    T = &TOASTERS[i];
    A = &model_art[i];
    for (u16 y = 0; y < PATCH_H; y++)
        for (u16 x = 0; x < PATCH_W; x++)
            vdp_set_tile(VRAM_PLANE_B, PATCH_X + x, PATCH_Y + y, model_b[i][y * PATCH_W + x]);
    restore_plane_a(PATCH_Y, PATCH_Y + PATCH_H - 1);
}

static void use_bread(u16 i) {
    save.bread = i;
    B = &BREADS[i];
    set_bands();
}

// ---------- particles, flying toast, the plate and the cat ----------
typedef struct { s32 x, y, vx, vy; u16 life, tile, w; } Part;
#define MAX_PARTS 20
static Part parts[MAX_PARTS];

static void spawn(s16 x, s16 y, s16 vx, s16 vy, u16 life, u16 tile, u16 w) {  // speeds in 8.8 pixels per frame
    for (int i = 0; i < MAX_PARTS; i++) if (!parts[i].life) {
        Part *p = &parts[i];
        p->x = (s32)x << 16; p->y = (s32)y << 16; p->vx = (s32)vx << 8; p->vy = (s32)vy << 8;
        p->life = life; p->tile = tile; p->w = w;
        return;
    }
}

typedef struct { s32 x, y, vx, vy; u16 t, len, band, spin, delay; } Fly;
static Fly flying[2];
static u16 flying_n;
static u8 plate[7];
static u16 plate_n;

static void launch(u16 k, u16 band, u16 burnt) {
    Fly *f = &flying[k];
    s16 x0 = k ? A->slot1 : A->slot0, y0 = A->slot_y - 8;
    s16 yt = PY - 5 - 3 * (plate_n + k > 6 ? 6 : plate_n + k);
    s32 vy = -(s32)T->launch * (burnt ? 60 : 96 + 8 * k) / 100 * 65536 / 60;
    s32 y = (s32)y0 << 16, v = vy;
    u16 frames = 0;
    while (frames < 240) { y += v; v += GRAVITY; frames++; if (v > 0 && y >= ((s32)yt << 16)) break; }
    f->x = (s32)x0 << 16; f->y = (s32)y0 << 16; f->vy = vy;
    f->vx = ((s32)(PX - x0) << 16) / frames;
    f->len = frames; f->t = 0; f->band = band; f->spin = k; f->delay = k * 4;
}

typedef struct { s32 x; s16 dir; u16 state, t, carry; u8 stop, sat, fast; } Cat;
enum { CAT_AWAY, CAT_WALK, CAT_SIT, CAT_BAT };
static Cat kitty;
static u16 cat_timer = 900;

static void cat_update(void) {
    Cat *c = &kitty;
    if (c->state == CAT_AWAY) {
        if (--cat_timer) return;
        c->dir = rnd() & 1 ? 1 : -1;
        c->x = (s32)(c->dir > 0 ? -30 : 324) << 16;
        c->state = CAT_WALK; c->t = 0; c->stop = plate_n > 0; c->sat = 0; c->carry = 0; c->fast = 0;
        sfx_meow();
        return;
    }
    c->t++;
    if (c->state == CAT_WALK) {
        c->x += c->dir * (c->fast ? 52429 : 21845);  // 48 or 20 pixels a second
        s16 x = c->x >> 16;
        if (c->stop && !c->sat && (c->dir > 0 ? x >= PX - 32 : x <= PX + 3)) { c->state = CAT_SIT; c->t = 0; c->sat = 1; }
        if (x < -34 || x > 330) { c->state = CAT_AWAY; cat_timer = 2100 + rnd() % 3000; }
    } else if (c->state == CAT_SIT && c->t > 156) {
        u16 roll = rnd() % 100;
        if (plate_n >= 2 && roll < 30) { c->state = CAT_BAT; c->t = 0; return; }  // knocks the whole pile off
        if (plate_n && roll < 65) {
            c->carry = plate[--plate_n];
            say("OI!", PX, 90, 0, 0);
            sfx_meow();
        }
        c->state = CAT_WALK; c->t = 0;
    } else if (c->state == CAT_BAT) {
        if (c->t == 18) {
            for (u16 i = 0; i < plate_n; i++)
                spawn(PX - 8, PY - 4 - i * 3, c->dir * (150 + rnd() % 200), -200 - rnd() % 250 - i * 20, 90, plate_tiles[plate[i] - 1], 2);
            plate_n = 0;
            say("NOOO!", PX - 6, 90, 0, 0);
            for (u16 i = 0; i < 6; i++) note(1, 240 + rnd() % 260, 120, 3, 3, 15 + i * 4);
        }
        if (c->t > 66) { c->state = CAT_WALK; c->t = 0; c->fast = 1; sfx_meow(); }
    }
}

static void cat_draw(void) {
    Cat *c = &kitty;
    if (c->state == CAT_AWAY) return;
    s16 x = c->x >> 16, y = PY - 1 - 15;
    u16 walk = c->state == CAT_WALK ? (c->t / (c->fast ? 5 : 10)) % 2 : 0;
    u16 hf = c->dir < 0;
    if (c->carry) spr_add(hf ? x - 5 : x + 25, y + 7, 2, 1, ATTR(PAL_HUD, 0, 0, 0, plate_tiles[c->carry - 1]));
    spr_add(hf ? x - 4 : x, y, 4, 2, ATTR(PAL_HUD, 0, 0, hf, walk ? CAT_TILE1 : CAT_TILE0));
}

// ---------- a round ----------
static const char *judge(s32 d, u16 *base, u16 *colour, u16 *perfect) {
    s32 diff = d - ((s32)CENTER << 8);
    if (diff < 0) diff = -diff;
    s32 w = window();
    *perfect = 0;
    if (d >= (100L << 8)) { *base = 0; *colour = RGB(4, 4, 4); return "BURNT"; }
    if (diff <= w / 2) { *base = 10; *colour = RGB(7, 6, 2); *perfect = 1; return "PERFECT!"; }
    if (diff <= w * 5 / 4) { *base = 5; *colour = RGB(7, 5, 2); return "GOLDEN"; }
    if (d < ((s32)CENTER << 8)) {
        if (d < (30L << 8)) { *base = 0; *colour = RGB(6, 6, 7); return "STILL BREAD"; }
        if (d < (50L << 8)) { *base = 2; *colour = RGB(7, 7, 6); return "PALE"; }
        *base = 3; *colour = RGB(7, 6, 4); return "LIGHT";
    }
    if (d < (92L << 8)) { *base = 3; *colour = RGB(6, 4, 2); return "DARK"; }
    *base = 1; *colour = RGB(5, 3, 2); return "VERY DARK";
}

static void push(void) {
    state = TOASTING; doneness = 0; lever_to = 256; beeped = 0;
    note(0, 140, 110, 5, 2, 0);
    note(1, 900, 900, 2, 4, 3);
}

static void pop(int burnt) {
    u16 base, colour, perfect;
    const char *word = judge(doneness, &base, &colour, &perfect);
    u16 L = level;
    state = FLYING; lever_to = 0;
    save.rounds++;
    // from level 7, only a perfect moves you on
    if (L > 6 && !perfect) level_round = 0; else level_round++;
    u16 m = 0;
    if (perfect) {
        streak++; save.perfects++;
        m = streak - 1 > 4 ? 4 : streak - 1;
        if (streak > save.best) save.best = streak;
        if (streak > run_best) run_best = streak;
    } else { streak = 0; lives--; }  // breaking the streak costs a life

    // TP: pay, spreads, streak and level, in tenths of a TP
    u32 g = (u32)base * B->pay10;
    for (u16 i = 0; i < SPREAD_COUNT; i++) if (save.owned_spreads & (1 << i)) g = g * SPREADS[i].mult10 / 10;
    u32 gain = (g * (4 + m) * (4 + lvl()) + 80) / 160;
    save.tp += gain;
    // score: separate from TP; harder breads score more
    u32 pts = perfect ? (u32)100 * L * B->score10 * (4 + m) / 40 : base == 5 ? (u32)30 * L * B->score10 / 10 : 0;
    score += pts;

    u16 band = burnt ? 8 : band_of(doneness);
    launch(0, band, burnt); launch(1, band, burnt); flying_n = 2;
    for (int i = 0; i < 6; i++)
        spawn(A->slot0 + (i & 1) * 18, A->slot_y, (s16)(rnd() % 160) - 80, -100 - rnd() % 120, 40, CRUMB_TILE, 1);

    s16 top = A->top;
    say(word, 150, top - 40, 1, colour);
    char line[24], b[12], *p = line;
    *p = 0;
    if (gain) { p = cat(p, "+"); p = cat(p, num(gain, b + 11)); p = cat(p, " TP"); }
    if (pts) { p = cat(p, gain ? "  +" : "+"); p = cat(p, num(pts, b + 11)); p = cat(p, " PTS"); }
    if (line[0]) say(line, 150, top - 20, 0, 0);
    if (!perfect) say(lives ? "LIFE LOST" : "NO LIVES LEFT", 150, top - 56, 0, 0);
    else if (streak > 1) {
        p = line; p = cat(p, "STREAK "); cat(p, num(streak, b + 11));
        say(line, 150, top - 56, 0, 0);
    }

    note(0, 260, 700, 8, 2, 0);
    if (perfect) {
        note(1, 1319, 1319, 11, 3, 0); note(2, 1760, 1760, 18, 3, 5);
        for (int i = 0; i < 8; i++) spawn(150, top - 6, (s16)(rnd() % 256) - 128, -(s16)(rnd() % 160), 30, STAR_TILE, 1);
    } else {
        if (burnt) for (int i = 0; i < 4; i++) note(1, 2794, 2794, 8, 3, i * 15);
        else if (base >= 5) note(1, 1047, 1047, 12, 3, 0);
        else note(1, 330, 330, 7, 3, 0);
        note(0, 392, 392, 7, 3, 18); note(0, 262, 262, 15, 3, 25);  // a life lost
    }

    if (!lives) game_over_pending = 1;
    else if (level_round >= ROUNDS_PER_LEVEL) {
        score += 1000 * L + 500 * lives;  // clearing a level
        if (L >= MAX_LEVEL) won_pending = 1;
        else {
            level++; level_round = 0; lives = LIVES;
            static const u16 up[5] = {523, 659, 784, 1047, 1319};
            jingle(up, 5, 5, 78);
            say("LEVEL UP!", 160, 34, 1, RGB(3, 7, 3));
        }
    }
    save_write();
    hud_dirty = 1;
}

// ---------- panels: the shop, the results and the title ----------
static void panel(u16 y0, u16 y1) {
    for (u16 y = y0; y <= y1; y++) {
        vram_write_addr(VRAM_PLANE_A + y * 128);
        for (u16 x = 0; x < 40; x++) VDP_DATA = ATTR(PAL_HUD, 1, 0, 0, y == y0 ? PANEL_TOP : PANEL_TILE);
    }
}

static void wrap(u16 x, u16 y, u16 width, u16 rows, const char *s, u16 font) {  // word-wrapped panel text
    for (u16 r = 0; r < rows; r++) {
        char line[41];
        u16 n = 0, last_space = 0;
        while (s[n] && n < width) { if (s[n] == ' ') last_space = n; n++; }
        if (s[n] && last_space) n = last_space;
        for (u16 i = 0; i < width; i++) line[i] = i < n ? s[i] : ' ';
        line[width] = 0;
        print(x, y + r, line, font);
        s += n;
        while (*s == ' ') s++;
    }
}

// the shop
static u16 tab, cursor;
static const char *shop_msg;
static const char *TABS[4] = {"TOASTERS", "BREADS", "SPREADS", "SCORES"};

static u16 tab_count(void) { return tab == 0 ? MODEL_COUNT : tab == 1 ? BREAD_COUNT : tab == 2 ? SPREAD_COUNT : 0; }
static u8 *owned_mask(void) { return tab == 0 ? &save.owned_toasters : tab == 1 ? &save.owned_breads : &save.owned_spreads; }
static const char *item_name(u16 i) { return tab == 0 ? TOASTERS[i].name : tab == 1 ? BREADS[i].name : SPREADS[i].name; }
static u32 item_cost(u16 i) { return tab == 0 ? TOASTERS[i].cost : tab == 1 ? BREADS[i].cost : SPREADS[i].cost; }

static void draw_shop(void) {
    char line[41], b[16], *p;
    panel(2, 25);
    print(2, 3, "SHOP", FONT_PANEL_GOLD);
    p = line; p = cat(p, num(save.tp, b + 15)); cat(p, " TP");
    print_right(38, 3, line, FONT_PANEL);
    u16 x = 2;
    for (u16 t = 0; t < 4; t++) {
        p = line;
        p = cat(p, t == tab ? "<" : " "); p = cat(p, TABS[t]); cat(p, t == tab ? ">" : " ");
        print(x, 5, line, t == tab ? FONT_PANEL_GOLD : FONT_PANEL_DIM);
        x += slen(TABS[t]) + 3;
    }
    if (tab == 3) {
        print(2, 7, "HIGH SCORES", FONT_PANEL_GOLD);
        for (u16 i = 0; i < 5; i++) {
            HiScore *h = &save.hi[i];
            if (!h->score) { if (!i) print(2, 9, "NONE YET. FINISH A RUN TO GET ON HERE.", FONT_PANEL_DIM); break; }
            p = line; *p++ = '1' + i; p = cat(p, ". "); cat(p, num(h->score, b + 15));
            print(3, 9 + i * 2, line, FONT_PANEL);
            p = line;
            if (h->won) cat(p, "ALL CLEAR"); else { p = cat(p, "LEVEL "); cat(p, num(h->level, b + 15)); }
            print_right(37, 9 + i * 2, line, FONT_PANEL_DIM);
        }
        p = line; p = cat(p, "BEST STREAK EVER: "); cat(p, num(save.best, b + 15));
        print(2, 20, line, FONT_PANEL_DIM);
    } else {
        u8 own = *owned_mask();
        u16 current = tab == 0 ? save.toaster : tab == 1 ? save.bread : 0xFF;
        for (u16 i = 0; i < tab_count(); i++) {
            u16 y = 7 + i * 2;
            print(1, y, i == cursor ? ">" : " ", FONT_PANEL_GOLD);
            print(3, y, item_name(i), (own & (1 << i)) ? FONT_PANEL : FONT_PANEL_DIM);
            if (i == current) cat(line, "IN USE");
            else if (own & (1 << i)) cat(line, tab == 2 ? "ON TOAST" : "OWNED");
            else { p = line; p = cat(p, num(item_cost(i), b + 15)); cat(p, " TP"); }
            print_right(38, y, line, (own & (1 << i)) || item_cost(i) <= save.tp ? FONT_PANEL : FONT_PANEL_DIM);
        }
        const char *blurb = tab == 0 ? TOASTERS[cursor].blurb : tab == 1 ? BREADS[cursor].blurb : SPREADS[cursor].blurb;
        wrap(2, 21, 36, 2, blurb, FONT_PANEL);
        p = line;
        if (tab == 0) {
            const Toaster *t = &TOASTERS[cursor];
            u16 sp = (t->speed * 10 + 128) / 256;
            p = cat(p, "SPEED X"); p = cat(p, num(sp / 10, b + 15)); p = cat(p, ".");
            p = cat(p, num(sp % 10, b + 15)); p = cat(p, "  WINDOW "); p = cat(p, num(t->win, b + 15));
            if (t->wobble >= 50) p = cat(p, "  PATCHY"); else if (!t->wobble) p = cat(p, "  STEADY");
            if (t->beep) cat(p, "  BEEPS");
        } else if (tab == 1) {
            const Bread *br = &BREADS[cursor];
            p = cat(p, "SCORE X"); p = cat(p, num(br->score10 / 10, b + 15));
            if (br->score10 % 10) { p = cat(p, "."); p = cat(p, num(br->score10 % 10, b + 15)); }
            p = cat(p, "  PAYS X"); p = cat(p, num(br->pay10 / 10, b + 15));
            if (br->pay10 % 10) { p = cat(p, "."); p = cat(p, num(br->pay10 % 10, b + 15)); }
            if (br->surge) p = cat(p, "  UNEVEN");
            if (br->accel) cat(p, "  SPEEDS UP");
        } else {
            p = cat(p, "EVERY ROUND'S TP X"); p = cat(p, num(SPREADS[cursor].mult10 / 10, b + 15));
            if (SPREADS[cursor].mult10 % 10) { p = cat(p, "."); cat(p, num(SPREADS[cursor].mult10 % 10, b + 15)); }
        }
        print(2, 23, line, FONT_PANEL_DIM);
    }
    print(2, 24, shop_msg ? shop_msg : "", FONT_PANEL_GOLD);
    print(2, 25, tab == 3 ? "</>: TABS   B: BACK" : "A: BUY/USE   </>: TABS   B: BACK", FONT_PANEL_DIM);
}

static void open_shop(void) {
    mode = SHOP; tab = 0; cursor = save.toaster; shop_msg = 0;
    note(1, 660, 990, 6, 3, 0);
    draw_shop();
}

static void close_shop(void) {
    mode = PLAY;
    restore_plane_a(2, 25);
    note(1, 990, 660, 6, 3, 0);
    hud_dirty = 1;
}

static void shop_update(void) {
    if (pressed & (BTN_B | BTN_START)) { close_shop(); return; }
    u16 n = tab_count(), redraw = 0;
    if (pressed & BTN_LEFT) { tab = (tab + 3) % 4; cursor = 0; redraw = 1; }
    if (pressed & BTN_RIGHT) { tab = (tab + 1) % 4; cursor = 0; redraw = 1; }
    if (n && (pressed & BTN_UP)) { cursor = (cursor + n - 1) % n; redraw = 1; }
    if (n && (pressed & BTN_DOWN)) { cursor = (cursor + 1) % n; redraw = 1; }
    if (redraw) { shop_msg = 0; note(1, 1200, 1200, 2, 5, 0); }
    if (n && (pressed & (BTN_A | BTN_C))) {
        u8 *own = owned_mask();
        redraw = 1;
        if (!(*own & (1 << cursor))) {
            if (save.tp < item_cost(cursor)) { shop_msg = "NOT ENOUGH TP YET."; note(1, 196, 147, 10, 3, 0); }
            else {
                save.tp -= item_cost(cursor);
                *own |= 1 << cursor;
                shop_msg = "BOUGHT!";
                static const u16 buy[4] = {523, 659, 784, 1047};
                jingle(buy, 4, 4, 0);
            }
        }
        if (*own & (1 << cursor)) {
            if (tab == 0) use_toaster(cursor);
            if (tab == 1) use_bread(cursor);
            if (!shop_msg) shop_msg = tab == 2 ? "ALREADY ON THE TOAST." : "IN USE.";
        }
        save_write();
    }
    if (redraw) draw_shop();
}

// the results at the end of a run
static u8 results_won;
static u16 results_at;

static void finish_run(u16 won) {
    char line[41], b[16], *p;
    u32 total = score + (u32)run_best * 200;
    if (won) total += 10000 + (u32)lives * 1000;
    u16 rank = 5;
    for (u16 i = 0; i < 5; i++) if (total > save.hi[i].score) { rank = i; break; }
    if (rank < 5) {
        for (u16 i = 4; i > rank; i--) save.hi[i] = save.hi[i - 1];
        save.hi[rank].score = total; save.hi[rank].level = level; save.hi[rank].won = won;
    }
    save_write();

    mode = RESULTS; results_won = won; results_at = frame;
    vdp_set_colour(COL_WORD, won ? RGB(7, 6, 2) : RGB(7, 3, 3));
    panel(5, 23);
    u16 y = 10;
    #define ROW(label, value) do { print(6, y, label, FONT_PANEL_DIM); print_right(34, y, value, FONT_PANEL); y += 2; } while (0)
    ROW("LEVEL REACHED", num(level, b + 15));
    ROW("RUN SCORE", num(score, b + 15));
    p = line; p = cat(p, "BEST STREAK "); cat(p, num(run_best, b + 15));
    char v[16]; v[0] = '+'; cat(v + 1, num((u32)run_best * 200, b + 15));
    ROW(line, v);
    if (won) {
        ROW("ALL CLEAR", "+10,000");
        p = line; p = cat(p, "LIVES LEFT "); cat(p, num(lives, b + 15));
        cat(v + 1, num((u32)lives * 1000, b + 15));
        ROW(line, v);
    }
    print(6, y, "TOTAL", FONT_PANEL_GOLD); print_right(34, y, num(total, b + 15), FONT_PANEL_GOLD); y += 2;
    if (!rank) print(6, y, "NEW HIGH SCORE!", FONT_PANEL_GOLD);
    else { print(6, y, "HIGH SCORE", FONT_PANEL_DIM); print_right(34, y, num(save.hi[0].score, b + 15), FONT_PANEL); }
    if (won) { static const u16 tune[7] = {523, 659, 784, 1047, 784, 1047, 1319}; jingle(tune, 7, 7, 10); }
    else { static const u16 tune[4] = {523, 440, 349, 262}; jingle(tune, 4, 13, 10); }
}

static void new_run(void) {
    level = 1; level_round = 0; lives = LIVES; score = 0; streak = 0; run_best = 0;
    game_over_pending = won_pending = 0;
    state = READY; doneness = 0;
    mode = PLAY;
    restore_plane_a(2, 25);
    hud_dirty = 1;
}

// the title
static void show_title(void) {
    char line[41], b[16], *p;
    mode = TITLE;
    vdp_set_colour(COL_WORD, RGB(7, 6, 2));
    panel(15, 22);
    print_right(39 - 13, 17, "PRESS START", FONT_PANEL_GOLD);
    p = line; p = cat(p, "HIGH SCORE "); cat(p, num(save.hi[0].score, b + 15));
    print(20 - slen(line) / 2, 19, line, FONT_PANEL);
    print(5, 21, "(C) 2026 A TOASTY PRODUCTION", FONT_PANEL_DIM);
    static const u16 tune[8] = {392, 523, 659, 784, 659, 784, 1047, 1047};
    jingle(tune, 8, 8, 20);
}

// ---------- every frame ----------
static void play_update(void) {
    if (game_over_pending || won_pending) {
        if (state == READY) finish_run(won_pending);
    } else if (pressed & BTN_START) {
        if (state == READY) { open_shop(); return; }
    } else if (pressed & (BTN_A | BTN_B | BTN_C)) {
        if (state == READY) push();
        else if (state == TOASTING) pop(0);
    }

    if (state == TOASTING) {
        s16 s = (sin_lut[(phase1 >> 8) & 255] * 154 + sin_lut[(phase2 >> 8) & 255] * 102) >> 8;
        s32 f = 256 + (((s32)T->wobble * s) >> 7);
        if (B->surge) {
            s16 su = sin_lut[(phase3 >> 8) & 255];
            f = f * (141 + (su > 0 ? (s32)B->surge * su * 16 / 1270 : 0)) >> 8;
        }
        if (B->accel) f = f * (102 + (doneness / 50)) >> 8;
        if (f < 38) f = 38;
        doneness += (((((s32)94 * f) >> 8) * level_speed() >> 8) * T->speed >> 8) * B->rate >> 8;
        if (T->beep && !beeped && doneness >= ((s32)CENTER << 8) - window() / 2) { beeped = 1; note(1, 1976, 1976, 3, 3, 0); }
        if (doneness >= (100L << 8)) { doneness = 100L << 8; pop(1); }
    }
}

static void world_update(void) {
    lever += (lever_to - lever) / (lever_to ? 3 : 2);
    if (lever_to - lever < 3 && lever_to - lever > -3) lever = lever_to;
    phase1 += 400; phase2 += 991; phase3 += 529;

    for (u16 k = 0; k < 2; k++) {
        Fly *f = &flying[k];
        if (!f->len) continue;
        if (f->delay) { f->delay--; continue; }
        f->t++;
        f->x += f->vx; f->y += f->vy; f->vy += GRAVITY;
        if (f->t >= f->len) {
            f->len = 0;
            if (plate_n == 7) { for (int i = 0; i < 6; i++) plate[i] = plate[i + 1]; plate_n--; }
            plate[plate_n++] = f->band;
            note(2, 180, 110, 3, 4, 0);
            if (!--flying_n) state = READY;
        }
    }
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (!p->life) continue;
        p->life--;
        p->x += p->vx; p->y += p->vy;
        if (p->tile == CRUMB_TILE) {
            p->vy += 12 << 8;
            s32 floor = (s32)(A->top + A->h - 1) << 16;
            if (p->y > floor) { p->y = floor; p->vy = 0; p->vx = p->vx * 3 / 4; }
        } else if (p->w == 2) p->vy += 37 << 8;  // the cat's handiwork, falling off the worktop
        else { p->vx = p->vx * 15 / 16; p->vy = p->vy * 15 / 16; }
    }
    cat_update();
    if (++save.clock >= 14400) save.clock = 0;
}

static void build_sprites(void) {
    spr_clear();
    if (mode == SHOP) return;  // the shop covers everything
    if (mode == TITLE) {
        sprite_text("TOAST", 160 - 40, 44, 1);
        sprite_text("SIMULATOR", 160 - 72, 64, 1);
    }
    if (mode == RESULTS) {
        const char *t = results_won ? "ALL CLEAR!" : "GAME OVER";
        sprite_text(t, 160 - slen(t) * 8, 50, 1);
        if (frame - results_at > 60 && (frame / 20) % 2) sprite_text("PRESS A TO PLAY AGAIN", 160 - 84, 206 - 16, 0);
    }
    draw_texts();
    if (mode == PLAY && save.rounds == 0 && state == READY && (frame / 30) % 2) {
        sprite_text("PRESS A TO TOAST", 160 - 64, 64, 0);
        sprite_text("START FOR THE SHOP", 160 - 72, 76, 0);
    }
    // the meter: brackets and needle
    s32 w = window();
    s16 a = 80 + (((s32)CENTER << 8) - w / 2) * 231 / (100L << 8);
    s16 z = 80 + (((s32)CENTER << 8) + w / 2) * 231 / (100L << 8);
    spr_add(a - 1, 3, 1, 2, ATTR(PAL_HUD, 1, 0, 0, BRACKET_TILE));
    spr_add(z - 6, 3, 1, 2, ATTR(PAL_HUD, 1, 0, 1, BRACKET_TILE));
    s16 nx = 80 + (state == TOASTING ? doneness * 231 / (100L << 8) : 0);
    spr_add(nx - 2, 4, 1, 2, ATTR(PAL_HUD, 1, 0, 0, NEEDLE_TILE));
    if (mode != PLAY) goto behind;  // the panels cover the toaster
    // the lever's knob, in front of the toaster
    spr_add(A->x + A->w - 12, A->top + 8 + ((A->h - 21) * lever >> 8), 2, 1, ATTR(KNOB_PAL, 1, 0, 0, KNOB_TILE));
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life) spr_add(p->x >> 16, p->y >> 16, p->w, 1, ATTR(PAL_HUD, 1, 0, 0, p->tile));
    }
    for (u16 k = 0; k < 2; k++) {
        Fly *f = &flying[k];
        if (!f->len || f->delay) continue;
        u16 q = (f->t / 4) % 4;
        if (f->spin) q = (4 - q) % 4;
        spr_add((f->x >> 16) - 8, (f->y >> 16) - 8, 2, 2, ATTR(PAL_HUD, 0, 0, 0, fly_tiles[(f->band - 1) * 4 + q]));
    }
behind:
    // the toast in the slots, behind the toaster's front
    if (state != FLYING) {
        s16 vis = 11 - (8 * lever >> 8);
        spr_add(A->slot0 - 8, A->slot_y + 2 - vis, 2, 2, ATTR(PAL_HUD, 0, 0, 0, SLOT_SLICE_TILE));
        spr_add(A->slot1 - 8, A->slot_y + 2 - vis, 2, 2, ATTR(PAL_HUD, 0, 0, 0, SLOT_SLICE_TILE));
    }
    for (u16 i = 0; i < plate_n; i++)
        spr_add(PX - 8, PY - 4 - i * 3, 2, 1, ATTR(PAL_HUD, 0, 0, 0, plate_tiles[plate[i] - 1]));
    cat_draw();
}

int main(void) {
    vdp_init();
    pad_init();
    for (u16 ch = 0; ch < 4; ch++) psg_vol(ch, 15);
    save_load();
    rnd_state ^= save.rounds * 31 + save.clock;
    if (!rnd_state) rnd_state = 1;

    vdp_load_tiles(0, tile_data, TILE_COUNT);
    vdp_load_pal(PAL_HUD, hud_pal);
    vdp_set_colour(0, RGB(0, 0, 1));  // backdrop: navy
    vdp_load_map(VRAM_PLANE_B, plane_b_map);
    vdp_load_map(VRAM_PLANE_A, plane_a_map);
    print(1, 1, "DONENESS", FONT_LABEL);
    for (u16 i = 0; i < 29; i++) vdp_set_tile(VRAM_PLANE_A, 10 + i, 1, ATTR(PAL_HUD, 0, 0, 0, BAR_TILE + i));
    use_toaster(save.toaster);
    use_bread(save.bread);
    apply_light();
    draw_status();
    show_title();

    build_sprites();
    spr_upload();
    vdp_display(1);
    __asm__ volatile("move.w #0x2000, %sr");  // interrupts on

    for (;;) {
        wait_vblank();
        spr_upload();
        RGB3 c = toast_col(state == TOASTING ? doneness : 0);
        vdp_set_colour(COL_BODY, md_colour(c));
        vdp_set_colour(COL_CRUST, md_colour(darker(c)));
        if ((frame & 7) == 0) apply_light();
        if (hud_dirty) { draw_status(); hud_dirty = 0; }
        if (mode == TITLE) print_right(26, 17, (frame / 20) % 2 ? "PRESS START" : "           ", FONT_PANEL_GOLD);
        sound_update();

        frame++;
        pad_prev = pad;
        pad = pad_read();
        pressed = pad & ~pad_prev;
        if (mode == TITLE) { if (pressed & (BTN_START | BTN_A | BTN_B | BTN_C)) new_run(); }
        else if (mode == SHOP) shop_update();
        else if (mode == RESULTS) { if (frame - results_at > 60 && (pressed & (BTN_A | BTN_B | BTN_C | BTN_START))) new_run(); }
        else play_update();
        world_update();
        if ((frame & 1023) == 0) save_write();  // keep the time of day
        build_sprites();
    }
}
