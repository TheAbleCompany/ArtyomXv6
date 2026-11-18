/* multiboot2.h — разбор boot info от GRUB (теги: карта памяти, модуль). */
#pragma once

#include "types.h"

#define MB2_BOOTLOADER_MAGIC 0x36D76289ULL

#define MB2_TAG_END     0
#define MB2_TAG_MODULE  3
#define MB2_TAG_MEMMAP  6
#define MB2_MEM_AVAILABLE 1

typedef struct {
    u32 type;
    u32 size;
} mb2_tag_t;

typedef struct {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
} mb2_memmap_t;

typedef struct {
    u64 base;
    u64 len;
    u32 type;
    u32 reserved;
} mb2_memmap_entry_t;

typedef struct {
    u32 type;
    u32 size;
    u32 mod_start;   /* физический адрес начала модуля */
    u32 mod_end;     /* физический адрес конца модуля   */
    /* далее — строка cmdline, нуль-терминированная */
} mb2_module_t;
