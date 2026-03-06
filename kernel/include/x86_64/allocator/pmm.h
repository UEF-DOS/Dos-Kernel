#ifndef PMM_H
#define PMM_H

#include <limine.h>
#include <stdint.h>

void frame_allocator_init(struct limine_memmap_response *memmap_response, uint64_t hhdm_offset);
uint64_t frame_alloc();
void frame_free(uint64_t frame);

#endif