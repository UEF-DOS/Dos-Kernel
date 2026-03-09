#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

#define APIC_TIMER_VECTOR 0x20

void cpu_set_apic_base(uintptr_t apic);
uintptr_t cpu_get_apic_base();
void apic_eoi();
void apic_map();
void ioapic_set_irq(uint8_t irq, uint64_t apic_id, uint8_t vector);
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t apic_id);
void apic_timer_init(uint32_t initial_count, bool periodic);
uint32_t apic_calibrate_timer(uint32_t target_hz);
void enable_apic();
void sleep_timer_ms(uint64_t ms);

#endif