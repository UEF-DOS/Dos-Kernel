#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/serial.h>

// Each bit in the bitmap represents one 4KiB frame (0 = free, 1 = used)
// Using 64-bit words lets us scan 64 frames per iteration with ctzll
#define BITMAP_WORD(frame)  ((frame) / 64)
#define BITMAP_BIT(frame)   ((frame) % 64)
#define FRAME_USED(w, b)    ((w) |=  (1ULL << (b)))
#define FRAME_FREE(w, b)    ((w) &= ~(1ULL << (b)))
#define FRAME_IS_FREE(w, b) (!((w) >> (b) & 1ULL))

#define DEBUG

#ifdef DEBUG
#define PMM_LOG(msg)     serial_print(msg)
#define PMM_LOG_HEX(val) serial_print_hex(val)
#define PMM_LOG_NUM(val) serial_print_num(val)
#else
#define PMM_LOG(msg)
#define PMM_LOG_HEX(val)
#define PMM_LOG_NUM(val)
#endif

uint64_t *bit_map;
uint64_t  highest_addr  = 0;
uint64_t  total_frames  = 0;

// Cached index of the last word that had a free frame; avoids scanning from 0
// on every allocation when memory is moderately fragmented
static uint64_t last_free_word = 0;

void frame_allocator_init(struct limine_memmap_response *memmap_response, uint64_t hhdm_offset) {
    uint64_t count = memmap_response->entry_count;

    // Find the highest usable address so we know how large the bitmap must be
    for (uint64_t i = 0; i < count; i++) {
        if (memmap_response->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            uint64_t top = memmap_response->entries[i]->base + memmap_response->entries[i]->length;
            if (top > highest_addr) highest_addr = top;
        }
    }

    total_frames = highest_addr / 4096;

    // Place the bitmap at the start of the first usable region
    for (uint64_t i = 0; i < count; i++) {
        if (memmap_response->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            bit_map = (uint64_t *)(memmap_response->entries[i]->base + hhdm_offset);
            serial_print("bitmap_phys: ");
            serial_print_hex(memmap_response->entries[i]->base);
            serial_print("\nbitmap_virt: ");
            serial_print_hex((uint64_t)bit_map);
            serial_print("\nhighest_addr: ");
            serial_print_hex(highest_addr);
            serial_print("\nbitmap_size (words): ");
            serial_print_num((total_frames + 63) / 64);
            serial_print("\n");
            break;
        }
    }

    // Mark every frame as used; only usable regions will be freed below
    serial_print("marking everything used...\n");
    uint64_t words = (total_frames + 63) / 64;
    for (uint64_t i = 0; i < words; i++) bit_map[i] = ~0ULL;

    // Free frames that belong to usable memory regions
    serial_print("marking usable as free...\n");
    for (uint64_t i = 0; i < count; i++) {
        if (memmap_response->entries[i]->type == LIMINE_MEMMAP_USABLE) {
            uint64_t base  = memmap_response->entries[i]->base;
            uint64_t len   = memmap_response->entries[i]->length;

            PMM_LOG("usable region: base="); PMM_LOG_HEX(base);
            PMM_LOG(" len=");               PMM_LOG_HEX(len);
            PMM_LOG("\n");

            for (uint64_t j = 0; j < len; j += 4096) {
                uint64_t frame = (base + j) / 4096;
                FRAME_FREE(bit_map[BITMAP_WORD(frame)], BITMAP_BIT(frame));
            }
        }
    }

    // Reserve the first 16 MiB for the kernel and the bitmap itself
    serial_print("reserving first 16MiB...\n");
    uint64_t reserved = (16 * 1024 * 1024) / 4096;
    for (uint64_t i = 0; i < reserved; i++)
        FRAME_USED(bit_map[BITMAP_WORD(i)], BITMAP_BIT(i));

    PMM_LOG("frame_allocator_init: total_frames="); PMM_LOG_NUM(total_frames); PMM_LOG("\n");

    serial_print("frame allocator initialized\n");
}

uint64_t frame_alloc() {
    uint64_t words = (total_frames + 63) / 64;

    // Search from the cached hint first, then wrap around if needed
    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t start = (pass == 0) ? last_free_word : 0;
        uint64_t end   = (pass == 0) ? words          : last_free_word;

        for (uint64_t w = start; w < end; w++) {
            if (bit_map[w] == ~0ULL) continue; // all 64 frames in this word are used

            // __builtin_ctzll finds the lowest set bit in the inverted word,
            // which corresponds to the lowest free (0) bit in the original
            int bit = __builtin_ctzll(~bit_map[w]);
            uint64_t frame = w * 64 + bit;

            if (frame >= total_frames) continue;

            FRAME_USED(bit_map[w], bit);
            last_free_word = w; // update hint for next allocation

            PMM_LOG("frame_alloc: phys="); PMM_LOG_HEX(frame * 4096); PMM_LOG("\n");

            return frame * 4096;
        }
    }

    serial_print("frame_alloc: out of memory\n");
    return 0;
}

void frame_free(uint64_t frame_addr) {
    uint64_t frame = frame_addr / 4096;

    // Validate before touching the bitmap
    if (frame >= total_frames) {
        serial_print("frame_free: invalid frame ");
        serial_print_hex(frame_addr);
        serial_print("\n");
        return;
    }

    FRAME_FREE(bit_map[BITMAP_WORD(frame)], BITMAP_BIT(frame));

    // Roll back the hint if this freed frame is earlier than our current position
    uint64_t w = BITMAP_WORD(frame);
    if (w < last_free_word) last_free_word = w;

    PMM_LOG("frame_free: phys="); PMM_LOG_HEX(frame_addr); PMM_LOG("\n");
}