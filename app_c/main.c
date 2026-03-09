#include <stdint.h>

static inline uint64_t syscall_ret(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    register uint64_t _a1 asm("rdi") = a1;
    register uint64_t _a2 asm("rsi") = a2;
    register uint64_t _a3 asm("rdx") = a3;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "r"(_a1), "r"(_a2), "r"(_a3) : "memory");
    return ret;
}

#define LUT_SIZE 1024

static uint16_t sin_lut[LUT_SIZE];

static void build_lut() {
    for (int i = 0; i < LUT_SIZE; i++) {
        int x = i;
        int half    = LUT_SIZE / 2;
        int quarter = LUT_SIZE / 4;
        int sign = 1;
        if (x >= half) { x -= half; sign = -1; }
        if (x > quarter) x = half - x;
        int v    = x * (quarter - x);
        int vmax = quarter * quarter / 4;
        int s    = v * 32767 / vmax;
        sin_lut[i] = (uint16_t)(sign > 0 ? 32767 + s : 32767 - s);
    }
}

static inline int isin(int a) { return sin_lut[a & (LUT_SIZE - 1)]; }

int main() {
    build_lut();

    uint8_t key = syscall_ret(6, 0, 0, 0);

    if (key == 'n') {
        syscall_ret(0, 0, 0, 0);
    }

    int       W     = (int)syscall_ret(2, 0, 0, 0);
    int       H     = (int)syscall_ret(3, 0, 0, 0);
    int       pitch = (int)syscall_ret(4, 0, 0, 0) / 4;
    uint32_t *fb    = (uint32_t *)syscall_ret(5, 0, 0, 0);

    for (uint32_t t = 0; ; t++) {
        for (int py = 0; py < H; py++) {
            uint32_t *row = fb + py * pitch;

            // three overlapping sine waves per row — one lookup each
            int wave1 = isin(py * 2 + t);
            int wave2 = isin(py * 3 - t * 2 + 128);
            int wave3 = isin(py     + t * 3 + 256);

            // each channel gets a different wave combo → smooth color shifting
            uint8_t r = (uint8_t)((wave1 + wave2) >> 9);
            uint8_t g = (uint8_t)((wave2 + wave3) >> 9);
            uint8_t b = (uint8_t)((wave1 + wave3) >> 9);

            // boost blue/green so it looks like aurora not fire
            g = (uint8_t)(g < 180 ? g + 40 : 255);
            b = (uint8_t)(b < 150 ? b + 80 : 255);
            r = (uint8_t)(r >> 1);  // dim red

            uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;

            // fill the row — inner loop is a single write
            for (int px = 0; px < W; px++)
                row[px] = color;
        }
    }

    syscall_ret(0, 0, 0, 0);
    return 0;
}