/* initrd.h — формат RAM-диска (см. scripts/mkinitrd.py). */
#pragma once

#include "types.h"
#include "vfs.h"

#define INITRD_MAGIC 0xBF00BF00U

typedef struct {
    u32 magic;
    u32 nfiles;
} initrd_header_t;

typedef struct {
    char name[64];
    u32  offset;    /* от начала образа */
    u32  length;
} initrd_file_header_t;

/* монтировать образ (base_va — VA копии в куче ядра, size — размер) */
fs_node_t *initrd_mount(u64 base_va, u32 size);
