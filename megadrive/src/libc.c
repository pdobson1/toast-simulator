// The few C runtime pieces the game needs. GCC's own versions here are built for the 68020,
// which the Mega Drive's 68000 can't run, so these are plain C for the 68000.
#include "md.h"

void *memset(void *dst, int c, unsigned long n) {
    u8 *d = dst;
    while (n--) *d++ = (u8)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n) {
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

static inline u32 mulu16(u16 a, u16 b) {  // the 68000's own 16 x 16 -> 32 multiply
    u32 r = a;
    __asm__("mulu.w %1, %0" : "+d"(r) : "dm"(b));
    return r;
}

unsigned long __mulsi3(unsigned long a, unsigned long b) {
    // the top halves only matter for the low 32 bits of the result
    u16 al = a, ah = a >> 16, bl = b, bh = b >> 16;
    return mulu16(al, bl) + ((mulu16(al, bh) + mulu16(ah, bl)) << 16);
}

static unsigned long udivmod(unsigned long n, unsigned long d, int want_mod) {
    unsigned long q = 0, bit = 1;
    if (!d) return 0;
    while (!(d & 0x80000000UL) && d < n) { d <<= 1; bit <<= 1; }
    while (bit) {
        if (n >= d) { n -= d; q |= bit; }
        d >>= 1; bit >>= 1;
    }
    return want_mod ? n : q;
}

unsigned long __udivsi3(unsigned long n, unsigned long d) { return udivmod(n, d, 0); }
unsigned long __umodsi3(unsigned long n, unsigned long d) { return udivmod(n, d, 1); }

long __divsi3(long n, long d) {
    int neg = (n < 0) ^ (d < 0);
    unsigned long q = udivmod(n < 0 ? -n : n, d < 0 ? -d : d, 0);
    return neg ? -(long)q : (long)q;
}

long __modsi3(long n, long d) {
    unsigned long r = udivmod(n < 0 ? -n : n, d < 0 ? -d : d, 1);
    return n < 0 ? -(long)r : (long)r;
}
