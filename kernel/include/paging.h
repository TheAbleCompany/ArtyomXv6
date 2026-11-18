/* paging.h — 4-уровневая трансляция (порт paging.c Моллоя на x86_64).
 * Все таблицы страниц живут в куче ядра (kernel half), поэтому доступны
 * через p2v(phys) из ЛЮБОГО адресного пространства — самонастройка
 * PML4[511] из 32-бит туториала больше не нужна (см. отчёт, исправление P-1). */
#pragma once

#include "types.h"
#include "multiboot2.h"

#define PAGE_SIZE 4096ULL

#define PTE_PRESENT (1ULL << 0)
#define PTE_WRITE   (1ULL << 1)
#define PTE_USER    (1ULL << 2)
#define PTE_PS      (1ULL << 7)
#define PTE_ADDR    0x000FFFFFFFFFF000ULL
#define PTE_FLAGS   0x0FFFULL

#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)   (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)   (((va) >> 12) & 0x1FF)

/* физический адрес PML4 ядра (создан boot.S, подхвачен paging_init) */
extern u64 kernel_cr3;

void paging_init(void);

/* указатель на PTE виртуального адреса в заданном каталоге; make=1 — создать путь */
u64 *walk_addr(u64 dir_phys, u64 va, int make);

/* аллокатор физических кадров (API Моллоя сохранён) */
u64  alloc_frame(void);
void clear_frame(u64 phys);
int  test_frame(u64 phys);
void frames_init(const mb2_memmap_entry_t *mem, u32 mem_count,
                 u64 mod_start, u64 mod_end);   /* после heap_init */

/* адресные пространства процессов (порт clone_directory, без бага copy_page_physical) */
u64  clone_address_space(u64 parent_dir);
void free_address_space(u64 dir);
u64  create_kernel_address_space(void);

static inline void switch_cr3(u64 phys) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(phys) : "memory");
}
