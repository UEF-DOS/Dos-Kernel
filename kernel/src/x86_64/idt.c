#include <stdint.h>
#include <stdbool.h>
#include <x86_64/commands.h>
#include <x86_64/apic.h>
#include <x86_64/serial.h>
#include <x86_64/idt.h>

extern uint64_t int128_handler();

#define APIC_TIMER_VECTOR 0x20

typedef struct {
	uint16_t    isr_low;      // The lower 16 bits of the ISR's address
	uint16_t    kernel_cs;    // The GDT segment selector that the CPU will load into CS before calling the ISR
	uint8_t	    ist;          // The IST in the TSS that the CPU will load into RSP; set to zero for now
	uint8_t     attributes;   // Type and attributes; see the IDT page
	uint16_t    isr_mid;      // The higher 16 bits of the lower 32 bits of the ISR's address
	uint32_t    isr_high;     // The higher 32 bits of the ISR's address
	uint32_t    reserved;     // Set to zero
} __attribute__((packed)) idt_entry_t;

typedef struct {
	uint16_t	limit;
	uint64_t	base;
} __attribute__((packed)) idtr_t;

__attribute__((aligned(0x10)))
static idt_entry_t idt[256];

static idtr_t idtr;

static bool vectors[256];

extern void* isr_stub_table[];

__attribute__((noreturn)) void exception_handler(uint8_t exception);
void isr_handler(uint8_t vector);

void idt_set_descriptor(uint8_t vector, void* isr, uint8_t flags) {
    idt_entry_t* descriptor = &idt[vector];

    descriptor->isr_low        = (uint64_t)isr & 0xFFFF;
    descriptor->kernel_cs      = 0x08;
    descriptor->ist            = 0;
    descriptor->attributes     = flags;
    descriptor->isr_mid        = ((uint64_t)isr >> 16) & 0xFFFF;
    descriptor->isr_high       = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    descriptor->reserved       = 0;
}

void idt_init() {
    idtr.base = (uintptr_t)&idt[0];
    idtr.limit = (uint16_t)sizeof(idt_entry_t) * 256 - 1;

    for (uint8_t vector = 0; vector < 32; vector++) {
        idt_set_descriptor(vector, isr_stub_table[vector], 0x8E);
        vectors[vector] = true;
    }

    // Add a handler for the APIC timer (vector 0x20)
    idt_set_descriptor(APIC_TIMER_VECTOR, isr_stub_table[APIC_TIMER_VECTOR], 0x8E);
    vectors[APIC_TIMER_VECTOR] = true;
    idt_set_descriptor(128, (void*)int128_handler, 0xEE);
    vectors[128] = true;

    // Mask the legacy PIC so we don't receive IRQs from it
    outb(0xA1, 0xFF); // slave
    outb(0x21, 0xFF); // master

    __asm__ volatile ("lidt %0" : : "m"(idtr)); // load the new IDT
    __asm__ volatile ("sti"); // set the interrupt flag
}

void isr_handler(uint8_t vector) {
    switch (vector) {
        case APIC_TIMER_VECTOR:
        #ifdef DEBUG
            serial_print(".");
        #endif
            apic_eoi();
            break;
        default:
            exception_handler(vector);
            break;
    }
}

__attribute__((noreturn))
void exception_handler(uint8_t exception) {
    serial_print("\n=== EXCEPTION ===\n");
    serial_print("Vector: ");
    serial_print_num(exception);
    serial_print("\n");

    switch (exception) {
        case 0:  serial_print("Divide Error\n"); break;
        case 6:  serial_print("Invalid Opcode\n"); break;
        case 13: serial_print("General Protection Fault\n"); break;
        case 14: {
            serial_print("Page Fault\n");
            uint64_t cr2;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
            serial_print("  Faulting address (CR2): ");
            serial_print_hex(cr2);
            serial_print("\n");
            break;
        }
        default: serial_print("Unknown Exception\n"); break;
    }

    uint64_t rsp, cr3;
    __asm__ volatile ("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    serial_print("  RSP: "); serial_print_hex(rsp); serial_print("\n");
    serial_print("  CR3: "); serial_print_hex(cr3); serial_print("\n");

    for (;;) __asm__ volatile ("cli; hlt");
}