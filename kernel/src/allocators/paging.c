#include "limine.h"
#include <stdint.h>
#include <memory.h>
#include <allocators/frame.h>
#include <allocators/hhdm.h>
#include <allocators/paging.h>

extern struct limine_memmap_request memmap_request;

#define ENTRY_4K_ADDRESS_MASK ((uint64_t) 0x000F'FFFF'FFFF'F000)
#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)

static void map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t *current_table = pml4;

    for (int level = 4; level > 1; level--) {
        int index = VADDR_TO_INDEX(vaddr, level);
        uint64_t entry = current_table[index];

        if ((entry & ENTRY_FLAG_PRESENT) == 0) {
            uint64_t phys = frame_alloc();
            uint64_t *new_table = (uint64_t *)(offset + phys);
            memset(new_table, 0, 0x1000);
            entry = ENTRY_FLAG_PRESENT | ENTRY_FLAG_RW | (phys & ENTRY_4K_ADDRESS_MASK);
            __atomic_store(&current_table[index], &entry, __ATOMIC_SEQ_CST);
        }

        current_table = (uint64_t *)(offset + (entry & ENTRY_4K_ADDRESS_MASK));
    }

    int index = VADDR_TO_INDEX(vaddr, 1);
    uint64_t entry = ENTRY_FLAG_PRESENT | (paddr & ENTRY_4K_ADDRESS_MASK) | flags;
    __atomic_store(&current_table[index], &entry, __ATOMIC_SEQ_CST);
}

void paging_init() {
    uint64_t pml4_phys = frame_alloc();
    uint64_t *pml4 = (uint64_t *)(offset + pml4_phys);
    memset(pml4, 0, 0x1000);

    struct limine_memmap_response *memmap = memmap_request.response;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];

        uint64_t flags = ENTRY_FLAG_PRESENT | ENTRY_FLAG_RW | ENTRY_FLAG_NX;
        if (e->type == LIMINE_MEMMAP_EXECUTABLE_AND_MODULES)
            flags &= ~ENTRY_FLAG_NX;

        for (uint64_t j = 0; j < e->length; j += 0x1000)
            map_page(pml4, offset + e->base + j, e->base + j, flags);
    }

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        if (e->type != LIMINE_MEMMAP_EXECUTABLE_AND_MODULES)
            continue;

        for (uint64_t j = 0; j < e->length; j += 0x1000) {
            map_page(pml4,
                     0xffffffff80000000 + j,
                     e->base + j,
                     ENTRY_FLAG_PRESENT | ENTRY_FLAG_RW);
        }
    }

    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}