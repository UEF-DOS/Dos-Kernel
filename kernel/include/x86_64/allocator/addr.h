#ifndef ADDR_H
#define ADDR_H

#include <stdint.h>

void *phys_to_virt(uint64_t phys_addr);
void *virt_to_phys(uint64_t virt_addr);

#endif