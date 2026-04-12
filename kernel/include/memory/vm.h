#pragma once

#include <stddef.h>
#include <stdint.h>

struct vm_region {
    uint64_t base;
    size_t size;
    struct vm_region *next;
};

struct vm_space {
    uint64_t *pml4;
    uint64_t heap_base;
    uint64_t heap_end;
    uint64_t next_free;
    struct vm_region *regions;
};

struct vm_space *vm_space_create(uint64_t heap_base, size_t heap_size);
void vm_space_destroy(struct vm_space *space);
uint64_t vm_alloc(struct vm_space *space, size_t size, uint64_t flags);
uint64_t vm_alloc_at(struct vm_space *space, uint64_t vaddr, size_t size, uint64_t flags);
uint64_t vm_map(struct vm_space *space, uint64_t paddr, size_t size, uint64_t flags);
uint64_t vm_map_at(struct vm_space *space, uint64_t vaddr, uint64_t paddr, size_t size, uint64_t flags);
void vm_free(struct vm_space *space, uint64_t vaddr, size_t size);
void vm_switch(struct vm_space *space);
uint64_t vm_virt_to_phys(struct vm_space *space, uint64_t vaddr);