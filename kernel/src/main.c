#include <stdint.h>
#include <stdbool.h>
#include <limine.h>
#include <x86_64/gdt.h>
#include <x86_64/idt.h>
#include <x86_64/interrupts/pit.h>
#include <allocators/frame.h>
#include <allocators/hhdm.h>
#include <allocators/paging.h>

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
volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

// The following will be our kernel's entry point.
// If renaming kmain() to something else, make sure to change the
// linker script accordingly.
void kmain(void) {
    __asm__ volatile ("cli");
    // Ensure the bootloader actually understands our base revision (see spec). 
    if (LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision) == false) {
        for (;;) {
            asm ("hlt");
        }
    }

    gdt_init();
    idt_init();
    pit_init(100);
    hhdm_init();
    frame_init();
    paging_init();

    // We're done, just hang...
    for (;;) {
        asm ("hlt");
    }
}
