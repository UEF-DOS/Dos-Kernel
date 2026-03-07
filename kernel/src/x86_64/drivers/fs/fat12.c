#include <stdint.h>
#include <x86_64/drivers/block/ide.h>
#include <x86_64/serial.h>
#include <memory.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/drivers/fs/fat12.h>

#define FAT12_BUF_SIZE 6144

typedef struct bpb {
    uint8_t bs_jmp_boot[3];
    uint8_t bs_oem_name[8];
    uint8_t bpb_bytes_per_sector[2];
    uint8_t bpb_sectors_per_cluster;
    uint8_t bpb_reserved_sector_count[2];
    uint8_t bpb_num_fats;
    uint8_t bpb_root_entry_count[2];
    uint8_t bpb_total_sectors_16[2];
    uint8_t bpb_media;
    uint8_t bpb_fat_size_16[2];
    uint8_t bpb_sectors_per_track[2];
    uint8_t bpb_num_heads[2];
    uint8_t bpb_hidden_sectors[4];
    uint8_t bpb_total_sectors_32[4];
    uint8_t bs_drive_number;
    uint8_t bs_reserved1;
    uint8_t bs_boot_signature;
    uint8_t bs_volume_id[4];
    uint8_t bs_volume_label[11];
    uint8_t bs_file_system_type[8];
    uint8_t bs_boot_code[448];
    uint8_t bs_signature[2];
} fat12_t;

typedef struct {
    uint8_t name[8];
    uint8_t ext[3];
    uint8_t attr;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint8_t create_time[2];
    uint8_t create_date[2];
    uint8_t last_access_date[2];
    uint8_t first_cluster_high[2];
    uint8_t write_time[2];
    uint8_t write_date[2];
    uint8_t first_cluster_low[2];
    uint8_t file_size[4];
} fat12_dir_entry_t;

typedef struct {
    uint8_t name[11];
    uint8_t attr;
    uint8_t reserved[10];
    uint8_t first_cluster[2];
    uint8_t file_size[4];
} fat12_file_entry_t;

static inline uint16_t u16_from_le(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static inline void u16_to_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static uint32_t fat12_first_data_sector(const fat12_t *fat12) {
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint16_t reserved_sectors = u16_from_le(fat12->bpb_reserved_sector_count);
    uint16_t root_entry_count = u16_from_le(fat12->bpb_root_entry_count);
    uint16_t num_fats = fat12->bpb_num_fats;
    uint16_t fat_size = u16_from_le(fat12->bpb_fat_size_16);
    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
    return reserved_sectors + (uint32_t)num_fats * fat_size + root_dir_sectors;
}

static uint32_t fat12_first_sector_of_cluster(const fat12_t *fat12, uint16_t cluster) {
    uint16_t sectors_per_cluster = fat12->bpb_sectors_per_cluster;
    uint32_t first_data_sector = fat12_first_data_sector(fat12);
    return first_data_sector + (uint32_t)(cluster - 2) * sectors_per_cluster;
}

static uint16_t fat12_read_fat_entry(const uint8_t *fat_buf, uint16_t cluster) {
    uint32_t index = (cluster * 3) / 2;
    uint16_t entry = fat_buf[index] | (uint16_t)fat_buf[index + 1] << 8;
    if (cluster & 1) entry >>= 4;
    else             entry &= 0x0FFF;
    return entry;
}

static void fat12_write_fat_entry(uint8_t *fat_buf, uint16_t cluster, uint16_t value) {
    uint32_t index = (cluster * 3) / 2;
    uint16_t entry = fat_buf[index] | (uint16_t)fat_buf[index + 1] << 8;
    if (cluster & 1) entry = (entry & 0x000F) | (value << 4);
    else             entry = (entry & 0xF000) | (value & 0x0FFF);
    fat_buf[index]     = entry & 0xFF;
    fat_buf[index + 1] = entry >> 8;
}

static void fat12_read_fat(uint8_t drive, const fat12_t *fat12, uint8_t *fat_buf) {
    uint16_t fat_size         = u16_from_le(fat12->bpb_fat_size_16);
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint32_t fat_lba          = u16_from_le(fat12->bpb_reserved_sector_count);
    for (uint16_t s = 0; s < fat_size; s++)
        ide_read_sectors(drive, 1, fat_lba + s, fat_buf + s * bytes_per_sector);
}

static void fat12_write_fat(uint8_t drive, const fat12_t *fat12, const uint8_t *fat_buf) {
    uint16_t fat_size         = u16_from_le(fat12->bpb_fat_size_16);
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint16_t num_fats         = fat12->bpb_num_fats;
    uint32_t fat_lba          = u16_from_le(fat12->bpb_reserved_sector_count);
    for (uint16_t f = 0; f < num_fats; f++) {
        uint32_t fat_lba_f = fat_lba + (uint32_t)f * fat_size;
        for (uint16_t s = 0; s < fat_size; s++)
            ide_write_sectors(drive, 1, fat_lba_f + s, (void *)(fat_buf + s * bytes_per_sector));
    }
}

static void fat12_free_cluster_chain(uint8_t drive, const fat12_t *fat12, uint16_t start_cluster) {
    uint16_t fat_size         = u16_from_le(fat12->bpb_fat_size_16);
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    static uint8_t fat_buf[FAT12_BUF_SIZE];
    if ((uint32_t)fat_size * bytes_per_sector > sizeof(fat_buf)) return;

    fat12_read_fat(drive, fat12, fat_buf);

    uint16_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0xFF8) {
        uint16_t next = fat12_read_fat_entry(fat_buf, cluster);
        fat12_write_fat_entry(fat_buf, cluster, 0x000);
        if (next >= 0xFF8) break;
        cluster = next;
    }

    fat12_write_fat(drive, fat12, fat_buf);
}

static void fat12_make_83_name(const char *name, uint8_t out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int in = 0, out_i = 0;
    while (name[in] && name[in] != '.' && out_i < 8) {
        char c = name[in++];
        out[out_i++] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    if (name[in] == '.') {
        in++;
        int ext_i = 0;
        while (name[in] && ext_i < 3) {
            char c = name[in++];
            out[8 + ext_i++] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
        }
    }
}

static void fat12_format_83_name(const uint8_t *entry, char *out, size_t out_sz) {
    int len = 0;
    for (int i = 0; i < 8 && entry[i] != ' '; i++)
        if (len + 1 < (int)out_sz) out[len++] = entry[i];
    int ext_len = 0;
    for (int i = 8; i < 11 && entry[i] != ' '; i++) ext_len++;
    if (ext_len > 0 && len + 1 < (int)out_sz) {
        out[len++] = '.';
        for (int i = 0; i < ext_len && len + 1 < (int)out_sz; i++)
            out[len++] = entry[8 + i];
    }
    if (len < (int)out_sz) out[len] = '\0';
    else if (out_sz > 0)   out[out_sz - 1] = '\0';
}

static int fat12_compare_83_name(const uint8_t *entry, const uint8_t *name11) {
    for (int i = 0; i < 11; i++)
        if (entry[i] != name11[i]) return 0;
    return 1;
}

static void fat12_fill_file_info(const fat12_dir_entry_t *entry, fat12_file_info_t *info) {
    fat12_format_83_name(entry->name, info->name, sizeof(info->name));
    info->attr          = entry->attr;
    info->size          = (uint32_t)entry->file_size[0] | ((uint32_t)entry->file_size[1] << 8) |
                          ((uint32_t)entry->file_size[2] << 16) | ((uint32_t)entry->file_size[3] << 24);
    info->first_cluster = (uint16_t)entry->first_cluster_low[0] | ((uint16_t)entry->first_cluster_low[1] << 8);
}

static uint8_t fat12_find_entry_in_dir(uint8_t drive, const fat12_t *fat12, uint16_t start_cluster,
                                        const uint8_t name11[11], fat12_file_info_t *out) {
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint16_t root_entry_count = u16_from_le(fat12->bpb_root_entry_count);
    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
    uint8_t sector[512];

    if (start_cluster == 0) {
        uint32_t root_start = fat12_first_data_sector(fat12) - root_dir_sectors;
        for (uint32_t s = 0; s < root_dir_sectors; s++) {
            ide_read_sectors(drive, 1, root_start + s, sector);
            for (uint16_t i = 0; i < bytes_per_sector; i += 32) {
                fat12_dir_entry_t *entry = (fat12_dir_entry_t *)&sector[i];
                if (entry->name[0] == 0x00) return 0;
                if (entry->name[0] == 0xE5) continue;
                if (entry->attr & 0x08)     continue;
                if (fat12_compare_83_name(entry->name, name11)) {
                    fat12_fill_file_info(entry, out);
                    return 1;
                }
            }
        }
        return 0;
    }

    uint16_t fat_size = u16_from_le(fat12->bpb_fat_size_16);
    static uint8_t fat_buf[FAT12_BUF_SIZE];
    if ((uint32_t)fat_size * bytes_per_sector > sizeof(fat_buf)) return 0;
    fat12_read_fat(drive, fat12, fat_buf);

    uint16_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0xFF8) {
        uint32_t first_sector = fat12_first_sector_of_cluster(fat12, cluster);
        for (uint16_t s = 0; s < fat12->bpb_sectors_per_cluster; s++) {
            ide_read_sectors(drive, 1, first_sector + s, sector);
            for (uint16_t i = 0; i < bytes_per_sector; i += 32) {
                fat12_dir_entry_t *entry = (fat12_dir_entry_t *)&sector[i];
                if (entry->name[0] == 0x00) return 0;
                if (entry->name[0] == 0xE5) continue;
                if (entry->attr & 0x08)     continue;
                if (fat12_compare_83_name(entry->name, name11)) {
                    fat12_fill_file_info(entry, out);
                    return 1;
                }
            }
        }
        cluster = fat12_read_fat_entry(fat_buf, cluster);
    }
    return 0;
}

static uint8_t fat12_write_dir_entry(uint8_t drive, const fat12_t *fat12,
                                      uint16_t parent_cluster, const uint8_t name11[11],
                                      uint8_t attr, uint16_t first_cluster, uint32_t size) {
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint8_t sector[512];

    if (parent_cluster == 0) {
        uint16_t root_entry_count = u16_from_le(fat12->bpb_root_entry_count);
        uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
        uint32_t root_start       = fat12_first_data_sector(fat12) - root_dir_sectors;

        for (uint32_t s = 0; s < root_dir_sectors; s++) {
            ide_read_sectors(drive, 1, root_start + s, sector);
            for (uint16_t i = 0; i < bytes_per_sector; i += 32) {
                if (sector[i] != 0x00 && sector[i] != 0xE5) continue;
                memset(&sector[i], 0, 32);
                memcpy(&sector[i], name11, 11);
                sector[i + 11] = attr;
                sector[i + 26] = first_cluster & 0xFF;
                sector[i + 27] = (first_cluster >> 8) & 0xFF;
                sector[i + 28] = (uint8_t)(size & 0xFF);
                sector[i + 29] = (uint8_t)((size >> 8) & 0xFF);
                sector[i + 30] = (uint8_t)((size >> 16) & 0xFF);
                sector[i + 31] = (uint8_t)((size >> 24) & 0xFF);
                ide_write_sectors(drive, 1, root_start + s, sector);
                return 1;
            }
        }
        return 0;
    }

    uint16_t fat_size = u16_from_le(fat12->bpb_fat_size_16);
    static uint8_t fat_buf[FAT12_BUF_SIZE];
    if ((uint32_t)fat_size * bytes_per_sector > sizeof(fat_buf)) return 0;
    fat12_read_fat(drive, fat12, fat_buf);

    uint16_t cluster = parent_cluster;
    while (cluster >= 2 && cluster < 0xFF8) {
        uint32_t first_sector = fat12_first_sector_of_cluster(fat12, cluster);
        for (uint16_t s = 0; s < fat12->bpb_sectors_per_cluster; s++) {
            ide_read_sectors(drive, 1, first_sector + s, sector);
            for (uint16_t i = 0; i < bytes_per_sector; i += 32) {
                if (sector[i] != 0x00 && sector[i] != 0xE5) continue;
                memset(&sector[i], 0, 32);
                memcpy(&sector[i], name11, 11);
                sector[i + 11] = attr;
                sector[i + 26] = first_cluster & 0xFF;
                sector[i + 27] = (first_cluster >> 8) & 0xFF;
                sector[i + 28] = (uint8_t)(size & 0xFF);
                sector[i + 29] = (uint8_t)((size >> 8) & 0xFF);
                sector[i + 30] = (uint8_t)((size >> 16) & 0xFF);
                sector[i + 31] = (uint8_t)((size >> 24) & 0xFF);
                ide_write_sectors(drive, 1, first_sector + s, sector);
                return 1;
            }
        }
        cluster = fat12_read_fat_entry(fat_buf, cluster);
    }
    return 0;
}

static void fat12_init_dir_cluster(uint8_t drive, const fat12_t *fat12,
                                    uint16_t new_cluster, uint16_t parent_cluster) {
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint32_t first_sector     = fat12_first_sector_of_cluster(fat12, new_cluster);
    uint8_t sector[512];

    memset(sector, 0, bytes_per_sector);

    fat12_dir_entry_t *dot = (fat12_dir_entry_t *)&sector[0];
    memset(dot->name, ' ', 11);
    dot->name[0]              = '.';
    dot->attr                 = 0x10;
    dot->first_cluster_low[0] = new_cluster & 0xFF;
    dot->first_cluster_low[1] = (new_cluster >> 8) & 0xFF;

    fat12_dir_entry_t *dotdot = (fat12_dir_entry_t *)&sector[32];
    memset(dotdot->name, ' ', 11);
    dotdot->name[0]              = '.';
    dotdot->name[1]              = '.';
    dotdot->attr                 = 0x10;
    dotdot->first_cluster_low[0] = parent_cluster & 0xFF;
    dotdot->first_cluster_low[1] = (parent_cluster >> 8) & 0xFF;

    ide_write_sectors(drive, 1, first_sector, sector);

    memset(sector, 0, bytes_per_sector);
    for (uint16_t s = 1; s < fat12->bpb_sectors_per_cluster; s++)
        ide_write_sectors(drive, 1, first_sector + s, sector);
}

static uint16_t fat12_list_dir_internal(uint8_t drive, const fat12_t *fat12, uint16_t start_cluster,
                                         fat12_file_info_t *out, uint16_t max) {
    if (max == 0) return 0;

    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    uint16_t root_entry_count = u16_from_le(fat12->bpb_root_entry_count);
    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;

    uint8_t sector[512];
    uint16_t found = 0;

    if (start_cluster == 0) {
        uint32_t root_start = fat12_first_data_sector(fat12) - root_dir_sectors;
        for (uint32_t s = 0; s < root_dir_sectors && found < max; s++) {
            ide_read_sectors(drive, 1, root_start + s, sector);
            for (uint16_t i = 0; i < bytes_per_sector && found < max; i += 32) {
                fat12_dir_entry_t *entry = (fat12_dir_entry_t *)&sector[i];
                if (entry->name[0] == 0x00) return found;
                if (entry->name[0] == 0xE5) continue;
                if (entry->attr & 0x08)     continue;
                fat12_fill_file_info(entry, &out[found++]);
            }
        }
        return found;
    }

    uint16_t fat_size = u16_from_le(fat12->bpb_fat_size_16);
    static uint8_t fat_buf[FAT12_BUF_SIZE];
    if ((uint32_t)fat_size * bytes_per_sector > sizeof(fat_buf)) return found;
    fat12_read_fat(drive, fat12, fat_buf);

    uint16_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0xFF8 && found < max) {
        uint32_t first_sector = fat12_first_sector_of_cluster(fat12, cluster);
        for (uint16_t s = 0; s < fat12->bpb_sectors_per_cluster && found < max; s++) {
            ide_read_sectors(drive, 1, first_sector + s, sector);
            for (uint16_t i = 0; i < bytes_per_sector && found < max; i += 32) {
                fat12_dir_entry_t *entry = (fat12_dir_entry_t *)&sector[i];
                if (entry->name[0] == 0x00) return found;
                if (entry->name[0] == 0xE5) continue;
                if (entry->attr & 0x08)     continue;
                fat12_fill_file_info(entry, &out[found++]);
            }
        }
        cluster = fat12_read_fat_entry(fat_buf, cluster);
    }
    return found;
}

uint16_t fat12_file_count(uint8_t drive) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    uint16_t bytes_per_sector = u16_from_le(fat12.bpb_bytes_per_sector);
    uint16_t root_entry_count = u16_from_le(fat12.bpb_root_entry_count);
    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
    uint32_t root_start       = fat12_first_data_sector(&fat12) - root_dir_sectors;

    uint8_t sector[512];
    uint16_t count = 0;

    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        ide_read_sectors(drive, 1, root_start + s, sector);
        for (uint16_t i = 0; i < bytes_per_sector; i += 32) {
            fat12_dir_entry_t *entry = (fat12_dir_entry_t *)&sector[i];
            if (entry->name[0] == 0x00) return count;
            if (entry->name[0] == 0xE5) continue;
            if (entry->attr & 0x08)     continue;
            count++;
        }
    }
    return count;
}

uint16_t fat12_list_root(uint8_t drive, fat12_file_info_t *out, uint16_t max) {
    return fat12_list_dir(drive, 0, out, max);
}

uint16_t fat12_list_dir(uint8_t drive, uint16_t start_cluster, fat12_file_info_t *out, uint16_t max) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);
    return fat12_list_dir_internal(drive, &fat12, start_cluster, out, max);
}

uint8_t fat12_find_path(uint8_t drive, const char *path, fat12_file_info_t *out) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    const char *p = path;
    while (*p == '/') p++;

    uint16_t current_cluster = 0;
    char component[13];

    while (*p) {
        int len = 0;
        while (*p && *p != '/' && len < 12) component[len++] = *p++;
        component[len] = '\0';
        if (*p == '/') p++;

        uint8_t name11[11];
        fat12_make_83_name(component, name11);

        fat12_file_info_t info;
        if (!fat12_find_entry_in_dir(drive, &fat12, current_cluster, name11, &info))
            return 0;

        if (*p == '\0') {
            *out = info;
            return 1;
        }

        if (!(info.attr & 0x10)) return 0;
        current_cluster = info.first_cluster;
    }
    return 0;
}

static uint16_t fat12_find_free_cluster(uint8_t drive, const fat12_t *fat12) {
    uint16_t fat_size         = u16_from_le(fat12->bpb_fat_size_16);
    uint16_t bytes_per_sector = u16_from_le(fat12->bpb_bytes_per_sector);
    static uint8_t fat_buf[FAT12_BUF_SIZE];
    uint32_t fat_bytes = (uint32_t)fat_size * bytes_per_sector;
    if (fat_bytes > sizeof(fat_buf)) return 0xFFFF;

    fat12_read_fat(drive, fat12, fat_buf);

    uint16_t max_clusters = (uint16_t)(fat_bytes * 8 / 12);
    for (uint16_t cl = 2; cl < max_clusters; cl++) {
        if (fat12_read_fat_entry(fat_buf, cl) == 0x000) {
            fat12_write_fat_entry(fat_buf, cl, 0xFFF);
            fat12_write_fat(drive, fat12, fat_buf);
            return cl;
        }
    }
    return 0xFFFF;
}

uint8_t fat12_create_directory(uint8_t drive, const char *path) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    const char *last_slash = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '/') last_slash = p;

    const char *dirname;
    char parent_path[256];

    if (last_slash) {
        size_t parent_len = (size_t)(last_slash - path);
        if (parent_len >= sizeof(parent_path)) return 0;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';
        dirname = last_slash + 1;
    } else {
        parent_path[0] = '\0';
        dirname = path;
    }

    if (!dirname[0]) return 0;

    uint16_t parent_cluster = 0;
    if (parent_path[0] != '\0') {
        fat12_file_info_t parent_info;
        if (!fat12_find_path(drive, parent_path, &parent_info)) return 0;
        if (!(parent_info.attr & 0x10)) return 0;
        parent_cluster = parent_info.first_cluster;
    }

    uint8_t name11[11];
    fat12_make_83_name(dirname, name11);

    fat12_file_info_t existing;
    if (fat12_find_entry_in_dir(drive, &fat12, parent_cluster, name11, &existing)) return 0;

    uint16_t new_cluster = fat12_find_free_cluster(drive, &fat12);
    if (new_cluster == 0xFFFF) return 0;

    fat12_init_dir_cluster(drive, &fat12, new_cluster, parent_cluster);

    return fat12_write_dir_entry(drive, &fat12, parent_cluster, name11, 0x10, new_cluster, 0);
}

uint8_t fat12_create_file(uint8_t drive, const char *filepath, const uint8_t *data,
                          uint8_t attribute, uint32_t size) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    const char *last_slash = NULL;
    for (const char *p = filepath; *p; p++)
        if (*p == '/') last_slash = p;

    const char *filename;
    char parent_path[256];

    if (last_slash) {
        size_t parent_len = (size_t)(last_slash - filepath);
        if (parent_len >= sizeof(parent_path)) return 0;
        memcpy(parent_path, filepath, parent_len);
        parent_path[parent_len] = '\0';
        filename = last_slash + 1;
    } else {
        parent_path[0] = '\0';
        filename = filepath;
    }

    if (!filename[0]) return 0;

    uint16_t parent_cluster = 0;
    if (parent_path[0] != '\0') {
        fat12_file_info_t parent_info;
        if (!fat12_find_path(drive, parent_path, &parent_info)) return 0;
        if (!(parent_info.attr & 0x10)) return 0;
        parent_cluster = parent_info.first_cluster;
    }

    uint8_t name11[11];
    fat12_make_83_name(filename, name11);

    uint16_t bytes_per_sector    = u16_from_le(fat12.bpb_bytes_per_sector);
    uint16_t sectors_per_cluster = fat12.bpb_sectors_per_cluster;
    uint16_t fat_size            = u16_from_le(fat12.bpb_fat_size_16);

    static uint8_t fat_buf[FAT12_BUF_SIZE];
    if ((uint32_t)fat_size * bytes_per_sector > sizeof(fat_buf)) return 0;
    fat12_read_fat(drive, &fat12, fat_buf);

    uint32_t cluster_size     = (uint32_t)sectors_per_cluster * bytes_per_sector;
    uint32_t clusters_needed  = size == 0 ? 1 : (size + cluster_size - 1) / cluster_size;
    uint16_t max_clusters     = (uint16_t)((uint32_t)fat_size * bytes_per_sector * 8 / 12);

    uint16_t first_cluster = 0xFFFF;
    uint16_t prev_cluster  = 0xFFFF;

    for (uint32_t c = 0; c < clusters_needed; c++) {
        uint16_t cl = 0xFFFF;
        for (uint16_t i = 2; i < max_clusters; i++) {
            if (fat12_read_fat_entry(fat_buf, i) == 0x000) {
                cl = i;
                break;
            }
        }
        if (cl == 0xFFFF) {
            if (first_cluster != 0xFFFF)
                fat12_free_cluster_chain(drive, &fat12, first_cluster);
            return 0;
        }
        fat12_write_fat_entry(fat_buf, cl, 0xFFF);
        if (prev_cluster != 0xFFFF)
            fat12_write_fat_entry(fat_buf, prev_cluster, cl);
        else
            first_cluster = cl;
        prev_cluster = cl;
    }

    fat12_write_fat(drive, &fat12, fat_buf);

    uint32_t remaining = size;
    const uint8_t *ptr = data;
    uint16_t cluster   = first_cluster;
    uint8_t sector[512];

    while (cluster >= 2 && cluster < 0xFF8) {
        uint32_t first_sector = fat12_first_sector_of_cluster(&fat12, cluster);
        for (uint16_t s = 0; s < sectors_per_cluster; s++) {
            memset(sector, 0, bytes_per_sector);
            if (remaining > 0) {
                uint32_t to_copy = remaining > bytes_per_sector ? bytes_per_sector : remaining;
                memcpy(sector, ptr, to_copy);
                ptr       += to_copy;
                remaining -= to_copy;
            }
            ide_write_sectors(drive, 1, first_sector + s, sector);
        }
        cluster = fat12_read_fat_entry(fat_buf, cluster);
    }

    return fat12_write_dir_entry(drive, &fat12, parent_cluster, name11, attribute, first_cluster, size);
}

uint8_t fat12_delete_file(uint8_t drive, const char *name) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    uint8_t name11[11];
    fat12_make_83_name(name, name11);

    uint16_t bytes_per_sector = u16_from_le(fat12.bpb_bytes_per_sector);
    uint16_t root_entry_count = u16_from_le(fat12.bpb_root_entry_count);
    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
    uint32_t root_start       = fat12_first_data_sector(&fat12) - root_dir_sectors;

    uint8_t sector[512];
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        ide_read_sectors(drive, 1, root_start + s, sector);
        for (uint16_t i = 0; i < bytes_per_sector; i += 32) {
            fat12_dir_entry_t *entry = (fat12_dir_entry_t *)&sector[i];
            if (entry->name[0] == 0x00) return 0;
            if (entry->name[0] == 0xE5) continue;
            if (fat12_compare_83_name(entry->name, name11)) {
                uint16_t start_cluster = (uint16_t)entry->first_cluster_low[0] |
                                         ((uint16_t)entry->first_cluster_low[1] << 8);
                entry->name[0] = 0xE5;
                ide_write_sectors(drive, 1, root_start + s, sector);
                if (start_cluster >= 2)
                    fat12_free_cluster_chain(drive, &fat12, start_cluster);
                return 1;
            }
        }
    }
    return 0;
}

const char *fat12_read_file(uint8_t drive, const char *name) {
    fat12_file_info_t info;
    if (!fat12_find_path(drive, name, &info)) return NULL;
    return fat12_read_file_by_info(drive, &info);
}

const char *fat12_read_file_by_info(uint8_t drive, const fat12_file_info_t *info) {
    if (!info) return NULL;

    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    uint32_t bytes_per_sector = u16_from_le(fat12.bpb_bytes_per_sector);
    uint32_t size = info->size;
    char *data = kmalloc(size + 1);
    if (!data) return NULL;

    uint16_t fat_size  = u16_from_le(fat12.bpb_fat_size_16);
    uint32_t fat_bytes = (uint32_t)fat_size * bytes_per_sector;
    static uint8_t fat_buf[FAT12_BUF_SIZE];
    if (fat_bytes > sizeof(fat_buf)) { kfree(data); return NULL; }
    fat12_read_fat(drive, &fat12, fat_buf);

    uint32_t written = 0;
    uint16_t cluster = info->first_cluster;

    while (cluster >= 2 && cluster < 0xFF8 && written < size) {
        uint32_t first_sector        = fat12_first_sector_of_cluster(&fat12, cluster);
        uint16_t sectors_per_cluster = fat12.bpb_sectors_per_cluster;
        for (uint16_t s = 0; s < sectors_per_cluster && written < size; s++) {
            uint8_t sector[512];
            ide_read_sectors(drive, 1, first_sector + s, sector);
            uint32_t to_copy = size - written;
            if (to_copy > bytes_per_sector) to_copy = bytes_per_sector;
            memcpy(data + written, sector, to_copy);
            written += to_copy;
        }
        cluster = fat12_read_fat_entry(fat_buf, cluster);
    }

    data[written < size ? written : size] = '\0';
    return data;
}

void fat12_write_fat12(uint8_t drive, fat12_t *fat12) {
    uint16_t bytes_per_sector  = u16_from_le(fat12->bpb_bytes_per_sector);
    uint16_t reserved_sectors  = u16_from_le(fat12->bpb_reserved_sector_count);
    uint16_t num_fats          = fat12->bpb_num_fats;
    uint16_t root_entry_count  = u16_from_le(fat12->bpb_root_entry_count);
    uint16_t fat_size          = u16_from_le(fat12->bpb_fat_size_16);
    uint16_t sectors_per_cluster = fat12->bpb_sectors_per_cluster;

    ide_write_sectors(drive, 1, 0, fat12);

    static uint8_t buf[512];
    if (bytes_per_sector > sizeof(buf)) return;

    memset(buf, 0, bytes_per_sector);
    for (uint32_t lba = 1; lba < reserved_sectors; lba++)
        ide_write_sectors(drive, 1, lba, buf);

    uint32_t fat_start = reserved_sectors;
    for (uint16_t f = 0; f < num_fats; f++) {
        uint32_t fat_lba = fat_start + (uint32_t)f * fat_size;
        buf[0] = 0xF8; buf[1] = 0xFF; buf[2] = 0xFF;
        ide_write_sectors(drive, 1, fat_lba, buf);
        buf[0] = 0x00; buf[1] = 0x00; buf[2] = 0x00;
        for (uint16_t s = 1; s < fat_size; s++)
            ide_write_sectors(drive, 1, fat_lba + s, buf);
    }

    uint32_t root_dir_sectors = ((uint32_t)root_entry_count * 32 + bytes_per_sector - 1) / bytes_per_sector;
    uint32_t root_start       = reserved_sectors + (uint32_t)num_fats * fat_size;
    for (uint32_t s = 0; s < root_dir_sectors; s++)
        ide_write_sectors(drive, 1, root_start + s, buf);

    uint32_t first_data_sector = root_start + root_dir_sectors;
    for (uint16_t s = 0; s < sectors_per_cluster; s++)
        ide_write_sectors(drive, 1, first_data_sector + s, buf);
}

void fat12_init(uint8_t drive) {
    fat12_t fat12;
    ide_read_sectors(drive, 1, 0, &fat12);

    if (fat12.bs_signature[0] != 0x55 || fat12.bs_signature[1] != 0xAA) {
        serial_print("FAT12: Invalid signature\n");
        return;
    }

    uint16_t bytes_per_sector = u16_from_le(fat12.bpb_bytes_per_sector);
    uint16_t reserved_sectors = u16_from_le(fat12.bpb_reserved_sector_count);
    uint16_t num_fats         = fat12.bpb_num_fats;
    uint16_t root_entry_count = u16_from_le(fat12.bpb_root_entry_count);
    uint16_t total_sectors    = u16_from_le(fat12.bpb_total_sectors_16);
    uint16_t fat_size         = u16_from_le(fat12.bpb_fat_size_16);

    serial_print("FAT12: Bytes per sector: ");    serial_print_num(bytes_per_sector);
    serial_print("\nFAT12: Reserved sectors: ");  serial_print_num(reserved_sectors);
    serial_print("\nFAT12: Number of FATs: ");    serial_print_num(num_fats);
    serial_print("\nFAT12: Root entry count: ");  serial_print_num(root_entry_count);
    serial_print("\nFAT12: Total sectors: ");     serial_print_num(total_sectors);
    serial_print("\nFAT12: FAT size (sectors): "); serial_print_num(fat_size);
    serial_print("\n");
}