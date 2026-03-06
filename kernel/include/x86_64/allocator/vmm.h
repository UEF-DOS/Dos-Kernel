#ifndef VMM_H
#define VMM_H

#include <stdint.h>

void map_page(uint64_t *pml4, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags);
void unmap_page(uint64_t *pml4, uint64_t virt_addr);
uint64_t vmm_create_pml4();
void vmm_switch(uint64_t pml4_phys);
void vmm_map_range(uint64_t *pml4, uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags);
void vmm_unmap_range(uint64_t *pml4, uint64_t virt, uint64_t size);
uint64_t vmm_get_phys(uint64_t *pml4, uint64_t virt);
void vmm_init();

#endif