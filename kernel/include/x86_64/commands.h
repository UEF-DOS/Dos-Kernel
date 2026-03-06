#ifndef COMMANDS_H
#define COMMANDS_H

#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile (
        "inb %w1, %b0": "=a" (result): "Nd" (port): "memory"
    );
    return result;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile (
        "outb %b0, %w1":: "a" (val),"Nd" (port): "memory"
    );
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile (
        "outw %w0, %w1":: "a" (val), "Nd" (port): "memory"
    );
}

static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile (
        "inw %w1, %w0": "=a" (result): "Nd" (port): "memory"
    );
    return result;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t edx = (value >> 32) & 0xffffffff;
    uint32_t eax = value & 0xffffffff;
    __asm__ volatile (
        "wrmsr"
        :
        : "c" (msr), "a" (eax), "d" (edx)
    );
}

static inline void read_msr(uint32_t msr, uint32_t *eax, uint32_t *edx) {
    __asm__ volatile (
        "rdmsr"
        : "=a" (*eax), "=d" (*edx)
        : "c" (msr)
    );
}


static inline void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *edx) {
    __asm__ volatile (
        "cpuid"
        : "=a" (*eax), "=d" (*edx)
        : "a" (leaf)
        : "ebx", "ecx"
    );
}

#endif