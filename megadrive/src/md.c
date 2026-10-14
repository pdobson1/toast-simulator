#include "md.h"

volatile u32 vblank_count;
u16 exc_vector;  // set by boot.s when the CPU hits an exception
u32 exc_sp, exc_frame[4];

void vdp_reg(u8 reg, u8 value) { VDP_CTRL = 0x8000 | (reg << 8) | value; }

void vram_write_addr(u16 addr) { VDP_CTRL32 = 0x40000000UL | ((u32)(addr & 0x3FFF) << 16) | (addr >> 14); }

static void cram_write_addr(u16 addr) { VDP_CTRL32 = 0xC0000000UL | ((u32)addr << 16); }

static void vsram_write_addr(u16 addr) { VDP_CTRL32 = 0x40000010UL | ((u32)addr << 16); }

void vdp_init(void) {
    static const u8 regs[24] = {
        0x04,  // 0: no horizontal interrupt
        0x34,  // 1: display off for now, vblank interrupt on, DMA on, mode 5, 224 lines
        VRAM_PLANE_A / 0x400,
        0xB000 / 0x400,  // 3: window plane (unused)
        VRAM_PLANE_B / 0x2000,
        VRAM_SPRITES / 0x200,
        0x00,
        0x00,  // 7: backdrop colour: palette 0, entry 0
        0x00, 0x00,
        0xFF,  // 10: horizontal interrupt counter
        0x00,  // 11: whole-screen scrolling
        0x81,  // 12: 320 pixels wide, no shadow/highlight
        VRAM_HSCROLL / 0x400,
        0x00,
        0x02,  // 15: auto-increment by a word
        0x01,  // 16: planes 64 x 32 tiles
        0x00, 0x00,  // 17-18: window off
        0, 0, 0, 0, 0,
    };
    (void)VDP_CTRL;  // clear any pending control write
    for (int i = 0; i < 19; i++) vdp_reg(i, regs[i]);
    vram_write_addr(0);
    for (u16 i = 0; i < 0x8000; i++) VDP_DATA = 0;
    cram_write_addr(0);
    for (int i = 0; i < 64; i++) VDP_DATA = 0;
    vsram_write_addr(0);
    for (int i = 0; i < 40; i++) VDP_DATA = 0;
}

void vdp_display(int on) { vdp_reg(1, on ? 0x74 : 0x34); }

void vdp_load_tiles(u16 first, const u32 *data, u16 count) {
    vram_write_addr(first * 32);
    const u16 *w = (const u16 *)data;
    for (u32 i = (u32)count * 16; i; i--) VDP_DATA = *w++;
}

void vdp_set_tile(u16 plane, u16 x, u16 y, u16 attr) {
    vram_write_addr(plane + (y * 64 + x) * 2);
    VDP_DATA = attr;
}

void vdp_load_map(u16 plane, const u16 *map) {
    for (u16 y = 0; y < 28; y++) {
        vram_write_addr(plane + y * 128);
        for (u16 x = 0; x < 40; x++) VDP_DATA = *map++;
    }
}

void vdp_set_colour(u16 index, u16 colour) {
    cram_write_addr(index * 2);
    VDP_DATA = colour;
}

void vdp_load_pal(u16 pal, const u16 *colours) {
    cram_write_addr(pal * 32);
    for (int i = 0; i < 16; i++) VDP_DATA = colours[i];
}

void wait_vblank(void) {
    u32 now = vblank_count;
    while (vblank_count == now) {}
}

// ---------- sprites ----------
#define MAX_SPRITES 80
static u16 spr[MAX_SPRITES][4];
static u16 spr_n;

void spr_clear(void) { spr_n = 0; }

void spr_add(s16 x, s16 y, u16 w, u16 h, u16 attr) {
    if (spr_n >= MAX_SPRITES || x <= -32 || x >= 320 || y <= -32 || y >= 224) return;
    u16 *s = spr[spr_n++];
    s[0] = (y + 128) & 0x3FF;
    s[1] = (((w - 1) << 2) | (h - 1)) << 8;
    s[2] = attr;
    s[3] = (x + 128) & 0x1FF;
}

void spr_upload(void) {
    vram_write_addr(VRAM_SPRITES);
    if (!spr_n) {  // one off-screen sprite ends the list
        VDP_DATA = 0; VDP_DATA = 0; VDP_DATA = 0; VDP_DATA = 1;
        return;
    }
    for (u16 i = 0; i < spr_n; i++) {
        VDP_DATA = spr[i][0];
        VDP_DATA = spr[i][1] | (i + 1 < spr_n ? i + 1 : 0);  // link to the next
        VDP_DATA = spr[i][2];
        VDP_DATA = spr[i][3];
    }
}

// ---------- controller ----------
#define PAD_DATA (*(volatile u8 *)0xA10003)
#define PAD_CTRL (*(volatile u8 *)0xA10009)

void pad_init(void) {
    PAD_CTRL = 0x40;  // the TH line is an output
    PAD_DATA = 0x40;
}

u16 pad_read(void) {
    PAD_DATA = 0x40;
    __asm__ volatile("nop\n nop\n nop\n nop");
    u8 hi = ~PAD_DATA;  // up, down, left, right, B, C
    PAD_DATA = 0x00;
    __asm__ volatile("nop\n nop\n nop\n nop");
    u8 lo = ~PAD_DATA;  // A, Start
    PAD_DATA = 0x40;
    return (hi & 0x3F) | ((lo & 0x10) << 2) | ((lo & 0x20) << 2);
}

// ---------- PSG ----------
void psg_tone(u16 ch, u16 period) {
    if (period > 1023) period = 1023;
    PSG_PORT = 0x80 | (ch << 5) | (period & 0x0F);
    PSG_PORT = (period >> 4) & 0x3F;
}

void psg_vol(u16 ch, u16 vol) { PSG_PORT = 0x90 | (ch << 5) | (vol & 0x0F); }

// ---------- YM2612 ----------
#define Z80_RAM     ((volatile u8 *)0xA00000)
#define Z80_BUSREQ  (*(volatile u16 *)0xA11100)
#define Z80_RESET   (*(volatile u16 *)0xA11200)
#define YM_ADDR0    (*(volatile u8 *)0xA04000)
#define YM_DATA0    (*(volatile u8 *)0xA04001)
#define YM_ADDR1    (*(volatile u8 *)0xA04002)
#define YM_DATA1    (*(volatile u8 *)0xA04003)

void ym_init(void) {
    // The YM2612 is held in reset with the Z80, so release the Z80 but keep its bus: it sits on a jump-to-itself.
    Z80_BUSREQ = 0x100;
    Z80_RESET = 0x100;
    while (Z80_BUSREQ & 0x100) {}
    Z80_RAM[0] = 0x18; Z80_RAM[1] = 0xFE;  // jr $
    Z80_RESET = 0;
    for (volatile int i = 0; i < 20; i++) {}
    Z80_RESET = 0x100;
    ym_write(0, 0x22, 0);     // no LFO
    ym_write(0, 0x27, 0);     // channel 3 normal
    ym_write(0, 0x2B, 0);     // no DAC
    for (u16 ch = 0; ch < 3; ch++) {
        ym_write(0, 0x28, ch); ym_write(0, 0x28, 4 + ch);  // keys off
        for (u16 op = 0; op < 4; op++) { ym_write(0, 0x40 + op * 4 + ch, 127); ym_write(1, 0x40 + op * 4 + ch, 127); }
    }
}

static void ym_wait(void) { while (YM_ADDR0 & 0x80) {} }

void ym_write(u16 part, u8 reg, u8 value) {
    ym_wait();
    if (part) { YM_ADDR1 = reg; YM_DATA1 = value; }
    else { YM_ADDR0 = reg; YM_DATA0 = value; }
}
