#ifndef ADDR_H
#define ADDR_H

#include <stdint.h>

uint64_t phys_to_virt(uint64_t phys_addr);
uint64_t virt_to_phys(uint64_t virt_addr);

#endif