#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <memory/frame.h>
#include <memory/vm.h>
#include <memory/hhdm.h>
#include <memory/paging.h>
#include <memory/mmio.h>

#define MMIO_FLAGS  (ENTRY_FLAG_PRESENT  | \
                     ENTRY_FLAG_RW       | \
                     ENTRY_FLAG_PWT      | \
                     ENTRY_FLAG_PCD      | \
                     ENTRY_FLAG_NX)

struct mmio_region *mmio_init(struct vm_space *space, uint64_t phys_base, size_t size) {
    ASSERT_NOT_NULL(space);
    ASSERT(phys_base % 0x1000 == 0);
    ASSERT(size > 0);

    size_t aligned_size = (size + 0xFFF) & ~(size_t)0xFFF;

    uint64_t phys = frame_alloc();
    struct mmio_region *mr = (struct mmio_region *)(offset + phys);

    mr->phys_base       = phys_base;
    mr->size            = aligned_size;

    mr->vm_region.base  = phys_base;
    mr->vm_region.size  = aligned_size;
    mr->vm_region.next  = NULL;

    mr->virt_base = vm_map_region(space, &mr->vm_region, MMIO_FLAGS);

    return mr;
}

uint8_t mmio_read8(const struct mmio_region *mr, size_t offset_bytes) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes < mr->size);
    return *(volatile uint8_t *)(mr->virt_base + offset_bytes);
}

uint16_t mmio_read16(const struct mmio_region *mr, size_t offset_bytes) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes + sizeof(uint16_t) <= mr->size);
    return *(volatile uint16_t *)(mr->virt_base + offset_bytes);
}

uint32_t mmio_read32(const struct mmio_region *mr, size_t offset_bytes) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes + sizeof(uint32_t) <= mr->size);
    return *(volatile uint32_t *)(mr->virt_base + offset_bytes);
}

uint64_t mmio_read64(const struct mmio_region *mr, size_t offset_bytes) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes + sizeof(uint64_t) <= mr->size);
    return *(volatile uint64_t *)(mr->virt_base + offset_bytes);
}

void mmio_write8(const struct mmio_region *mr, size_t offset_bytes, uint8_t val) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes < mr->size);
    *(volatile uint8_t *)(mr->virt_base + offset_bytes) = val;
}

void mmio_write16(const struct mmio_region *mr, size_t offset_bytes, uint16_t val) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes + sizeof(uint16_t) <= mr->size);
    *(volatile uint16_t *)(mr->virt_base + offset_bytes) = val;
}

void mmio_write32(const struct mmio_region *mr, size_t offset_bytes, uint32_t val) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes + sizeof(uint32_t) <= mr->size);
    *(volatile uint32_t *)(mr->virt_base + offset_bytes) = val;
}

void mmio_write64(const struct mmio_region *mr, size_t offset_bytes, uint64_t val) {
    ASSERT_NOT_NULL(mr);
    ASSERT(offset_bytes + sizeof(uint64_t) <= mr->size);
    *(volatile uint64_t *)(mr->virt_base + offset_bytes) = val;
}

void mmio_destroy(struct vm_space *space, struct mmio_region *mr) {
    ASSERT_NOT_NULL(space);
    ASSERT_NOT_NULL(mr);

    paging_unmap_page(space->pml4, mr->virt_base, mr->size);

    frame_free((uint64_t)mr - offset);
}