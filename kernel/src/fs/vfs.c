#include <stdint.h>
#include <fs/vfs.h>

void vfs_init(void) { }

uint8_t vfs_mount(enum StorageDevType type, uint8_t controller, uint8_t port) {
    (void)type;
    (void)controller;
    (void)port;
    return 0;
}

void vfs_unmount(void) { }

uint8_t vfs_read(const char *path, void *buffer, uint32_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    return 1;
}

uint8_t vfs_write(const char *path, const void *buffer, uint32_t size) {
    (void)path;
    (void)buffer;
    (void)size;
    return 1;
}

uint8_t vfs_format(enum StorageDevType type, uint8_t controller, uint8_t port, uint32_t total_sectors) {
    (void)type;
    (void)controller;
    (void)port;
    (void)total_sectors;
    return 1;
}
