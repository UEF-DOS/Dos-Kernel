#include "limine.h"
#include <stddef.h>
#include <stdint.h>
#include <memory.h>
#include <assert.h>
#include <memory/tlb.h>
#include <memory/frame.h>
#include <memory/hhdm.h>
#include <memory/paging.h>

extern struct limine_memmap_request memmap_request;

#define ENTRY_4K_ADDRESS_MASK ((uint64_t)0x000FFFFFFFFFF000)
#define VADDR_TO_INDEX(VADDR, LEVEL) (((VADDR) >> ((LEVEL) * 9 + 3)) & 0x1FF)

uint64_t *kernel_pml4 = NULL;

static void map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, uint64_t flags) {
    uint64_t *current_table = pml4;
    for (int level = 4; level > 1; level--) {
        int index = VADDR_TO_INDEX(vaddr, level);
        uint64_t entry = __atomic_load_n(&current_table[index], __ATOMIC_SEQ_CST);
        if ((entry & ENTRY_FLAG_PRESENT) == 0) {
            uint64_t phys = frame_alloc();
            uint64_t *new_table = (uint64_t *)(offset + phys);
            memset(new_table, 0, 0x1000);
            entry = ENTRY_FLAG_PRESENT | ENTRY_FLAG_RW | (phys & ENTRY_4K_ADDRESS_MASK);
            __atomic_store(&current_table[index], &entry, __ATOMIC_SEQ_CST);
        }
        entry = __atomic_load_n(&current_table[index], __ATOMIC_SEQ_CST);
        current_table = (uint64_t *)(offset + (entry & ENTRY_4K_ADDRESS_MASK));
    }
    int index = VADDR_TO_INDEX(vaddr, 1);
    uint64_t entry = ENTRY_FLAG_PRESENT | (paddr & ENTRY_4K_ADDRESS_MASK) | flags;
    __atomic_store(&current_table[index], &entry, __ATOMIC_SEQ_CST);
}

static void unmap_page(uint64_t *pml4, uint64_t vaddr) {
    uint64_t *current_table = pml4;
    for (int level = 4; level > 1; level--) {
        int index = VADDR_TO_INDEX(vaddr, level);
        uint64_t entry = __atomic_load_n(&current_table[index], __ATOMIC_SEQ_CST);
        if ((entry & ENTRY_FLAG_PRESENT) == 0)
            return;
        current_table = (uint64_t *)(offset + (entry & ENTRY_4K_ADDRESS_MASK));
    }
    int index = VADDR_TO_INDEX(vaddr, 1);
    uint64_t zero = 0;
    __atomic_store(&current_table[index], &zero, __ATOMIC_SEQ_CST);
}

void paging_map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, size_t length, uint64_t flags) {
    ASSERT(vaddr % 0x1000 == 0);
    ASSERT(paddr % 0x1000 == 0);
    ASSERT(length % 0x1000 == 0);
    for (size_t i = 0; i < length; i += 0x1000)
        map_page(pml4, vaddr + i, paddr + i, flags);
    invalidate(vaddr, length);
}

void paging_unmap_page(uint64_t *pml4, uint64_t vaddr, size_t length) {
    ASSERT(vaddr % 0x1000 == 0);
    ASSERT(length % 0x1000 == 0);
    for (size_t i = 0; i < length; i += 0x1000)
        unmap_page(pml4, vaddr + i);
    invalidate(vaddr, length);
}

uint64_t paging_get_entry(uint64_t *pml4, uint64_t vaddr) {
    uint64_t *current_table = pml4;
    for (int level = 4; level > 1; level--) {
        int index = VADDR_TO_INDEX(vaddr, level);
        uint64_t entry = __atomic_load_n(&current_table[index], __ATOMIC_SEQ_CST);
        if ((entry & ENTRY_FLAG_PRESENT) == 0)
            return 0;
        current_table = (uint64_t *)(offset + (entry & ENTRY_4K_ADDRESS_MASK));
    }
    return __atomic_load_n(&current_table[VADDR_TO_INDEX(vaddr, 1)], __ATOMIC_SEQ_CST);
}

uint64_t *paging_create_pml4(void) {
    uint64_t phys = frame_alloc();
    uint64_t *pml4 = (uint64_t *)(offset + phys);
    memset(pml4, 0, 0x1000);
    for (int i = 256; i < 512; i++)
        pml4[i] = __atomic_load_n(&kernel_pml4[i], __ATOMIC_SEQ_CST);
    return pml4;
}

void paging_init(void) {
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

    kernel_pml4 = pml4;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}