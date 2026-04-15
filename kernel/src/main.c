#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include <memory.h>
#include <x86_64/gdt.h>
#include <x86_64/idt.h>
#include <memory/frame.h>
#include <memory/hhdm.h>
#include <memory/paging.h>
#include <logging/printk.h>
#include <x86_64/apic.h>
#include <fs/fat16.h>
#include <storage/ahci.h>

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(5);

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

void kmain(void) {
    __asm__ volatile ("cli");

    printk_init();

    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        log_fatal("Bootloader does not support base revision: %lu\n", limine_base_revision[0]);
        for (;;) asm ("hlt");
    }

    log_info("Bootloader base revision OK\n");

    gdt_init();
    log_info("GDT initialized\n");

    idt_init();
    log_info("IDT initialized\n");

    hhdm_init();
    log_info("HHDM initialized\n");

    frame_init();
    log_info("Frame allocator initialized\n");

    paging_init();
    log_info("Paging initialized\n");

    enable_apic(true);
    log_info("APIC enabled\n");

    apic_timer_init(1000);
    log_info("APIC timer initialized at 1000 Hz\n");

    ahci_init();
    log_info("AHCI initialized\n");

    fat16_init(AHCI, 0, 0);\
    log_info("FAT16 initialized\n");

    log_debug("Kernel init complete, halting\n");
    for (;;) asm ("hlt");
}