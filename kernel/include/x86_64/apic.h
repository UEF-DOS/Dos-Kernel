#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

#define APIC_TIMER_VECTOR 0x20

void cpu_set_apic_base(uintptr_t apic);
uintptr_t cpu_get_apic_base(void);
void apic_eoi(void);
void apic_map(void);
void apic_timer_init(uint32_t initial_count, bool periodic);
uint32_t apic_calibrate_timer(uint32_t target_hz);
void enable_apic(void);

#endif