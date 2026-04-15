#pragma once

#include <stdint.h>

enum StorageDevType {
    AHCI = 0,
};

void vfs_init(void);
uint8_t vfs_mount(enum StorageDevType type, uint8_t controller, uint8_t port);
void vfs_unmount(void);
uint8_t vfs_read(const char *path, void *buffer, uint32_t size);
uint8_t vfs_write(const char *path, const void *buffer, uint32_t size);
uint8_t vfs_format(enum StorageDevType type, uint8_t controller, uint8_t port, uint32_t total_sectors);