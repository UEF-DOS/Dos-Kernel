#include <stdint.h>
#include <allocators/paging.h>

[[gnu::aligned(0x20)]]
uint64_t pdpt[4];

[[gnu::aligned(0x1000)]]
uint64_t page_dir[512];

void init_paging() {
    pdpt[0] = (uint64_t)&page_dir | PRESENT;
    page_dir[0] = 0b10000011;
    asm volatile ("mov %0, %%cr3" :: "r" (&pdpt));
}