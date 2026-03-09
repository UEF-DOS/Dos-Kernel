#include <stdint.h>
#include <x86_64/fpu.h>

void fpu_init() {
    uint64_t cr0, cr4;

    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);  // clear EM
    cr0 |=  (1ULL << 1);  // set MP
    cr0 &= ~(1ULL << 3);  // clear TS
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile ("fninit");

    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);   // OSFXSR
    cr4 |= (1ULL << 10);  // OSXMMEXCPT
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    uint32_t mxcsr = 0x1F80;
    __asm__ volatile ("ldmxcsr %0" :: "m"(mxcsr));
}