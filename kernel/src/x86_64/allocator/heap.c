#include <x86_64/allocator/heap.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/serial.h>
#include <stdbool.h>

// Metadata stored immediately before each allocation's data region
struct heap_node {
    struct heap_node *all_prev;
    struct heap_node *all_next;
    struct heap_node *fl_prev;
    struct heap_node *fl_next;
    size_t size;
    bool   used;
};

// One doubly-linked free list per size bucket
struct heap_free_list {
    struct heap_node *first;
    struct heap_node *last;
};

// Segregated size classes: [8,64), [64,256), [256,1024), [1024,∞)
#define NUM_BUCKETS 4
static const size_t bucket_thresholds[NUM_BUCKETS] = { 64, 256, 1024, SIZE_MAX };
static struct heap_free_list free_buckets[NUM_BUCKETS];

// Global list of every node (used + free) for coalescing during kfree
static struct heap_node *all_first = NULL;
static struct heap_node *all_last  = NULL;

// Virtual address region reserved for the kernel heap.
// Adjust base and size to fit your memory map.
#define HEAP_VIRT_BASE 0xFFFF900000000000ULL
#define HEAP_VIRT_SIZE 0x0000100000000000ULL

// Cursor into the heap VA region; bumped by one page each time expand_heap maps a new frame
static uint64_t heap_virt_cursor = HEAP_VIRT_BASE;

// PML4 used for all heap mappings; set during heap_init from the current CR3
static uint64_t heap_pml4_phys = 0;

// Kernel heap pages are mapped with Present | RW; no user access
#define HEAP_PAGE_FLAGS 0x3

#ifdef DEBUG
#define HEAP_LOG(msg)      serial_print(msg)
#define HEAP_LOG_HEX(val)  serial_print_hex(val)
#define HEAP_LOG_NUM(val)  serial_print_num(val)
#else
#define HEAP_LOG(msg)
#define HEAP_LOG_HEX(val)
#define HEAP_LOG_NUM(val)
#endif

#define NODE_DATA(node)      ((void *)((uint8_t *)(node) + sizeof(struct heap_node)))
#define NODE_FROM_DATA(ptr)  ((struct heap_node *)((uint8_t *)(ptr) - sizeof(struct heap_node)))

static int bucket_for_size(size_t size) {
    for (int i = 0; i < NUM_BUCKETS - 1; i++)
        if (size < bucket_thresholds[i]) return i;
    return NUM_BUCKETS - 1;
}

static void fl_insert(struct heap_node *node) {
    int b = bucket_for_size(node->size);
    node->fl_next = free_buckets[b].first;
    node->fl_prev = NULL;
    if (free_buckets[b].first) free_buckets[b].first->fl_prev = node;
    else                        free_buckets[b].last = node;
    free_buckets[b].first = node;
}

static void fl_remove(struct heap_node *node) {
    int b = bucket_for_size(node->size);
    if (node->fl_prev) node->fl_prev->fl_next = node->fl_next;
    else               free_buckets[b].first  = node->fl_next;
    if (node->fl_next) node->fl_next->fl_prev = node->fl_prev;
    else               free_buckets[b].last   = node->fl_prev;
    node->fl_prev = node->fl_next = NULL;
}

static void all_append(struct heap_node *node) {
    node->all_prev = all_last;
    node->all_next = NULL;
    if (all_last) all_last->all_next = node;
    else          all_first = node;
    all_last = node;
}

static void all_insert_after(struct heap_node *after, struct heap_node *node) {
    node->all_prev = after;
    node->all_next = after->all_next;
    if (after->all_next) after->all_next->all_prev = node;
    else                 all_last = node;
    after->all_next = node;
}

static void all_remove(struct heap_node *node) {
    if (node->all_prev) node->all_prev->all_next = node->all_next;
    else                all_first = node->all_next;
    if (node->all_next) node->all_next->all_prev = node->all_prev;
    else                all_last  = node->all_prev;
    node->all_prev = node->all_next = NULL;
}

// Allocate one or more frames, map them into the heap VA region via the VMM.
static struct heap_node *expand_heap(size_t size) {
    size_t total         = size + sizeof(struct heap_node);
    size_t frames_needed = (total + 4095) / 4096;
    if (frames_needed == 0) frames_needed = 1;

    HEAP_LOG("expand_heap: size="); HEAP_LOG_NUM(size);
    HEAP_LOG(" frames=");           HEAP_LOG_NUM(frames_needed);
    HEAP_LOG("\n");

    // Map all frames contiguously and make ONE node spanning all of them
    uint64_t virt_start = heap_virt_cursor;

    for (size_t i = 0; i < frames_needed; i++) {
        void *phys = frame_alloc(1);
        if (!phys) {
            serial_print("expand_heap: frame_alloc failed\n");
            return NULL;
        }

        uint64_t virt = heap_virt_cursor;
        heap_virt_cursor += 4096;
        map_page((uint64_t *)heap_pml4_phys, virt, phys, HEAP_PAGE_FLAGS);

        HEAP_LOG("expand_heap: mapped phys="); HEAP_LOG_HEX((uint64_t)phys);
        HEAP_LOG(" virt=");                    HEAP_LOG_HEX(virt);
        HEAP_LOG("\n");
    }

    // Single node spanning all allocated frames
    struct heap_node *node = (struct heap_node *)virt_start;
    node->size     = (frames_needed * 4096) - sizeof(struct heap_node);
    node->used     = false;
    node->fl_prev  = node->fl_next  = NULL;
    node->all_prev = node->all_next = NULL;
    all_append(node);
    fl_insert(node);

    return node;
}

// Initialize the kernel heap; capture the current PML4 and pre-fault one frame
void heap_init() {
    for (int i = 0; i < NUM_BUCKETS; i++) {
        free_buckets[i].first = NULL;
        free_buckets[i].last  = NULL;
    }
    all_first = all_last = NULL;

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    heap_pml4_phys = cr3 & ~0xFFFULL;

    HEAP_LOG("heap_init: pml4="); HEAP_LOG_HEX(heap_pml4_phys); HEAP_LOG("\n");

    expand_heap(0);

    serial_print("heap initialized\n");
}

// Allocate a block of at least 'size' bytes from the heap
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    const size_t alignment = 16;
    size = (size + (alignment - 1)) & ~(alignment - 1);

    for (int b = bucket_for_size(size); b < NUM_BUCKETS; b++) {
        for (struct heap_node *node = free_buckets[b].first; node; node = node->fl_next) {
            if (node->size < size) continue;

            size_t remaining = node->size - size;
            if (remaining >= sizeof(struct heap_node) + alignment) {
                fl_remove(node);
                node->size = size;

                struct heap_node *split =
                    (struct heap_node *)((uint8_t *)NODE_DATA(node) + size);
                split->size  = remaining - sizeof(struct heap_node);
                split->used  = false;
                split->fl_prev = split->fl_next = NULL;
                all_insert_after(node, split);
                fl_insert(split);
            } else {
                fl_remove(node);
            }

            node->used = true;
            HEAP_LOG("kmalloc: size="); HEAP_LOG_NUM(size);
            HEAP_LOG(" ptr=");          HEAP_LOG_HEX((uint64_t)NODE_DATA(node));
            HEAP_LOG("\n");
            return NODE_DATA(node);
        }
    }

    serial_print("kmalloc: no free block, expanding heap\n");
    if (!expand_heap(size)) {
        serial_print("kmalloc: out of memory\n");
        return NULL;
    }

    return kmalloc(size);
}

// Release a previously allocated block and coalesce adjacent free blocks
void kfree(void *ptr) {
    if (!ptr) return;

    struct heap_node *node = NODE_FROM_DATA(ptr);

    HEAP_LOG("kfree: ptr=");  HEAP_LOG_HEX((uint64_t)ptr);
    HEAP_LOG(" size=");       HEAP_LOG_NUM(node->size);
    HEAP_LOG("\n");

    node->used = false;

    // Coalesce with the previous node if it is also free.
    // We must remove both from their current buckets before resizing so
    // the bucket index re-calculation stays correct.
    if (node->all_prev && !node->all_prev->used) {
        struct heap_node *prev = node->all_prev;
        fl_remove(prev);

        prev->size += sizeof(struct heap_node) + node->size;
        all_remove(node);
        node = prev;

        HEAP_LOG("kfree: coalesced with prev, new size="); HEAP_LOG_NUM(node->size); HEAP_LOG("\n");
    }

    // Coalesce with the next node if it is also free
    if (node->all_next && !node->all_next->used) {
        struct heap_node *next = node->all_next;
        fl_remove(next);

        node->size += sizeof(struct heap_node) + next->size;
        all_remove(next);

        HEAP_LOG("kfree: coalesced with next, new size="); HEAP_LOG_NUM(node->size); HEAP_LOG("\n");
    }

    fl_insert(node);
}