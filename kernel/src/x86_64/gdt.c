#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static struct gdt_entry gdt_entries[3];
static struct gdt_ptr   gdt_ptr;

static void gdt_encode_entry(struct gdt_entry *entry, uint32_t base, uint32_t limit,
                              uint8_t access, uint8_t granularity) {
    entry->limit_low   = limit & 0xFFFF;
    entry->base_low    = base  & 0xFFFF;
    entry->base_middle = (base  >> 16) & 0xFF;
    entry->access      = access;
    entry->granularity = (granularity & 0xF0) | ((limit >> 16) & 0x0F);
    entry->base_high   = (base  >> 24) & 0xFF;
}

static void gdt_flush() {
    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr));

    // Reload data segments
    __asm__ volatile (
        "mov $0x10, %%ax \n"
        "mov %%ax, %%ds  \n"
        "mov %%ax, %%es  \n"
        "mov %%ax, %%fs  \n"
        "mov %%ax, %%gs  \n"
        "mov %%ax, %%ss  \n"
        : : : "ax"
    );

    // Far return to reload CS with kernel code selector (0x08)
    __asm__ volatile (
        "pushq $0x08          \n"
        "lea 1f(%%rip), %%rax \n"
        "pushq %%rax          \n"
        "lretq                \n"
        "1:                   \n"
        : : : "rax", "memory"
    );
}

void gdt_init() {
    gdt_ptr.limit = (sizeof(struct gdt_entry) * 3) - 1;
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    gdt_encode_entry(&gdt_entries[0], 0, 0,          0x00, 0x00); // null
    gdt_encode_entry(&gdt_entries[1], 0, 0xFFFFF, 0x9A, 0xA0   ); // kernel code
    gdt_encode_entry(&gdt_entries[2], 0, 0xFFFFF, 0x92, 0xC0   ); // kernel data

    gdt_flush();
}