/* vfs.h — минимальный VFS из туториала Моллоя: узлы-функции без ioctl/lock,
 * API сохранён (read/write/open/close/readdir/finddir). */
#pragma once

#include "types.h"

#define FS_FILE       0x01
#define FS_DIRECTORY  0x02
#define FS_CHARDEVICE 0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE       0x05
#define FS_SYMLINK    0x08

typedef struct fs_node fs_node_t;

typedef struct dirent {
    u32  ino;
    char name[128];
} dirent_t;

struct fs_node {
    char   name[128];
    u32    flags;
    u64    size;
    u32    inode;
    u32  (*read)   (fs_node_t *node, u64 offset, u32 len, u8 *buf);
    u32  (*write)  (fs_node_t *node, u64 offset, u32 len, const u8 *buf);
    void (*open)   (fs_node_t *node);
    void (*close)  (fs_node_t *node);
    dirent_t *(*readdir) (fs_node_t *node, u32 index);
    fs_node_t *(*finddir)(fs_node_t *node, const char *name);
};

extern fs_node_t *fs_root;       /* корень = монтированный initrd */
extern fs_node_t *console_node;  /* устройство /console (регистрирует syscall.c) */

u32  vfs_read (fs_node_t *node, u64 offset, u32 len, u8 *buf);
u32  vfs_write(fs_node_t *node, u64 offset, u32 len, const u8 *buf);
void vfs_open (fs_node_t *node);
void vfs_close(fs_node_t *node);
