#include <stdint.h>

static inline uint64_t syscall_ret(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    register uint64_t _a1 asm("rdi") = a1;
    register uint64_t _a2 asm("rsi") = a2;
    register uint64_t _a3 asm("rdx") = a3;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "r"(_a1), "r"(_a2), "r"(_a3) : "memory");
    return ret;
}

static inline void put_pixel(int x, int y, uint32_t c) { syscall_ret(1, x, y, c); }
static inline int   fb_width()  { return (int)syscall_ret(2, 0, 0, 0); }
static inline int   fb_height() { return (int)syscall_ret(3, 0, 0, 0); }

// ── fixed-point config ───────────────────────────────────────────────────────
// Q2.26: 26 fractional bits.  1.0 = 1<<26 = 67108864
// max |z| before escape ~ 2.0 = 2^27,  (2^27)^2 = 2^54  → fits int64 fine
#define FRAC   26
#define ONE    (1LL << FRAC)
#define MUL(a,b)  (((int64_t)(a) * (int64_t)(b)) >> FRAC)

#define MAX_ITER 200

// ── view: full Mandelbrot set ─────────────────────────────────────────────────
// real ∈ [-2.5, 1.0],  imag ∈ [-1.25, 1.25]
#define XMIN  (-167772160LL)   // -2.5 * ONE
#define XMAX  (  67108864LL)   //  1.0 * ONE
#define YMIN  ( -83886080LL)   // -1.25 * ONE
#define YMAX  (  83886080LL)   //  1.25 * ONE

static int mandelbrot_fx(int64_t cr, int64_t ci) {
    int64_t zr = 0, zi = 0;
    for (int i = 0; i < MAX_ITER; i++) {
        int64_t zr2 = MUL(zr, zr);
        int64_t zi2 = MUL(zi, zi);
        if (zr2 + zi2 > 4 * ONE) return i;   // |z|^2 > 4.0
        zi  = 2 * MUL(zr, zi) + ci;
        zr  = zr2 - zi2 + cr;
    }
    return MAX_ITER; // inside set
}

// ── palette: map iter → 24-bit colour (pure integer) ─────────────────────────
// Three staggered sine-ish waves faked with triangle waves for each channel.
// triangle(t, period) oscillates [0,255] with the given period in iterations.
static uint8_t tri(int t, int period) {
    t = t % period;
    if (t < 0) t += period;
    int half = period / 2;
    return (uint8_t)((t < half ? t : period - t) * 255 / half);
}

static uint32_t colour(int iter) {
    if (iter == MAX_ITER) return 0x000000; // inside = black
    uint8_t r = tri(iter,       32);
    uint8_t g = tri(iter + 11,  24);
    uint8_t b = tri(iter + 19,  16);
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

int main() {
    int W = fb_width();
    int H = fb_height();

    int64_t x_step = (XMAX - XMIN) / W;
    int64_t y_step = (YMAX - YMIN) / H;

    for (int py = 0; py < H; py++) {
        int64_t ci = YMIN + py * y_step;
        for (int px = 0; px < W; px++) {
            int64_t cr   = XMIN + px * x_step;
            int     iter = mandelbrot_fx(cr, ci);
            put_pixel(px, py, colour(iter));
        }
    }

    syscall_ret(0, 0, 0, 0);
    return 0;
}