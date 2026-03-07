#ifndef FAT12_H
#define FAT12_H

#include <stdint.h>

#define FAT12_MAX_FILES 256

typedef struct {
    char name[13];
    uint8_t attr;
    uint32_t size;
    uint16_t first_cluster;
} fat12_file_info_t;

void     fat12_init(uint8_t drive);
uint16_t fat12_file_count(uint8_t drive);
uint16_t fat12_list_root(uint8_t drive, fat12_file_info_t *out, uint16_t max);
uint16_t fat12_list_dir(uint8_t drive, uint16_t start_cluster, fat12_file_info_t *out, uint16_t max);
uint8_t  fat12_find_path(uint8_t drive, const char *path, fat12_file_info_t *out);
uint8_t  fat12_delete_file(uint8_t drive, const char *name);
uint8_t  fat12_create_file(uint8_t drive, const char *filename, const uint8_t *data, uint8_t attribute, uint32_t size);
uint8_t  fat12_create_directory(uint8_t drive, const char *path);
const char *fat12_read_file(uint8_t drive, const char *name);
const char *fat12_read_file_by_info(uint8_t drive, const fat12_file_info_t *info);

#endif