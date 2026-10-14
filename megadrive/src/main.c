// Toast Simulator for the Sega Mega Drive.
// Push the lever, watch the needle, pop the toast in the yellow brackets.
#include "md.h"
#include "assets.h"

#define CENTER 72
#define ROUNDS_PER_LEVEL 6
#define PX 263                  // the plate, screen pixels
#define PY (129 + SCENE_OY)
// Positions are 16.16 fixed point: GCC's 68000 code reads the top half of a long at an aligned address,
// but turns an 8.8 value's ">> 8" into a misaligned word read, which crashes a real 68000.
#define GRAVITY 9466            // pixels per frame per frame, 16.16 (520 px/s/s)
#define TOAST_LAUNCH 165        // Nan's Pop-Up, pixels per second
#define WOBBLE 115              // Nan's patchy heat, 0.45 in 8.8

#define PAL_HUD 3
#define COL_WORD (PAL_HUD * 16 + 11)
#define COL_BODY (PAL_HUD * 16 + 14)
#define COL_CRUST (PAL_HUD * 16 + 15)

enum { READY, TOASTING, FLYING };

typedef struct { u8 r, g, b; } RGB3;
#define RGB(r, g, b) (((b) << 9) | ((g) << 5) | ((r) << 1))  // 0-7 per channel  // 0-7 per channel, the Mega Drive's own range

static u16 state, frame, rounds;
static s32 doneness;               // 0-100, 8.8 fixed point
static s16 lever, lever_to;        // 0-256
static u16 phase1, phase2;
static u16 level = 1, level_round, streak, best;
static u32 tp;
static u16 pad, pad_prev;
static u8 hud_dirty = 1;

// ---------- colour ----------
static const RGB3 WHITE_RAW = {7, 7, 6};
static const struct { u8 d; RGB3 c; } STOPS[5] = {
    {45, {7, 6, 4}}, {CENTER, {6, 4, 2}}, {86, {4, 2, 1}}, {96, {2, 1, 0}}, {100, {1, 1, 1}},
};

static u16 md_colour(RGB3 c) { return (c.b << 9) | (c.g << 5) | (c.r << 1); }

// the toast's colour at a doneness (8.8), as in the browser version
static RGB3 toast_col(s32 d) {
    RGB3 prev = WHITE_RAW;
    s32 prev_d = 0;
    for (int i = 0; i < 5; i++) {
        s32 sd = (s32)STOPS[i].d << 8;
        if (d <= sd) {
            s32 t = ((d - prev_d) << 8) / (sd - prev_d);  // 0-256
            RGB3 a = prev, b = STOPS[i].c, out;
            out.r = (a.r * 256 + (b.r - a.r) * t + 128) >> 8;
            out.g = (a.g * 256 + (b.g - a.g) * t + 128) >> 8;
            out.b = (a.b * 256 + (b.b - a.b) * t + 128) >> 8;
            return out;
        }
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

// ---------- levels ----------
static u16 lvl(void) { return (level > 15 ? 15 : level) - 1; }
static u16 level_speed(void) { return 256 + 38 * lvl(); }                       // 8.8
static u16 window(void) {                                                         // the perfect window, 8.8
    u16 w = 256 - 13 * lvl();
    if (w < 141) w = 141;
    return (9 * w);  // Nan's Pop-Up: 9 doneness wide
}

// ---------- sound ----------
typedef struct { u16 start, len, p0, p1; u8 ch, vol; } Note;
#define MAX_NOTES 16
static Note notes[MAX_NOTES];

static void note(u16 ch, u16 hz0, u16 hz1, u16 len, u16 vol, u16 delay) {
    for (int i = 0; i < MAX_NOTES; i++) {
        Note *n = &notes[i];
        if (n->len && (u16)(frame - n->start) < n->len) continue;  // still playing or waiting
        if (n->len && (s16)(n->start - frame) > 0) continue;
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
        psg_vol(ch, cur->vol + (15 - cur->vol) * t / cur->len);  // fades out
    }
}

// ---------- text, as sprites ----------
typedef struct { const char *s; char buf[24]; s32 y; u16 x, life, big; u16 colour; } Text;
#define MAX_TEXTS 4
static Text texts[MAX_TEXTS];

static u16 glyph(char c) {
    for (u16 i = 0; font_chars[i]; i++) if (font_chars[i] == c) return i;
    return 0;
}

static void say(const char *s, u16 cx, u16 y, u16 big, u16 colour) {
    Text *t = &texts[0];
    for (int i = 0; i < MAX_TEXTS; i++) if (!texts[i].life) { t = &texts[i]; break; }
    u16 n = 0;
    while (s[n] && n < 23) { t->buf[n] = s[n]; n++; }
    t->buf[n] = 0;
    t->s = t->buf;
    t->x = cx - n * (big ? 8 : 4);
    t->y = (s32)y << 16;
    t->life = 84;  // 1.4 seconds
    t->big = big;
    t->colour = colour;
    if (big) vdp_set_colour(COL_WORD, colour);
}

static void draw_texts(void) {
    for (int i = 0; i < MAX_TEXTS; i++) {
        Text *t = &texts[i];
        if (!t->life) continue;
        t->life--;
        t->y -= 60 << 8;  // rises 14 pixels a second
        if (t->life < 15 && (t->life / 2) % 2) continue;  // blinks as it goes
        s16 y = t->y >> 16;
        for (u16 k = 0; t->s[k]; k++) {
            if (t->s[k] == ' ') continue;
            u16 g = glyph(t->s[k]);
            if (t->big) spr_add(t->x + k * 16, y, 2, 2, ATTR(PAL_HUD, 1, 0, 0, FONT_BIG + g * 4));
            else spr_add(t->x + k * 8, y, 1, 1, ATTR(PAL_HUD, 1, 0, 0, FONT_SMALL + g));
        }
    }
}

// a number with thousands commas
static char *num(u32 n, char *end) {
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

static char *cat(char *d, const char *s) { while (*s) *d++ = *s++; *d = 0; return d; }

// plane A text, for the HUD and the status bar
static void print(u16 x, u16 y, const char *s, u16 first) {
    for (; *s; s++, x++) vdp_set_tile(VRAM_PLANE_A, x, y, *s == ' ' ? 0 : ATTR(PAL_HUD, 1, 0, 0, first + glyph(*s)));
}

static void draw_status(void) {
    char line[41], b[16], *p = line;
    p = cat(p, " TP "); p = cat(p, num(tp, b + 15));
    p = cat(p, "  LEVEL "); p = cat(p, num(level, b + 15)); p = cat(p, " ");
    p = cat(p, num(level_round, b + 15)); p = cat(p, "/6");
    p = cat(p, "  STREAK "); p = cat(p, num(streak, b + 15));
    while (p < line + 40) *p++ = ' ';
    *p = 0;
    print(0, 26, line, FONT_SMALL);
}

// ---------- particles and flying toast ----------
typedef struct { s32 x, y, vx, vy; u16 life, tile, pal; } Part;
#define MAX_PARTS 16
static Part parts[MAX_PARTS];

static void spawn(s16 x, s16 y, s16 vx, s16 vy, u16 life, u16 tile, u16 pal) {
    for (int i = 0; i < MAX_PARTS; i++) if (!parts[i].life) {
        Part *p = &parts[i];
        p->x = (s32)x << 16; p->y = (s32)y << 16; p->vx = (s32)vx << 8; p->vy = (s32)vy << 8; p->life = life; p->tile = tile; p->pal = pal;
        return;
    }
}

static u16 rnd_state = 12345;
static u16 rnd(void) { rnd_state ^= rnd_state << 7; rnd_state ^= rnd_state >> 9; rnd_state ^= rnd_state << 8; return rnd_state; }

typedef struct { s32 x, y, vx, vy; u16 t, len, band, spin, delay; s16 tx, ty; } Fly;
static Fly flying[2];
static u16 flying_n;
static u8 plate[7];
static u16 plate_n;

static void launch(u16 k, u16 band) {
    Fly *f = &flying[k];
    s16 x0 = k ? SLOT1_X : SLOT0_X, y0 = SLOT_Y - 8;
    s16 yt = PY - 5 - 3 * (plate_n + k > 6 ? 6 : plate_n + k);
    s32 vy = -(s32)TOAST_LAUNCH * (96 + 8 * k) / 100 * 65536 / 60;
    // flight time: step the fall until it reaches the pile
    s32 y = (s32)y0 << 16, v = vy;
    u16 frames = 0;
    while (frames < 240) { y += v; v += GRAVITY; frames++; if (v > 0 && y >= ((s32)yt << 16)) break; }
    f->x = (s32)x0 << 16; f->y = (s32)y0 << 16; f->vy = vy;
    f->vx = ((s32)(PX - x0) << 16) / frames;
    f->len = frames; f->t = 0; f->band = band; f->spin = k; f->delay = k * 4;
    f->tx = PX; f->ty = yt;
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
    state = TOASTING; doneness = 0; lever_to = 256;
    note(0, 140, 110, 5, 2, 0);
    note(1, 900, 900, 2, 4, 3);
}

static void pop(int burnt) {
    u16 base, colour, perfect;
    const char *word = judge(doneness, &base, &colour, &perfect);
    state = FLYING; lever_to = 0; rounds++;
    // from level 7 only a perfect moves you on
    if (level > 6 && !perfect) level_round = 0; else level_round++;
    u16 m = 0;
    if (perfect) { streak++; m = streak - 1 > 4 ? 4 : streak - 1; if (streak > best) best = streak; }
    else streak = 0;
    u32 gain = ((u32)base * (4 + m) * (4 + lvl()) + 8) / 16;  // streak and level bonuses
    tp += gain;

    u16 band = burnt ? 8 : band_of(doneness);
    launch(0, band); launch(1, band); flying_n = 2;
    for (int i = 0; i < 6; i++) {
        spawn(SLOT0_X + (i & 1) * 18, SLOT_Y, (s16)(rnd() % 160) - 80, -100 - rnd() % 120, 40, CRUMB_TILE, PAL_HUD);
    }

    say(word, 150, TOASTER_TOP - 40, 1, colour);
    if (gain) {
        char line[16], b[12], *p = line;
        p = cat(p, "+"); p = cat(p, num(gain, b + 11)); cat(p, " TP");
        say(line, 150, TOASTER_TOP - 20, 0, 0);
    }
    if (perfect && streak > 1) {
        char line[16], b[8], *p = line;
        p = cat(p, "STREAK "); cat(p, num(streak, b + 7));
        say(line, 150, TOASTER_TOP - 52, 0, 0);
    }

    note(0, 260, 700, 8, 2, 0);
    if (perfect) {
        note(1, 1319, 1319, 11, 3, 0); note(2, 1760, 1760, 18, 3, 5);
        for (int i = 0; i < 8; i++) spawn(150, TOASTER_TOP - 6, (s16)(rnd() % 256) - 128, -(s16)(rnd() % 160), 30, STAR_TILE, PAL_HUD);
    } else if (burnt) {
        for (int i = 0; i < 4; i++) note(1, 2794, 2794, 8, 3, i * 15);
    } else if (base >= 5) note(1, 1047, 1047, 12, 3, 0);
    else note(1, 330, 330, 7, 3, 0);

    if (level_round >= ROUNDS_PER_LEVEL) {
        level++; level_round = 0;
        for (int i = 0; i < 5; i++) note(2, 523 + i * 200, 523 + i * 200, 6, 3, 78 + i * 5);
        say("LEVEL UP!", 160, 40, 1, RGB(3, 7, 3));
    }
    hud_dirty = 1;
}

static void update(void) {
    u16 pressed = pad & ~pad_prev;
    if (pressed & (BTN_A | BTN_B | BTN_C | BTN_START)) {
        if (state == READY) push();
        else if (state == TOASTING) pop(0);
    }

    lever += (lever_to - lever) / (lever_to ? 3 : 2);
    if (lever_to - lever < 3 && lever_to - lever > -3) lever = lever_to;

    phase1 += 400; phase2 += 991;
    if (state == TOASTING) {
        s16 s = (sin_lut[(phase1 >> 8) & 255] * 154 + sin_lut[(phase2 >> 8) & 255] * 102) >> 8;
        s32 f = 256 + (((s32)WOBBLE * s) >> 7);
        if (f < 38) f = 38;
        doneness += ((s32)94 * f >> 8) * level_speed() >> 8;
        if (doneness >= (100L << 8)) { doneness = 100L << 8; pop(1); }
    }

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
        if (p->tile == CRUMB_TILE) { p->vy += 12 << 8; if (p->y > ((s32)(TOASTER_TOP + 33) << 16)) { p->y = (s32)(TOASTER_TOP + 33) << 16; p->vy = 0; p->vx = p->vx * 3 / 4; } }
        else { p->vx = p->vx * 15 / 16; p->vy = p->vy * 15 / 16; }
    }
}

static void build_sprites(void) {
    spr_clear();
    draw_texts();
    if (rounds == 0 && state == READY && (frame / 30) % 2) {
        static const char *prompt = "PRESS A TO TOAST";
        for (u16 k = 0; prompt[k]; k++)
            if (prompt[k] != ' ') spr_add(160 - 64 + k * 8, 64, 1, 1, ATTR(PAL_HUD, 1, 0, 0, FONT_SMALL + glyph(prompt[k])));
    }
    // the meter: brackets and needle
    s32 w = window();
    s16 a = 80 + (((s32)CENTER << 8) - w / 2) * 231 / (100L << 8);
    s16 z = 80 + (((s32)CENTER << 8) + w / 2) * 231 / (100L << 8);
    spr_add(a - 1, 3, 1, 2, ATTR(PAL_HUD, 1, 0, 0, BRACKET_TILE));
    spr_add(z - 6, 3, 1, 2, ATTR(PAL_HUD, 1, 0, 1, BRACKET_TILE));
    s16 nx = 80 + (state == TOASTING ? doneness * 231 / (100L << 8) : 0);
    spr_add(nx - 2, 4, 1, 2, ATTR(PAL_HUD, 1, 0, 0, NEEDLE_TILE));
    // the lever's knob, in front of the toaster
    spr_add(LEVER_X - 3, TOASTER_TOP + 8 + (13 * lever >> 8), 2, 1, ATTR(KNOB_PAL, 1, 0, 0, KNOB_TILE));
    for (int i = 0; i < MAX_PARTS; i++) {
        Part *p = &parts[i];
        if (p->life) spr_add(p->x >> 16, p->y >> 16, 1, 1, ATTR(p->pal, 1, 0, 0, p->tile));
    }
    for (u16 k = 0; k < 2; k++) {
        Fly *f = &flying[k];
        if (!f->len || f->delay) continue;
        u16 q = (f->t / 4) % 4;
        if (f->spin) q = (4 - q) % 4;
        spr_add((f->x >> 16) - 8, (f->y >> 16) - 8, 2, 2, ATTR(PAL_HUD, 0, 0, 0, fly_tiles[(f->band - 1) * 4 + q]));
    }
    // the toast in the slots, behind the toaster's front
    if (state != FLYING) {
        s16 vis = 11 - (8 * lever >> 8);
        spr_add(SLOT0_X - 8, SLOT_Y + 2 - vis, 2, 2, ATTR(PAL_HUD, 0, 0, 0, SLOT_SLICE_TILE));
        spr_add(SLOT1_X - 8, SLOT_Y + 2 - vis, 2, 2, ATTR(PAL_HUD, 0, 0, 0, SLOT_SLICE_TILE));
    }
    for (u16 i = 0; i < plate_n; i++)
        spr_add(PX - 8, PY - 4 - i * 3, 2, 1, ATTR(PAL_HUD, 0, 0, 0, plate_tiles[plate[i] - 1]));
}

int main(void) {
    vdp_init();
    pad_init();
    for (u16 ch = 0; ch < 4; ch++) psg_vol(ch, 15);

    vdp_load_tiles(0, tile_data, TILE_COUNT);
    for (u16 p = 0; p < 3; p++) vdp_load_pal(p, scene_pal[p]);
    vdp_load_pal(PAL_HUD, hud_pal);
    vdp_set_colour(0, RGB(0, 0, 1));  // backdrop: navy
    set_bands();
    vdp_load_map(VRAM_PLANE_B, plane_b_map);
    vdp_load_map(VRAM_PLANE_A, plane_a_map);
    print(1, 1, "DONENESS", FONT_LABEL);
    for (u16 i = 0; i < 29; i++) vdp_set_tile(VRAM_PLANE_A, 10 + i, 1, ATTR(PAL_HUD, 0, 0, 0, BAR_TILE + i));

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
        if (hud_dirty) { draw_status(); hud_dirty = 0; }
        sound_update();

        frame++;
        pad_prev = pad;
        pad = pad_read();
        update();
        build_sprites();
    }
}
