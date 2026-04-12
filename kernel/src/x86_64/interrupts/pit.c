#include <x86_64/interrupts/pit.h>
#include <commands.h>

static volatile uint64_t ticks = 0;

void pit_init(uint32_t hz) {
    if (hz == 0 || hz > PIT_BASE_HZ) {
        hz = PIT_BASE_HZ;
    }
    uint16_t divisor = (uint16_t)(PIT_BASE_HZ / hz);
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}

uint64_t pit_get_ticks() {
    return ticks;
}

void pit_tick() {
    ticks++;
}