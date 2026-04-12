
#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include <x86_64/gdt.h>
#include <x86_64/idt.h>
#include <x86_64/interrupts/pit.h>
#include <memory/frame.h>
#include <memory/hhdm.h>
#include <memory/paging.h>
#include <logging/printk.h>

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
        log_fatal("Bootloader does not support base revision 5\n");
        for (;;) asm ("hlt");
    }

    log_info("Bootloader base revision OK\n");

    gdt_init();
    log_info("GDT initialized\n");

    idt_init();
    log_info("IDT initialized\n");

    pit_init(100);
    log_info("PIT initialized at 100 Hz\n");

    hhdm_init();
    log_info("HHDM initialized\n");

    frame_init();
    log_info("Frame allocator initialized\n");

    paging_init();
    log_info("Paging initialized\n");

    log_debug("Kernel init complete, halting\n");

    for (;;) asm ("hlt");
}