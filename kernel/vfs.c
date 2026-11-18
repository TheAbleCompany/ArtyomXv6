/* vfs.c — обёртки VFS (порт из туториала Моллоя): проверка указателей
 * перед вызовом драйвера файловой системы. */
#include "vfs.h"

fs_node_t *fs_root = NULL;

u32 vfs_read(fs_node_t *node, u64 offset, u32 len, u8 *buf) {
    if (!node || !node->read) return 0;
    return node->read(node, offset, len, buf);
}

u32 vfs_write(fs_node_t *node, u64 offset, u32 len, const u8 *buf) {
    if (!node || !node->write) return 0;
    return node->write(node, offset, len, buf);
}

void vfs_open(fs_node_t *node) {
    if (node && node->open) node->open(node);
}

void vfs_close(fs_node_t *node) {
    if (node && node->close) node->close(node);
}
