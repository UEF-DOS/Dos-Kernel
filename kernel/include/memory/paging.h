#pragma once
#include <stddef.h>
#include <stdint.h>

// Flags
#define ENTRY_FLAG_PRESENT  ((uint64_t)1 << 0)
#define ENTRY_FLAG_RW       ((uint64_t)1 << 1)
#define ENTRY_FLAG_USER     ((uint64_t)1 << 2) 
#define ENTRY_FLAG_PWT      ((uint64_t)1 << 3)
#define ENTRY_FLAG_PCD      ((uint64_t)1 << 4) 
#define ENTRY_FLAG_ACCESSED ((uint64_t)1 << 5)
#define ENTRY_FLAG_DIRTY    ((uint64_t)1 << 6)
#define ENTRY_FLAG_NX       ((uint64_t)1 << 63)

#define ENTRY_4K_ADDRESS_MASK ((uint64_t)0x000FFFFFFFFFF000)

extern uint64_t *kernel_pml4;

uint64_t  paging_get_entry(uint64_t *pml4, uint64_t vaddr);
void      paging_map_page(uint64_t *pml4, uint64_t vaddr, uint64_t paddr, size_t length, uint64_t flags);
void      paging_unmap_page(uint64_t *pml4, uint64_t vaddr, size_t length);
uint64_t *paging_create_pml4();
void      paging_init(); 