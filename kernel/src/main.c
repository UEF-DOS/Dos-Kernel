#include "x86_64/serial.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <limine.h>
#include <x86_64/gdt.h>
#include <x86_64/idt.h>
#include <x86_64/allocator/pmm.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/apic.h>
#include <x86_64/drivers/block/ide.h>
#include <x86_64/drivers/fs/fat12.h>
#include <x86_64/drivers/fs/vfs.h>

// Set the base revision to 5, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(5);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0
};


// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void) {
    for (;;) {
        asm ("hlt");
    }
}

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    serial_init();
    // Ensure the bootloader actually understands our base revision (see spec). 
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        hcf();
    }

    // Ensure we got a framebuffer.
    if (framebuffer_request.response == NULL
     || framebuffer_request.response->framebuffer_count < 1) {
        hcf();
    }
    
    if (memmap_request.response == NULL) {
        hcf();
    }
    
    if (hhdm_request.response == NULL) {
        hcf();
    }

    // Fetch the first framebuffer.
    struct limine_framebuffer *framebuffer = framebuffer_request.response->framebuffers[0];
    
    serial_print("Initializing GDT\n");
    gdt_init();
    serial_print("Initializing IDT\n");
    idt_init();
    
    serial_print("Initializing PMM\n");
    frame_allocator_init(memmap_request.response, hhdm_request.response->offset);
    serial_print("Initializing VMM\n");
    vmm_init();
    serial_print("Initializing HEAP\n");
    heap_init();

    serial_print("Enabling APIC\n");
    uintptr_t apic_base = cpu_get_apic_base();
    cpu_set_apic_base(apic_base);
    apic_map();
    enable_apic();

#ifdef APIC_TIMER_ENABLED
    // Calibrate the APIC timer using the configured frequency (defaults to 100Hz).
    apic_calibrate_timer(APIC_TIMER_FREQUENCY);
#endif

    serial_print("Initializing IDE\n");
    ide_initialize(0, 0, 0, 0, 0);
    serial_print("Initializing FAT12\n");
    fat12_init(0);

    serial_print("Initializing VFS\n");
    vfs_init();
    if (vfs_write_file("/hello.txt", "Hello, world!", 13) == 0) {
        serial_print("Failed to write file to VFS\n");
    } else {
        serial_print("Write OK\n");
    }
    vfs_node_t *node = vfs_open("/hello.txt");
    if (node) {
        uint32_t size;
        char *data = vfs_read_file(node, &size);
        if (data) {
            serial_print("Read from VFS: ");
            serial_print(data);
            serial_print("\n");
        } else {
            serial_print("Failed to read file from VFS\n");
        } 
        vfs_close(node);
    } else {
        serial_print("Failed to open file from VFS\n");
    }

    serial_print("DONE\n");

    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    for (size_t i = 0; i < 100; i++) {
        volatile uint32_t *fb_ptr = framebuffer->address;
        fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
    }

    // We're done, just hang...
    hcf();
}
