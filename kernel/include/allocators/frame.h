#pragma once

#include <stdint.h>

uint64_t frame_alloc();
void frame_free(uint64_t ptr);
void frame_init();