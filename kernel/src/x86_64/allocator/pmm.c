#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/serial.h>
#include <stddef.h>

#define UNUSED 0x00
#define USED   0x01

uint8_t  *bit_map;
uint64_t  highest_addr = 0;
uint64_t  total_frames = 0;

void frame_allocator_init(struct limine_memmap_response *memmap_response, uint64_t hhdm_offset) {
    uint64_t count = memmap_response->entry_count;

    // Find the highest usable address
    for (uint64_t i = 0; i < count; i++) {
        if (memmap_response->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = memmap_response->entries[i]->base + memmap_response->entries[i]->length;
            if (top > highest_addr) highest_addr = top;
        }
    }

    total_frames = highest_addr / 4096;

    // Find the largest usable region to place the bitmap in
    struct limine_memmap_entry *best = NULL;
    for (uint64_t i = 0; i < count; i++) {
        if (memmap_response->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            if (best == NULL || memmap_response->entries[i]->length > best->length)
                best = memmap_response->entries[i];
        }
    }

    if (best == NULL || best->length < total_frames) {
        serial_print("pmm: no region large enough for bitmap!\n");
        return;
    }

    bit_map = (uint8_t *)(best->base + hhdm_offset);

    serial_print("bitmap_phys: ");  serial_print_hex(best->base);
    serial_print("\nbitmap_virt: "); serial_print_hex((uint64_t)bit_map);
    serial_print("\nhighest_addr: "); serial_print_hex(highest_addr);
    serial_print("\nbitmap_size: ");  serial_print_num(total_frames);
    serial_print(" bytes\n");

    // Mark only frames covered by memmap entries as used
    serial_print("marking everything used...\n");
    for (uint64_t i = 0; i < count; i++) {
        uint64_t base  = memmap_response->entries[i]->base;
        uint64_t len   = memmap_response->entries[i]->length;
        uint64_t start = base / 4096;
        uint64_t end   = (base + len) / 4096;
        for (uint64_t j = start; j < end && j < total_frames; j++)
            bit_map[j] = USED;
    }

    // Free only usable regions
    serial_print("marking usable as free...\n");
    for (uint64_t i = 0; i < count; i++) {
        if (memmap_response->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            uint64_t base = memmap_response->entries[i]->base;
            uint64_t len  = memmap_response->entries[i]->length;
            for (uint64_t j = 0; j < len; j += 4096)
                bit_map[(base + j) / 4096] = UNUSED;
        }
    }

    // Reserve the first 16 MiB for the kernel and bitmap
    serial_print("reserving first 16MiB...\n");
    for (uint64_t i = 0; i < (16 * 1024 * 1024) / 4096; i++) bit_map[i] = USED;

    // Also reserve the frames occupied by the bitmap itself
    uint64_t bitmap_start_frame = best->base / 4096;
    uint64_t bitmap_frame_count = (total_frames + 4095) / 4096;
    for (uint64_t i = bitmap_start_frame; i < bitmap_start_frame + bitmap_frame_count; i++)
        bit_map[i] = USED;

    serial_print("frame allocator initialized\n");
}

void *frame_alloc(uint64_t n) {
    for (uint64_t i = 0; i < total_frames; i++) {

        // Skip used frames
        if (bit_map[i] == USED) continue;

        // Check for frames
        uint64_t run = 0;
        while (run < n && (i + run) < total_frames && bit_map[i + run] == UNUSED)
            run++;

        // Not enough
        if (run < n) { i += run; continue; }

        // Found n frames
        for (uint64_t j = i; j < i + n; j++) bit_map[j] = USED;

        serial_print("frame_alloc: phys=");
        serial_print_hex(i * 4096);
        serial_print("\n");
        return (void *)(i * 4096);
    }

    serial_print("frame_alloc: out of memory\n");
    return NULL;
}

void frame_free(void *frame_addr, uint64_t n) {
    uint64_t frame = (uint64_t)frame_addr / 4096;

    if (frame + n > total_frames) {
        serial_print("frame_free: invalid range ");
        serial_print_hex((uint64_t)frame_addr);
        serial_print("\n");
        return;
    }

    for (uint64_t i = 0; i < n; i++) {
        bit_map[frame + i] = UNUSED;
    }

    serial_print("frame_free: phys=");
    serial_print_hex((uint64_t)frame_addr);
    serial_print(" n=");
    serial_print_num(n);
    serial_print("\n");
}