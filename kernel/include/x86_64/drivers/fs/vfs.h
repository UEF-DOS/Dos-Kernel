#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <x86_64/drivers/fs/fat12.h>

typedef fat12_file_info_t vfs_file_info_t;

typedef struct vfs_node    vfs_node_t;
typedef struct vfs_mount   vfs_mount_t;

typedef struct {
    vfs_node_t *(*open)      (const vfs_mount_t *mount, const char *path);
    void        (*close)     (vfs_node_t *node);
    void       *(*read)      (vfs_node_t *node, uint32_t *out_size);
    uint8_t     (*write)     (vfs_node_t *node, const void *data, uint32_t size);
    uint8_t     (*is_dir)    (vfs_node_t *node);
    uint16_t    (*list_dir)  (vfs_node_t *node, vfs_file_info_t *out, uint16_t max);
    uint8_t     (*mkdir)     (const vfs_mount_t *mount, const char *path);
    uint8_t     (*unlink)    (const vfs_mount_t *mount, const char *path);
} vfs_fs_ops_t;

typedef struct {
    const char        *name;
    const vfs_fs_ops_t *ops;
} vfs_filesystem_t;

struct vfs_mount {
    const char             *mount_point;
    const vfs_filesystem_t *fs;
    uint8_t                 drive;
};

struct vfs_node {
    const vfs_mount_t *mount;
    void              *internal;
    vfs_file_info_t    info;
};

void     vfs_init(void);
int      vfs_register_filesystem(const vfs_filesystem_t *fs);
int      vfs_mount(const char *mount_point, const vfs_filesystem_t *fs, uint8_t drive);
vfs_node_t *vfs_open(const char *path);
vfs_node_t *vfs_open_dir(const char *path);
void        vfs_close(vfs_node_t *node);
void    *vfs_read_file(vfs_node_t *node, uint32_t *out_size);
uint8_t  vfs_write_file(const char *path, const void *data, uint32_t size);
uint8_t  vfs_mkdir(const char *path);
uint8_t  vfs_unlink(const char *path);
uint8_t  vfs_is_dir(vfs_node_t *node);
uint16_t vfs_list_dir_node(vfs_node_t *node, vfs_file_info_t *out, uint16_t max);
uint16_t vfs_list_dir_path(const char *path, vfs_file_info_t *out, uint16_t max);

extern const vfs_filesystem_t fat12_filesystem;

#endif