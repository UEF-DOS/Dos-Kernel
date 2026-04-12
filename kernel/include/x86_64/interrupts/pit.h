#pragma once

#include <stdint.h>

#define PIT_BASE_HZ  1193182
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

void pit_init(uint32_t hz);
void pit_tick();
uint64_t pit_get_ticks();
