// A small Mega Drive hardware layer: the VDP (graphics), the controller and the PSG (sound).
#pragma once

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned long u32;
typedef signed long s32;

#define VDP_DATA   (*(volatile u16 *)0xC00000)
#define VDP_CTRL   (*(volatile u16 *)0xC00004)
#define VDP_CTRL32 (*(volatile u32 *)0xC00004)
#define PSG_PORT   (*(volatile u8 *)0xC00011)

// VRAM layout: tiles from 0 to 0xAFFF, then the name tables, sprites and scroll table
#define VRAM_PLANE_A 0xC000
#define VRAM_PLANE_B 0xE000
#define VRAM_SPRITES 0xF800
#define VRAM_HSCROLL 0xFC00

// a name-table or sprite tile word: priority, palette, flips and tile number
#define ATTR(pal, pri, vf, hf, tile) (((pri) << 15) | ((pal) << 13) | ((vf) << 12) | ((hf) << 11) | (tile))

void vdp_init(void);
void vdp_reg(u8 reg, u8 value);
void vdp_display(int on);
void vram_write_addr(u16 addr);
void vdp_load_tiles(u16 first, const u32 *data, u16 count);
void vdp_set_tile(u16 plane, u16 x, u16 y, u16 attr);
void vdp_load_map(u16 plane, const u16 *map);  // a 40 x 28 map into a 64-wide plane
void vdp_set_colour(u16 index, u16 colour);    // index 0-63: palette * 16 + entry
void vdp_load_pal(u16 pal, const u16 *colours);

extern volatile u32 vblank_count;
void wait_vblank(void);

// sprites: built in RAM each frame, then sent to the VDP in the blank
void spr_clear(void);
void spr_add(s16 x, s16 y, u16 w, u16 h, u16 attr);  // screen pixels; size in tiles (1-4)
void spr_upload(void);

// controller 1, 3-button pad
#define BTN_UP    0x01
#define BTN_DOWN  0x02
#define BTN_LEFT  0x04
#define BTN_RIGHT 0x08
#define BTN_B     0x10
#define BTN_C     0x20
#define BTN_A     0x40
#define BTN_START 0x80
void pad_init(void);
u16 pad_read(void);

// PSG: three square-wave channels (0-2); volume 0 is loudest, 15 silent
#define HZ(f) (111861UL / (f))  // tone period for a frequency in Hz (110 Hz or higher)
void psg_tone(u16 ch, u16 period);
void psg_vol(u16 ch, u16 vol);

void *memset(void *dst, int c, unsigned long n);
void *memcpy(void *dst, const void *src, unsigned long n);

// YM2612 FM synth, written by the 68000 directly (the Z80 is parked, holding nothing but a loop)
void ym_init(void);
void ym_write(u16 part, u8 reg, u8 value);  // part 0: channels 1-3, part 1: channels 4-6
#define PSG_NOISE(ctrl) (PSG_PORT = 0xE0 | (ctrl))   // bit 2: white noise; bits 0-1: rate
#define PSG_NOISE_VOL(v) (PSG_PORT = 0xF0 | (v))
