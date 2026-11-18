/* kheap.h — куча ядра: API туториала Моллоя (kmalloc/kmalloc_a/kmalloc_p/
 * kmalloc_ap/kfree) поверх исправленного first-fit-алгоритма. */
#pragma once

#include "types.h"

/* VA-область кучи: [KERNBASE+32MiB, KERNBASE+64MiB) <-> PA [32MiB, 64MiB).
 * Область покрыта прямым отображением (1GiB-страница boot.S) и целиком
 * резервируется в битовой карте кадров при frames_init(). */
#define KHEAP_VA_BASE     (KERNBASE + 0x02000000ULL)
#define KHEAP_INIT_SIZE   (1ULL << 20)
#define KHEAP_MAX_SIZE    (32ULL << 20)

void heap_init(void);
void *kmalloc(u32 n);
void *kmalloc_a(u32 n);                  /* выравнивание на страницу */
void *kmalloc_p(u32 n, u64 *phys_out);
void *kmalloc_ap(u32 n, u64 *phys_out);
void  kfree(void *p);

u32   heap_hole_count(void);             /* для самотеста */
u64   heap_early_top(void);              /* верх bump-аллокатора до кучи */
