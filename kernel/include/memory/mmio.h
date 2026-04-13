#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory/vm.h>

struct mmio_region {
    uint64_t        phys_base;
    uint64_t        virt_base;
    size_t          size;
    struct vm_region vm_region;
};

struct mmio_region *mmio_init(struct vm_space *space, uint64_t phys_base, size_t size);
void mmio_destroy(struct vm_space *space, struct mmio_region *mr);

uint8_t mmio_read8 (const struct mmio_region *mr, size_t offset);
uint16_t mmio_read16(const struct mmio_region *mr, size_t offset);
uint32_t mmio_read32(const struct mmio_region *mr, size_t offset);
uint64_t mmio_read64(const struct mmio_region *mr, size_t offset);

void mmio_write8 (const struct mmio_region *mr, size_t offset, uint8_t  val);
void mmio_write16(const struct mmio_region *mr, size_t offset, uint16_t val);
void mmio_write32(const struct mmio_region *mr, size_t offset, uint32_t val);
void mmio_write64(const struct mmio_region *mr, size_t offset, uint64_t val);