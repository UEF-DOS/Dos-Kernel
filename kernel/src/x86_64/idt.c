#include <stdint.h>
#include <stdbool.h>
#include <x86_64/commands.h>
#include <x86_64/apic.h>
#include <x86_64/serial.h>
#include <x86_64/drivers/keyboard/ps2_keyboard.h>
#include <x86_64/idt.h>

extern uint64_t int128_handler();

#define APIC_TIMER_VECTOR   0x20
#define PS2_KEYBOARD_VECTOR 0x21

typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  ist;
    uint8_t  attributes;
    uint16_t isr_mid;
    uint32_t isr_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t vector;
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) interrupt_frame_t;

__attribute__((aligned(0x10)))
static idt_entry_t idt[256];
static idtr_t idtr;
static bool vectors[256];

extern void *isr_stub_table[];

static const char *exception_names[] = {
    "Divide Error",                 // 0
    "Debug",                        // 1
    "NMI",                          // 2
    "Breakpoint",                   // 3
    "Overflow",                     // 4
    "Bound Range Exceeded",         // 5
    "Invalid Opcode",               // 6
    "Device Not Available",         // 7
    "Double Fault",                 // 8
    "Coprocessor Segment Overrun",  // 9
    "Invalid TSS",                  // 10
    "Segment Not Present",          // 11
    "Stack-Segment Fault",          // 12
    "General Protection Fault",     // 13
    "Page Fault",                   // 14
    "Reserved",                     // 15
    "x87 FPU Error",                // 16
    "Alignment Check",              // 17
    "Machine Check",                // 18
    "SIMD FP Exception",            // 19
    "Virtualization Exception",     // 20
    "Control Protection Exception", // 21
};

void idt_set_descriptor(uint8_t vector, void *isr, uint8_t flags) {
    idt_entry_t *d  = &idt[vector];
    d->isr_low      = (uint64_t)isr & 0xFFFF;
    d->kernel_cs    = 0x08;
    d->ist          = 0;
    d->attributes   = flags;
    d->isr_mid      = ((uint64_t)isr >> 16) & 0xFFFF;
    d->isr_high     = ((uint64_t)isr >> 32) & 0xFFFFFFFF;
    d->reserved     = 0;
}

void idt_init() {
    idtr.base  = (uintptr_t)&idt[0];
    idtr.limit = sizeof(idt_entry_t) * 256 - 1;

    for (uint8_t v = 0; v < 32; v++) {
        idt_set_descriptor(v, isr_stub_table[v], 0x8E);
        vectors[v] = true;
    }

    idt_set_descriptor(APIC_TIMER_VECTOR,   isr_stub_table[APIC_TIMER_VECTOR],   0x8E);
    idt_set_descriptor(PS2_KEYBOARD_VECTOR, isr_stub_table[PS2_KEYBOARD_VECTOR], 0x8E);
    idt_set_descriptor(128, (void *)int128_handler, 0xEF);
    vectors[APIC_TIMER_VECTOR] = vectors[PS2_KEYBOARD_VECTOR] = vectors[128] = true;

    ioapic_set_irq(1, 0, PS2_KEYBOARD_VECTOR);

    outb(0xA1, 0xFF);
    outb(0x21, 0xFF);

    __asm__ volatile ("lidt %0" :: "m"(idtr));
    __asm__ volatile ("sti");
}

static void print_pf_error(uint64_t err) {
    serial_print("  PF cause: ");
    serial_print(err & 1  ? "protection-violation"  : "non-present-page");
    serial_print(err & 2  ? " | write"   : " | read");
    serial_print(err & 4  ? " | user"    : " | kernel");
    if (err & 8)  serial_print(" | reserved-bit-set");
    if (err & 16) serial_print(" | instruction-fetch");
    if (err & 32) serial_print(" | protection-key");
    serial_print("\n");
}

static void print_gp_error(uint64_t err) {
    if (err == 0) {
        serial_print("  GP cause: null selector or unclassified\n");
        return;
    }
    serial_print("  GP selector: ");
    serial_print_hex(err & ~0x7ULL);
    serial_print(err & 1 ? " (external)" : "");
    serial_print(err & 2 ? " (IDT)"      : "");
    serial_print("\n");
}

__attribute__((noreturn))
static void exception_handler(interrupt_frame_t *f) {
    uint8_t vec = (uint8_t)f->vector;

    serial_print("\n=== EXCEPTION ===\n");
    serial_print("Vector : "); serial_print_num(vec); serial_print(" — ");
    serial_print(vec < 22 ? exception_names[vec] : "Unknown");
    serial_print("\n");

    if (vec == 14) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        serial_print("  CR2 (fault addr): "); serial_print_hex(cr2); serial_print("\n");
        print_pf_error(f->error_code);
    } else if (vec == 13) {
        print_gp_error(f->error_code);
    } else if (f->error_code) {
        serial_print("  Error code: "); serial_print_hex(f->error_code); serial_print("\n");
    }

    serial_print("\nRegisters:\n");
    serial_print("  RIP: "); serial_print_hex(f->rip);    serial_print("\n");
    serial_print("  CS : "); serial_print_hex(f->cs);     serial_print("\n");
    serial_print("  FLG: "); serial_print_hex(f->rflags); serial_print("\n");

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    serial_print("  CR3: "); serial_print_hex(cr3); serial_print("\n");

    serial_print("  RAX: "); serial_print_hex(f->rax); serial_print("\n");
    serial_print("  RBX: "); serial_print_hex(f->rbx); serial_print("\n");
    serial_print("  RCX: "); serial_print_hex(f->rcx); serial_print("\n");
    serial_print("  RDX: "); serial_print_hex(f->rdx); serial_print("\n");
    serial_print("  RSI: "); serial_print_hex(f->rsi); serial_print("\n");
    serial_print("  RDI: "); serial_print_hex(f->rdi); serial_print("\n");
    serial_print("  RBP: "); serial_print_hex(f->rbp); serial_print("\n");
    serial_print("  R8 : "); serial_print_hex(f->r8);  serial_print("\n");
    serial_print("  R9 : "); serial_print_hex(f->r9);  serial_print("\n");
    serial_print("  R10: "); serial_print_hex(f->r10); serial_print("\n");
    serial_print("  R11: "); serial_print_hex(f->r11); serial_print("\n");
    serial_print("  R12: "); serial_print_hex(f->r12); serial_print("\n");
    serial_print("  R13: "); serial_print_hex(f->r13); serial_print("\n");
    serial_print("  R14: "); serial_print_hex(f->r14); serial_print("\n");
    serial_print("  R15: "); serial_print_hex(f->r15); serial_print("\n");

    if ((f->cs & 3) != 3) {
        serial_print("  (ring-0 fault, RSP/SS not pushed by CPU)\n");
    } else {
        serial_print("  RSP: "); serial_print_hex(f->rsp); serial_print("\n");
        serial_print("  SS : "); serial_print_hex(f->ss);  serial_print("\n");
    }

    for (;;) __asm__ volatile ("cli; hlt");
}

void isr_handler(interrupt_frame_t *f) {
    switch (f->vector) {
        case APIC_TIMER_VECTOR:
            apic_eoi();
            break;
        case PS2_KEYBOARD_VECTOR:
            keyboard_handler();
            apic_eoi();
            break;
        default:
            exception_handler(f);
            break;
    }
}