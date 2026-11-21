/* multiboot2.h — типы boot info. После порта на Limine разбор тегов
 * multiboot2 не выполняется (см. limine.c / bootinfo.h), но mb2-совместимые
 * записи карты памяти остаются форматом boot info для frames_init(). */
#pragma once

#include "types.h"

/* типы записей карты памяти (нумерация multiboot2, как раньше) */
#define MB2_MEM_AVAILABLE 1
#define MB2_MEM_RESERVED  2

typedef struct {
    u32 type;
    u32 size;
} mb2_tag_t;

typedef struct {
    u64 base;
    u64 len;
    u32 type;
    u32 reserved;
} mb2_memmap_entry_t;
