/* bootinfo.h — мост limine.c -> ядро: boot info, собранный из ответов Limine
 * (замена разбора multiboot2 из старой версии). */
#pragma once

#include "types.h"
#include "multiboot2.h"

#define BOOTINFO_MEMMAP_MAX 64

/* карта памяти Limine, конвертированная в mb2-совместимые записи
 * (MB2_MEM_AVAILABLE и т.д.) — потребляется frames_init() */
extern mb2_memmap_entry_t boot_memmap[BOOTINFO_MEMMAP_MAX];
extern u32 boot_memmap_count;

/* модуль initrd (limine.conf: module_path). address у Limine — HHDM VA */
extern u8 *boot_initrd_va;
extern u64 boot_initrd_size;
extern u64 boot_initrd_phys;      /* = VA - hhdm_offset, для frames_init */

/* параметры линейного фреймбуфера для console_init() (flanterm) */
typedef struct {
    u64 addr;                     /* HHDM VA фреймбуфера */
    u64 width, height, pitch;
    u32 bpp;
    u8  red_size, red_shift;
    u8  green_size, green_shift;
    u8  blue_size, blue_shift;
} fb_info_t;
extern fb_info_t boot_fb;
extern int boot_have_fb;

/* misc */
extern u64 boot_hhdm_offset;      /* HHDM Limine (0, если запрос не поддержан) */
extern u64 kernel_cr3_phys;       /* PML4 Limine (с нашей правкой PML4[511]) */
extern u64 kern_slide;            /* слайд физической загрузки: PA - ELF paddr */
extern const char *boot_loader_name;  /* "Limine x.y" */
extern const char *boot_cmdline;

/* точка входа из boot.S: ранний serial-лог, мост, захват таблиц страниц,
 * console_init(), вызов kmain(). Не возвращается. */
void limine_boot_main(void);

/* main.c */
void kmain(void);
