#include <x86_64/allocator/heap.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/serial.h>
#include <stdbool.h>

// Metadata stored immediately before each allocation's data region
struct linked_list_node {
    struct linked_list_node *next;
    struct linked_list_node *prev;
    size_t size;
    bool   used;
};

// One doubly-linked free list per size bucket
struct linked_list {
    struct linked_list_node *first;
    struct linked_list_node *last;
};

// Segregated size classes: [8,64), [64,256), [256,1024), [1024,∞)
#define NUM_BUCKETS 4
static const size_t bucket_thresholds[NUM_BUCKETS] = { 64, 256, 1024, SIZE_MAX };
static struct linked_list free_lists[NUM_BUCKETS];

// Global list of every node (used + free) for coalescing during kfree
static struct linked_list all_nodes;

// Virtual address region reserved for the kernel heap.
// Adjust base and size to fit your memory map.
#define HEAP_VIRT_BASE 0xFFFF900000000000ULL
#define HEAP_VIRT_SIZE 0x0000100000000000ULL

// Cursor into the heap VA region; bumped by one page each time expand_heap maps a new frame
static uint64_t heap_virt_cursor = HEAP_VIRT_BASE;

// PML4 used for all heap mappings; set during heap_init from the current CR3
static uint64_t *heap_pml4 = NULL;

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

#define NODE_DATA(node)      ((void *)((uint8_t *)(node) + sizeof(struct linked_list_node)))
#define NODE_FROM_DATA(ptr)  ((struct linked_list_node *)((uint8_t *)(ptr) - sizeof(struct linked_list_node)))

static int bucket_for_size(size_t size) {
    for (int i = 0; i < NUM_BUCKETS - 1; i++)
        if (size < bucket_thresholds[i]) return i;
    return NUM_BUCKETS - 1;
}

static void free_list_insert(struct linked_list_node *node) {
    int b = bucket_for_size(node->size);
    node->next = free_lists[b].first;
    if (free_lists[b].first) free_lists[b].first->prev = node;
    else                      free_lists[b].last = node;
    free_lists[b].first = node;
    node->prev = NULL;
}

static void free_list_remove(struct linked_list_node *node) {
    int b = bucket_for_size(node->size);
    if (node->prev) node->prev->next = node->next;
    else            free_lists[b].first = node->next;
    if (node->next) node->next->prev = node->prev;
    else            free_lists[b].last = node->prev;
    node->next = node->prev = NULL;
}

static void linked_list_init(struct linked_list *list) {
    list->first = NULL;
    list->last  = NULL;
}

static void all_nodes_append(struct linked_list_node *node) {
    node->prev = all_nodes.last;
    node->next = NULL;
    if (all_nodes.last) all_nodes.last->next = node;
    else                all_nodes.first = node;
    all_nodes.last = node;
}

// Allocate one or more frames, map them into the heap VA region via the VMM.
static struct linked_list_node *expand_heap(size_t size) {
    size_t total         = size + sizeof(struct linked_list_node);
    size_t frames_needed = (total + 4095) / 4096;

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
        map_page(heap_pml4, virt, phys, HEAP_PAGE_FLAGS);

        HEAP_LOG("expand_heap: mapped phys="); HEAP_LOG_HEX((uint64_t)phys);
        HEAP_LOG(" virt=");                    HEAP_LOG_HEX(virt);
        HEAP_LOG("\n");
    }

    // Single node spanning all allocated frames
    struct linked_list_node *node = (struct linked_list_node *)virt_start;
    node->size = (frames_needed * 4096) - sizeof(struct linked_list_node);
    node->used = false;
    all_nodes_append(node);
    free_list_insert(node);

    return node;
}

// Initialize the kernel heap; capture the current PML4 and pre-fault one frame
void heap_init() {
    linked_list_init(&all_nodes);
    for (int i = 0; i < NUM_BUCKETS; i++) linked_list_init(&free_lists[i]);

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    heap_pml4 = (uint64_t *)(cr3 & ~0xFFFULL);

    HEAP_LOG("heap_init: pml4="); HEAP_LOG_HEX((uint64_t)heap_pml4); HEAP_LOG("\n");

    expand_heap(0);

    serial_print("heap initialized\n");
}

// Allocate a block of at least 'size' bytes from the heap
void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    const size_t alignment = 8;
    size = (size + (alignment - 1)) & ~(alignment - 1);

    for (int b = bucket_for_size(size); b < NUM_BUCKETS; b++) {
        for (struct linked_list_node *node = free_lists[b].first; node; node = node->next) {
            if (node->size < size) continue;

            size_t remaining = node->size - size;
            if (remaining >= sizeof(struct linked_list_node) + 8) {
                free_list_remove(node);
                node->size = size;

                struct linked_list_node *split =
                    (struct linked_list_node *)((uint8_t *)NODE_DATA(node) + size);
                split->size = remaining - sizeof(struct linked_list_node);
                split->used = false;
                split->next = node->next;
                split->prev = node;
                if (node->next) node->next->prev = split;
                else            all_nodes.last = split;
                node->next = split;
                free_list_insert(split);
            } else {
                free_list_remove(node);
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

    struct linked_list_node *node = NODE_FROM_DATA(ptr);

    HEAP_LOG("kfree: ptr=");  HEAP_LOG_HEX((uint64_t)ptr);
    HEAP_LOG(" size=");       HEAP_LOG_NUM(node->size);
    HEAP_LOG("\n");

    node->used = false;

    // Coalesce with the previous node if it is also free.
    // We must remove both from their current buckets before resizing so
    // the bucket index re-calculation stays correct.
    if (node->prev && !node->prev->used) {
        struct linked_list_node *prev = node->prev;
        free_list_remove(prev);

        prev->size += sizeof(struct linked_list_node) + node->size;
        prev->next  = node->next;
        if (node->next) node->next->prev = prev;
        else            all_nodes.last = prev;

        node = prev;

        HEAP_LOG("kfree: coalesced with prev, new size="); HEAP_LOG_NUM(node->size); HEAP_LOG("\n");
    }

    // Coalesce with the next node if it is also free
    if (node->next && !node->next->used) {
        struct linked_list_node *next = node->next;
        free_list_remove(next);

        node->size += sizeof(struct linked_list_node) + next->size;
        node->next  = next->next;
        if (next->next) next->next->prev = node;
        else            all_nodes.last = node;

        HEAP_LOG("kfree: coalesced with next, new size="); HEAP_LOG_NUM(node->size); HEAP_LOG("\n");
    }

    free_list_insert(node);
}