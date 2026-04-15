#pragma once

#include <stdint.h>
#include "vfs.h"

uint8_t fat16_init(enum StorageDevType type, uint8_t controller, uint8_t port);
uint8_t fat16_read(enum StorageDevType type, uint8_t controller, uint8_t port, uint32_t lba, uint8_t count, void *buffer);
uint8_t fat16_format(enum StorageDevType type, uint8_t controller, uint8_t port, uint32_t total_sectors);