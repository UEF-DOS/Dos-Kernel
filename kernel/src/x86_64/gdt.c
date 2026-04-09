#include <stdint.h>
#include <stddef.h>
#include <x86_64/gdt.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_tss_entry {
    uint16_t length;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  flags1;
    uint8_t  flags2;
    uint8_t  base_high;
    uint32_t base_upper;
    uint32_t reserved;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base;
} __attribute__((packed));

static struct gdt_entry    gdt_entries[7];
static struct gdt_ptr      gdt_ptr;
static struct tss_entry    tss;

#define KERNEL_IST_STACK_SIZE 0x4000
static uint8_t ist_stack[KERNEL_IST_STACK_SIZE] __attribute__((aligned(16)));

static void gdt_encode_entry(struct gdt_entry *entry, uint32_t base, uint32_t limit,
                              uint8_t access, uint8_t granularity) {
    entry->limit_low   = limit & 0xFFFF;
    entry->base_low    = base  & 0xFFFF;
    entry->base_middle = (base  >> 16) & 0xFF;
    entry->access      = access;
    entry->granularity = (granularity & 0xF0) | ((limit >> 16) & 0x0F);
    entry->base_high   = (base  >> 24) & 0xFF;
}

static void gdt_encode_tss(struct gdt_tss_entry *entry, uint64_t base, uint16_t length) {
    entry->length      = length;
    entry->base_low    = base & 0xFFFF;
    entry->base_mid    = (base >> 16) & 0xFF;
    entry->flags1      = 0x89;
    entry->flags2      = 0x00;
    entry->base_high   = (base >> 24) & 0xFF;
    entry->base_upper  = (base >> 32) & 0xFFFFFFFF;
    entry->reserved    = 0;
}

static void gdt_flush() {
    __asm__ volatile ("lgdt %0" : : "m"(gdt_ptr));

    __asm__ volatile (
        "mov $0x10, %%ax \n"
        "mov %%ax, %%ds  \n"
        "mov %%ax, %%es  \n"
        "mov %%ax, %%fs  \n"
        "mov %%ax, %%gs  \n"
        "mov %%ax, %%ss  \n"
        : : : "ax"
    );

    __asm__ volatile (
        "pushq $0x08          \n"
        "lea 1f(%%rip), %%rax \n"
        "pushq %%rax          \n"
        "lretq                \n"
        "1:                   \n"
        : : : "rax", "memory"
    );
}

static void tss_flush() {
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init() {
    tss.rsp0        = 0;
    tss.ist1        = (uint64_t)ist_stack + KERNEL_IST_STACK_SIZE;
    tss.io_map_base = sizeof(struct tss_entry);

    gdt_ptr.limit = (sizeof(struct gdt_entry) * 7) - 1;
    gdt_ptr.base  = (uint64_t)&gdt_entries;

    gdt_encode_entry(&gdt_entries[0], 0, 0,       0x00, 0x00); // null
    gdt_encode_entry(&gdt_entries[1], 0, 0xFFFFF, 0x9A, 0xA0); // kernel code  (0x08)
    gdt_encode_entry(&gdt_entries[2], 0, 0xFFFFF, 0x92, 0xC0); // kernel data  (0x10)
    gdt_encode_entry(&gdt_entries[3], 0, 0xFFFFF, 0xFA, 0xA0); // user code    (0x18)
    gdt_encode_entry(&gdt_entries[4], 0, 0xFFFFF, 0xF2, 0xC0); // user data    (0x20)
    gdt_encode_tss((struct gdt_tss_entry *)&gdt_entries[5], (uint64_t)&tss, sizeof(tss) - 1);

    gdt_flush();
    tss_flush();
}