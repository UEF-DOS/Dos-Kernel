#ifndef STD_H
#define STD_H

#include <stdint.h>

static inline uint64_t syscall_ret(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3) {
    uint64_t ret;
    register uint64_t _a1 asm("rdi") = a1;
    register uint64_t _a2 asm("rsi") = a2;
    register uint64_t _a3 asm("rdx") = a3;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "r"(_a1), "r"(_a2), "r"(_a3) : "memory");
    return ret;
}

static inline void _exit_(uint64_t v) {
    syscall_ret(0, v, 0, 0);
}

static inline void plot_pixel(uint64_t x, uint64_t y, uint32_t color) {
    syscall_ret(1, x, y, 0);
}

static inline uint64_t get_fb_width() {
    return syscall_ret(2, 0, 0, 0);
}

static inline uint64_t get_fb_height() {
    return syscall_ret(3, 0, 0, 0);
}

static inline uint64_t get_fb_pitch() {
    return syscall_ret(4, 0, 0, 0);
}

static inline uint64_t map_fb() {
    return syscall_ret(5, 0, 0, 0);
}

// Polls
static inline uint8_t get_key() {
    return syscall_ret(6, 0, 0, 0);
}

// Doesn't poll
static inline uint8_t consume_key() {
    return syscall_ret(7, 0, 0, 0);
}

static inline uint64_t wait_ms(uint64_t ms) {
    return syscall_ret(8, ms, 0, 0);
}

static inline uint8_t exec(const char *path) {
    return syscall_ret(9, (uint64_t)path, 0, 0);
}

#endif