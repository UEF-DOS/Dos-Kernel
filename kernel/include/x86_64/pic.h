#pragma once

#include <stdint.h>

void pic_init();
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);
void pic_disable();
uint16_t pic_get_irr();
uint16_t pic_get_isr();