#include <stdint.h>
#include <stdbool.h>
#include <x86_64/commands.h>
#include <x86_64/allocator/addr.h>
#include <x86_64/allocator/vmm.h>
#include <x86_64/apic.h>

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_APIC_BASE_MSR_BSP 0x100
#define IA32_APIC_BASE_MSR_ENABLE 0x800

#define APIC_LVT_TIMER   0x320
#define APIC_TIMER_DIV   0x3E0
#define APIC_TIMER_INIT  0x380
#define APIC_TIMER_CURRENT 0x390

#define PIT_FREQ 1193182u

uintptr_t cpu_get_apic_base(void);

static inline volatile uint32_t *apic_reg(uint32_t reg) {
    return (volatile uint32_t *)phys_to_virt(cpu_get_apic_base() + reg);
}

static inline uint32_t read_register(uint32_t reg) {
    return *apic_reg(reg);
}

static inline void write_reg(uint32_t reg, uint32_t value) {
    *apic_reg(reg) = value;
}

void cpu_set_apic_base(uintptr_t apic) {
    uint32_t edx = 0;
    uint32_t eax = (apic & 0xfffff0000) | IA32_APIC_BASE_MSR_ENABLE;
    #ifdef __HAVE_SPECULATION_SAFE_VALUE
        edx = (apic >> 32) & 0x0f;
    #endif

    write_msr(IA32_APIC_BASE_MSR, eax | ((uint64_t)edx << 32));
}

uintptr_t cpu_get_apic_base() {
   uint32_t eax, edx;
   read_msr(IA32_APIC_BASE_MSR, &eax, &edx);

    #ifdef __PHYSICAL_MEMORY_EXTENSION__
       return (eax & 0xfffff000) | ((edx & 0x0f) << 32);
    #else
       return (eax & 0xfffff000);
    #endif
}

void apic_map(void) {
    uintptr_t phys = cpu_get_apic_base();

    uint64_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)phys_to_virt(cr3);

    void *virt = phys_to_virt((uint64_t)phys);
    vmm_map_range(pml4, (uint64_t)virt, (void *)phys, 0x1000, 0x3);
}

void apic_eoi() {
    write_reg(0xB0, 0);
}

static void pit_set_divisor(uint16_t divisor) {
    // Channel 0, access mode: lobyte/hibyte, mode 2 (rate generator), binary
    outb(0x43, 0x34);
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
}

static uint16_t pit_read_count(void) {
    // Latch count, then read low/high
    outb(0x43, 0x00);
    uint8_t lo = inb(0x40);
    uint8_t hi = inb(0x40);
    return (uint16_t)hi << 8 | lo;
}

void apic_timer_init(uint32_t initial_count, bool periodic) {
    // Divide by 16 (common stable setting)
    write_reg(APIC_TIMER_DIV, 0x3);

    uint32_t mode = periodic ? (1 << 17) : 0;
    write_reg(APIC_LVT_TIMER, APIC_TIMER_VECTOR | mode);
    write_reg(APIC_TIMER_INIT, initial_count);
}

uint32_t apic_calibrate_timer(uint32_t target_hz) {
    // Use the PIT as a stable timebase (1193182 Hz).
    // Measure APIC ticks during a fixed PIT interval.
    pit_set_divisor(0xFFFF);

    // Start the APIC timer as one-shot at max count
    write_reg(APIC_TIMER_DIV, 0x3);
    write_reg(APIC_LVT_TIMER, APIC_TIMER_VECTOR);
    write_reg(APIC_TIMER_INIT, 0xFFFFFFFF);

    uint16_t start = pit_read_count();
    const uint16_t desired_ticks = 0x8000; // ~34ms

    while (1) {
        uint16_t now = pit_read_count();
        uint16_t elapsed = (uint16_t)(start - now);
        if (elapsed >= desired_ticks) break;
    }

    uint32_t current = read_register(APIC_TIMER_CURRENT);
    uint32_t elapsed_apic = 0xFFFFFFFFu - current;

    uint64_t ticks_per_sec = (uint64_t)elapsed_apic * PIT_FREQ / desired_ticks;
    uint32_t init = ticks_per_sec / target_hz;

    apic_timer_init(init, true);
    return init;
}

void enable_apic() {
    /* Hardware enable the Local APIC if it wasn't enabled */
    cpu_set_apic_base(cpu_get_apic_base());

    /* Set the Spurious Interrupt Vector Register bit 8 to start receiving interrupts */
    write_reg(0xF0, read_register(0xF0) | 0x100);
}