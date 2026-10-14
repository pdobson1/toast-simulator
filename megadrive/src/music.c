#include "music.h"

#define STEP 6  // frames per sixteenth note: 150 beats a minute

// ---------- instruments ----------
// per operator, in register order (slot 1, 3, 2, 4): DT/MUL, TL, RS/AR, AM/D1R, D2R, SL/RR
typedef struct { u8 fb_alg; u8 op[4][6]; } Patch;

static const Patch BASS = {(5 << 3) | 4, {
    {0x01, 30, 0x1F, 12, 2, 0x2F},   // modulator: a pluck of brightness
    {0x02, 44, 0x1F, 10, 2, 0x3F},
    {0x01, 18, 0x1F, 6, 2, 0x37},    // carrier
    {0x00, 24, 0x1F, 6, 2, 0x37},    // carrier, half speed: the sub
}};
static const Patch LEAD = {(3 << 3) | 4, {
    {0x01, 36, 0x1F, 8, 0, 0x26},
    {0x33, 46, 0x1F, 10, 0, 0x56},
    {0x01, 22, 0x1F, 4, 1, 0x26},
    {0x71, 28, 0x1F, 4, 1, 0x26},
}};
static const Patch ARP = {(0 << 3) | 7, {  // a little organ, all carriers
    {0x01, 34, 0x1F, 10, 4, 0x48},
    {0x04, 52, 0x1F, 12, 4, 0x48},
    {0x02, 44, 0x1F, 10, 4, 0x48},
    {0x00, 127, 0x1F, 0, 0, 0xFF},
}};
static const Patch KICK = {(0 << 3) | 7, {
    {0x00, 127, 0x1F, 0, 0, 0xFF},
    {0x00, 127, 0x1F, 0, 0, 0xFF},
    {0x00, 127, 0x1F, 0, 0, 0xFF},
    {0x01, 6, 0x1F, 14, 12, 0xFF},
}};

// channels: FM1 bass, FM2 lead, FM3 arpeggio, FM4 kick
static void set_patch(u16 fm, const Patch *p) {
    u16 part = fm >= 3, ch = fm % 3;
    for (u16 o = 0; o < 4; o++) {
        const u8 *op = p->op[o];
        u8 r = o * 4 + ch;
        ym_write(part, 0x30 + r, op[0]);
        ym_write(part, 0x40 + r, op[1]);
        ym_write(part, 0x50 + r, op[2]);
        ym_write(part, 0x60 + r, op[3]);
        ym_write(part, 0x70 + r, op[4]);
        ym_write(part, 0x80 + r, op[5]);
        ym_write(part, 0x90 + r, 0);
    }
    ym_write(part, 0xB0 + ch, p->fb_alg);
    ym_write(part, 0xB4 + ch, 0xC0);  // both speakers
}

// F-numbers for C to B at block 4 (A = 440 Hz)
static const u16 FNUM[12] = {644, 682, 723, 766, 811, 859, 910, 965, 1022, 1083, 1147, 1215};

static void key(u16 fm, u16 on) { ym_write(0, 0x28, (on ? 0xF0 : 0) | (fm >= 3 ? 4 : 0) | (fm % 3)); }

static void freq(u16 fm, u16 block, u16 fnum) {
    u16 part = fm >= 3, ch = fm % 3;
    ym_write(part, 0xA4 + ch, (block << 3) | (fnum >> 8));
    ym_write(part, 0xA0 + ch, fnum & 0xFF);
}

static void play_note(u16 fm, u8 midi) {  // MIDI note number: 60 is middle C
    key(fm, 0);
    freq(fm, midi / 12 - 1, FNUM[midi % 12]);
    key(fm, 1);
}

// ---------- the tune ----------
enum { C, AM, F, G };
static const u8 CHORDS[16] = {C, C, AM, AM, F, F, G, G, C, C, AM, AM, F, G, C, C};  // a chord every half bar
static const u8 ROOT[4] = {36, 33, 41, 43};
static const u8 ARPS[4][4] = {{60, 64, 67, 64}, {57, 60, 64, 60}, {53, 57, 60, 57}, {55, 59, 62, 59}};
static const s8 BASS_LINE[8] = {0, 12, 0, 12, 7, 12, 0, 12};
static const u8 MELODY[] = {  // note, length in sixteenths
    76, 4, 79, 2, 84, 2, 83, 2, 79, 2, 76, 4,
    81, 4, 84, 2, 88, 2, 86, 2, 84, 2, 81, 4,
    77, 2, 81, 2, 84, 4, 81, 2, 77, 2, 79, 4,
    79, 6, 77, 2, 76, 2, 74, 2, 71, 4,
    72, 2, 76, 2, 79, 2, 84, 2, 88, 4, 86, 4,
    84, 2, 81, 2, 76, 2, 81, 2, 84, 4, 83, 4,
    81, 4, 77, 4, 79, 4, 83, 4,
    84, 8, 0, 4, 79, 2, 0, 2,
    0xFF,
};
// drums for each sixteenth of a bar: 1 kick, 2 snare, 4 hat
static const u8 DRUMS[16] = {5, 0, 4, 0, 6, 0, 4, 0, 5, 0, 5, 0, 6, 0, 4, 4};

static u8 playing;
static u16 step, tick, mel, mel_left, pass, kick_t, noise_t, noise_len, noise_vol;

void music_init(void) {
    ym_init();
    set_patch(0, &BASS);
    set_patch(1, &LEAD);
    set_patch(2, &ARP);
    set_patch(3, &KICK);
    PSG_NOISE_VOL(15);
}

void music_play(void) {
    playing = 1; step = 0; tick = 0; mel = 0; mel_left = 0; pass = 0; kick_t = 99;
}

void music_stop(void) {
    playing = 0;
    for (u16 fm = 0; fm < 4; fm++) key(fm, 0);
    PSG_NOISE_VOL(15);
}

void music_update(void) {
    if (!playing) return;
    if (tick == 0) {
        u16 chord = CHORDS[(step / 8) % 16], in_bar = step % 16;
        if (step % 2 == 0) {
            play_note(0, ROOT[chord] + BASS_LINE[(step / 2) % 8]);
            play_note(2, ARPS[chord][(step / 2) % 4] + (pass & 1 ? 12 : 0));
        }
        if (!mel_left) {  // the melody: the second time round, an octave down
            if (MELODY[mel] == 0xFF) mel = 0;
            u8 n = MELODY[mel];
            mel_left = MELODY[mel + 1];
            mel += 2;
            if (n) play_note(1, n - (pass & 1 ? 12 : 0)); else key(1, 0);
        }
        mel_left--;
        u8 d = DRUMS[in_bar];
        if (d & 1) { kick_t = 0; play_note(3, 40); }
        if (d & 2) { PSG_NOISE(4 | 1); noise_len = 9; noise_vol = 2; noise_t = 0; }
        else if (d & 4) { PSG_NOISE(4 | 0); noise_len = 3; noise_vol = 6; noise_t = 0; }
        if (++step == 16 * 16) { step = 0; pass++; mel = 0; mel_left = 0; }
    }
    // short notes: the bass and arpeggio let go before the next one
    if (tick == 4) key(2, 0);
    if (tick == 5) key(0, 0);
    if (tick == 5 && mel_left == 0) key(1, 0);
    // the kick drops in pitch
    if (kick_t < 6) { freq(3, 2, 1000 - kick_t * 120); kick_t++; }
    else if (kick_t == 6) { key(3, 0); kick_t++; }
    // the noise fades
    if (noise_t < noise_len) { PSG_NOISE_VOL(noise_vol + (15 - noise_vol) * noise_t / noise_len); noise_t++; }
    else PSG_NOISE_VOL(15);
    if (++tick == STEP) tick = 0;
}
