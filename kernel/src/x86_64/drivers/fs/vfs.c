#include <stdint.h>
#include <stddef.h>
#include <memory.h>
#include <x86_64/allocator/heap.h>
#include <x86_64/drivers/fs/fat12.h>
#include <x86_64/drivers/fs/vfs.h>
#include <x86_64/serial.h>

#define VFS_MAX_FILESYSTEMS 8
#define VFS_MAX_MOUNTS      8

static const vfs_filesystem_t *s_filesystems[VFS_MAX_FILESYSTEMS];
static uint8_t                 s_filesystem_count;

static vfs_mount_t s_mounts[VFS_MAX_MOUNTS];
static uint8_t     s_mount_count;

static const vfs_mount_t *vfs_find_mount(const char *path, const char **out_relpath) {
    if (!path) return NULL;

    const vfs_mount_t *best     = NULL;
    size_t             best_len = 0;

    for (uint8_t i = 0; i < s_mount_count; i++) {
        const vfs_mount_t *m = &s_mounts[i];
        if (!m->mount_point) continue;
        size_t mlen = strlen(m->mount_point);

        if (mlen == 1 && m->mount_point[0] == '/') {
            if (!best) { best = m; best_len = 1; }
            continue;
        }

        if (mlen > 0 && strncmp(path, m->mount_point, mlen) == 0) {
            if (path[mlen] == '/' || path[mlen] == '\0') {
                if (mlen > best_len) { best = m; best_len = mlen; }
            }
        }
    }

    if (!best) return NULL;
    if (out_relpath) {
        const char *p = path;
        if (best_len > 1) p += best_len;
        while (*p == '/') p++;
        *out_relpath = p;
    }
    return best;
}

static int vfs_path_is_root(const char *path) {
    if (!path) return 1;
    while (*path == '/') path++;
    return *path == '\0';
}

static vfs_node_t *fat12_open(const vfs_mount_t *mount, const char *path) {
    if (!mount) return NULL;

    vfs_file_info_t info;
    if (vfs_path_is_root(path)) {
        memset(&info, 0, sizeof(info));
        info.attr          = 0x10;
        info.first_cluster = 0;
    } else {
        if (!fat12_find_path(mount->drive, path, &info)) return NULL;
    }

    vfs_node_t *node = kmalloc(sizeof(*node));
    if (!node) return NULL;
    node->mount    = mount;
    node->internal = NULL;
    node->info     = info;
    return node;
}

static void fat12_close(vfs_node_t *node) {
    if (node) kfree(node);
}

static void *fat12_read(vfs_node_t *node, uint32_t *out_size) {
    if (!node) { if (out_size) *out_size = 0; return NULL; }
    void *data = (void *)fat12_read_file_by_info(node->mount->drive, &node->info);
    if (out_size) *out_size = node->info.size;
    return data;
}

static uint8_t fat12_write(vfs_node_t *node, const void *data, uint32_t size) {
    if (!node || !data) return 0;
    fat12_delete_file(node->mount->drive, node->info.name);
    return fat12_create_file(node->mount->drive, node->info.name, (const uint8_t *)data, 0x20, size);
}

static uint8_t fat12_is_dir(vfs_node_t *node) {
    if (!node) return 0;
    return (node->info.attr & 0x10) != 0;
}

static uint16_t fat12_list_dir_node(vfs_node_t *node, vfs_file_info_t *out, uint16_t max) {
    if (!node) return 0;
    return fat12_list_dir(node->mount->drive, node->info.first_cluster, out, max);
}

static uint8_t fat12_mkdir(const vfs_mount_t *mount, const char *path) {
    if (!mount || !path) return 0;
    return fat12_create_directory(mount->drive, path);
}

static uint8_t fat12_unlink(const vfs_mount_t *mount, const char *path) {
    if (!mount || !path) return 0;
    return fat12_delete_file(mount->drive, path);
}

__attribute__((used))
static const vfs_fs_ops_t fat12_ops = {
    .open     = fat12_open,
    .close    = fat12_close,
    .read     = fat12_read,
    .write    = fat12_write,
    .is_dir   = fat12_is_dir,
    .list_dir = fat12_list_dir_node,
    .mkdir    = fat12_mkdir,
    .unlink   = fat12_unlink,
};

__attribute__((used))
const vfs_filesystem_t fat12_filesystem = {
    .name = "fat12",
    .ops  = &fat12_ops,
};

void vfs_init(void) {
    s_filesystem_count = 0;
    s_mount_count      = 0;
    vfs_register_filesystem(&fat12_filesystem);
    vfs_mount("/", &fat12_filesystem, 0);
}

int vfs_register_filesystem(const vfs_filesystem_t *fs) {
    if (!fs || !fs->name || !fs->ops) return 0;
    if (s_filesystem_count >= VFS_MAX_FILESYSTEMS) return 0;
    s_filesystems[s_filesystem_count++] = fs;
    return 1;
}

int vfs_mount(const char *mount_point, const vfs_filesystem_t *fs, uint8_t drive) {
    if (!mount_point || !fs) return 0;
    if (s_mount_count >= VFS_MAX_MOUNTS) return 0;
    s_mounts[s_mount_count].mount_point = mount_point;
    s_mounts[s_mount_count].fs          = fs;
    s_mounts[s_mount_count].drive       = drive;
    s_mount_count++;
    return 1;
}

vfs_node_t *vfs_open(const char *path) {
    const char        *relpath = NULL;
    const vfs_mount_t *mount   = vfs_find_mount(path, &relpath);
    if (!mount) return NULL;
    return mount->fs->ops->open(mount, relpath);
}

vfs_node_t *vfs_open_dir(const char *path) {
    vfs_node_t *node = vfs_open(path);
    if (!node) return NULL;
    if (!vfs_is_dir(node)) { vfs_close(node); return NULL; }
    return node;
}

void vfs_close(vfs_node_t *node) {
    if (!node) return;
    if (node->mount && node->mount->fs && node->mount->fs->ops && node->mount->fs->ops->close)
        node->mount->fs->ops->close(node);
    else
        kfree(node);
}

void *vfs_read_file(vfs_node_t *node, uint32_t *out_size) {
    if (!node) { if (out_size) *out_size = 0; return NULL; }
    if (node->mount && node->mount->fs && node->mount->fs->ops && node->mount->fs->ops->read)
        return node->mount->fs->ops->read(node, out_size);
    if (out_size) *out_size = 0;
    return NULL;
}

uint8_t vfs_write_file(const char *path, const void *data, uint32_t size) {
    if (!path || !data) return 0;
    const char        *relpath = NULL;
    const vfs_mount_t *mount   = vfs_find_mount(path, &relpath);
    if (!mount || !relpath || *relpath == '\0') return 0;
    if (!mount->fs->ops->write) return 0;

    fat12_delete_file(mount->drive, relpath);
    return fat12_create_file(mount->drive, relpath, (const uint8_t *)data, 0x20, size);
}

uint8_t vfs_mkdir(const char *path) {
    if (!path) return 0;
    const char        *relpath = NULL;
    const vfs_mount_t *mount   = vfs_find_mount(path, &relpath);
    if (!mount || !relpath) return 0;
    if (!mount->fs->ops->mkdir) return 0;
    return mount->fs->ops->mkdir(mount, relpath);
}

uint8_t vfs_unlink(const char *path) {
    if (!path) return 0;
    const char        *relpath = NULL;
    const vfs_mount_t *mount   = vfs_find_mount(path, &relpath);
    if (!mount || !relpath || *relpath == '\0') return 0;
    if (!mount->fs->ops->unlink) return 0;
    return mount->fs->ops->unlink(mount, relpath);
}

uint8_t vfs_is_dir(vfs_node_t *node) {
    if (!node) return 0;
    if (node->mount && node->mount->fs && node->mount->fs->ops && node->mount->fs->ops->is_dir)
        return node->mount->fs->ops->is_dir(node);
    return (node->info.attr & 0x10) != 0;
}

uint16_t vfs_list_dir_node(vfs_node_t *node, vfs_file_info_t *out, uint16_t max) {
    if (!node) return 0;
    if (node->mount && node->mount->fs && node->mount->fs->ops && node->mount->fs->ops->list_dir)
        return node->mount->fs->ops->list_dir(node, out, max);
    return 0;
}

uint16_t vfs_list_dir_path(const char *path, vfs_file_info_t *out, uint16_t max) {
    vfs_node_t *node = vfs_open_dir(path);
    if (!node) return 0;
    uint16_t count = vfs_list_dir_node(node, out, max);
    vfs_close(node);
    return count;
}