/* limine.c — интеграция с протоколом Limine (как в xOS: boot-слой, ядро
 * не меняет своей модели памяти).
 *
 * Обязанности:
 *   1. Запросы Limine (base revision 3) в секции .limine_requests
 *      — загрузчик заполняет их ответы ещё ДО входа в _start.
 *   2. Мост: ответы Limine -> mb2-совместимый boot info (карта памяти,
 *      модуль initrd, фреймбуфер) — см. bootinfo.h.
 *   3. takeover_paging(): подмена PML4[511] в таблицах Limine на гигабайтную
 *      страницу PA0..1GiB <-> [KERNBASE, KERNBASE+1GiB). Это даёт ядру его
 *      родную модель p2v()/kv2ph() из boot-версии, сохраняя HHDM Limine
 *      (PML4[256..510]) для доступа к фреймбуферу и структурам загрузчика.
 *   4. console_init() на фреймбуфере (flanterm) и вызов kmain(). */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <limine.h>               /* официальный заголовок (limine-binary/) */
#include "types.h"
#include "serial.h"
#include "multiboot2.h"
#include "bootinfo.h"
#include "console.h"

/* ---- запросы Limine (base revision 3) ---- */

__attribute__((used, section(".limine_requests_start"), aligned(8)))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile LIMINE_BASE_REVISION(3);

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_module_request module_request = {
    .id = LIMINE_MODULE_REQUEST, .revision = 1
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_bootloader_info_request bl_info_request = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_executable_cmdline_request cmdline_request = {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests"), aligned(8)))
static volatile struct limine_kernel_address_request kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0
};

__attribute__((used, section(".limine_requests_end"), aligned(8)))
static volatile LIMINE_REQUESTS_END_MARKER;

/* ---- boot info (см. bootinfo.h) ---- */

mb2_memmap_entry_t boot_memmap[BOOTINFO_MEMMAP_MAX];
u32 boot_memmap_count;
u8 *boot_initrd_va;
u64 boot_initrd_size;
u64 boot_initrd_phys;
fb_info_t boot_fb;
int boot_have_fb;
u64 boot_hhdm_offset;
u64 kernel_cr3_phys;
u64 kern_slide = 0;                   /* слайд физической загрузки ядра (Limine KASLR) */
const char *boot_loader_name = "?";
const char *boot_cmdline = "";

/* ---- ранний вывод только в COM1 (консоль ещё не инициализирована) ---- */

static void early_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

static void early_puts_hex(u64 v) {
    static const char d[] = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4)
        serial_putc(d[(v >> i) & 0xF]);
}

/* ---- мост: ответы Limine -> boot info ---- */

static void bridge_memmap(void) {
    struct limine_memmap_response *r =
        (struct limine_memmap_response *)memmap_request.response;
    if (r == NULL) return;
    for (u64 i = 0; i < r->entry_count && boot_memmap_count < BOOTINFO_MEMMAP_MAX; i++) {
        struct limine_memmap_entry *e = r->entries[i];
        mb2_memmap_entry_t *d = &boot_memmap[boot_memmap_count++];
        d->base = e->base;
        d->len  = e->length;
        switch (e->type) {
        case LIMINE_MEMMAP_USABLE:           d->type = MB2_MEM_AVAILABLE; break;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: d->type = 3; break;
        case LIMINE_MEMMAP_ACPI_NVS:         d->type = 4; break;
        case LIMINE_MEMMAP_BAD_MEMORY:       d->type = 5; break;
        default:
            /* RESERVED, BOOTLOADER_RECLAIMABLE, EXECUTABLE_AND_MODULES,
             * FRAMEBUFFER — всё «занято», кадры не выдавать */
            d->type = MB2_MEM_RESERVED; break;
        }
    }
}

static void bridge_modules(void) {
    struct limine_module_response *r =
        (struct limine_module_response *)module_request.response;
    if (r == NULL || r->module_count == 0) return;   /* ядро само отработает «нет initrd» */
    struct limine_file *f = r->modules[0];
    boot_initrd_va   = (u8 *)(uintptr_t)f->address;  /* HHDM VA */
    boot_initrd_size = f->size;
    boot_initrd_phys = (uintptr_t)f->address - boot_hhdm_offset;
}

static void bridge_framebuffer(void) {
    struct limine_framebuffer_response *r =
        (struct limine_framebuffer_response *)framebuffer_request.response;
    if (r == NULL || r->framebuffer_count == 0) return;
    struct limine_framebuffer *fb = r->framebuffers[0];
    if (fb == NULL || fb->address == NULL) return;
    boot_fb.addr        = (u64)(uintptr_t)fb->address;  /* HHDM VA */
    boot_fb.width       = fb->width;
    boot_fb.height      = fb->height;
    boot_fb.pitch       = fb->pitch;
    boot_fb.bpp         = fb->bpp;
    boot_fb.red_size    = fb->red_mask_size;
    boot_fb.red_shift   = fb->red_mask_shift;
    boot_fb.green_size  = fb->green_mask_size;
    boot_fb.green_shift = fb->green_mask_shift;
    boot_fb.blue_size   = fb->blue_mask_size;
    boot_fb.blue_shift  = fb->blue_mask_shift;
    boot_have_fb = 1;
}

static void bridge_misc(void) {
    struct limine_bootloader_info_response *bli =
        (struct limine_bootloader_info_response *)bl_info_request.response;
    if (bli != NULL && bli->name != NULL)
        boot_loader_name = bli->name;

    struct limine_executable_cmdline_response *cmd =
        (struct limine_executable_cmdline_response *)cmdline_request.response;
    if (cmd != NULL && cmd->cmdline != NULL)
        boot_cmdline = cmd->cmdline;
}

/* ---- захват таблиц страниц (замена самонастройки boot.S) ----
 *
 * Limine грузит ядро со ФИЗИЧЕСКИМ KASLR (kernel address response:
 * physical_base = P != ELF paddr 0x100000), оставляя виртуальные адреса
 * линковочными. Модель памяти ядра (p2v(p) = KERNBASE + p, куча на
 * KERNBASE+32MiB, покрытие кадров 0..1GiB) требует, чтобы образ лежал
 * ровно на PA = VA - KERNBASE. Поэтому:
 *   1. строим прямой обмен PA<->VA на 2MiB-страницах в PD_510
 *      (PML4[511] -> PDPT[510] -> 512 x 2MiB; pdpe1gb не требуется);
 *   2. пока активны таблицы Limine, копируем образ (весь memsz: код +
 *      данные + обнулённый .bss, включая сами таблицы) с physical_base
 *      на PA 0x100000 через HHDM;
 *   3. вписываем PML4[511] Limine -> PDPT КОПИИ (PA = VA - KERNBASE);
 *   4. стаб: перезагрузка CR3 (TLB flush), чистый стек, прыжок в
 *      after_cr3_switch() — кадры стека старого образа после
 *      переключения недоступны, поэтому стек начинается с нуля.
 * После этого код исполняется с VA KERNBASE+0x100000 = PA 0x100000 —
 * ровно та же модель, что была после boot.S под GRUB. */
static u64 boot_pdpt[512] __attribute__((aligned(4096)));
static u64 boot_pds[512] __attribute__((aligned(4096)));      /* PD_510: 2MiB */
extern u8 boot_stack_top[];                                    /* boot.S */
extern u8 kern_end[];                                          /* linker.ld */

void after_cr3_switch(void);

static void relocate_kernel(u64 phys_base) {
    u64 vstart = KERNBASE + 0x100000;          /* линковочный VA начала образа */
    u64 expected_phys = vstart - KERNBASE;     /* 0x100000 */
    if (phys_base == expected_phys) return;    /* слайда нет — нечего делать */

    u64 size = (u64)kern_end - vstart;         /* весь memsz образа */
    if (boot_initrd_va != NULL && boot_initrd_size > 0) {
        u64 mstart = boot_initrd_phys, mend = mstart + boot_initrd_size;
        if (mstart < expected_phys + size && mend > expected_phys)
            panic("initrd overlaps kernel relocation target");
    }
    const u8 *src = (const u8 *)(boot_hhdm_offset + phys_base);
    u8 *dst = (u8 *)(boot_hhdm_offset + expected_phys);
    /* Limine уже обнулил .bss в своём образе — копируем memsz как есть */
    for (u64 off = 0; off < size; off += 4096) {
        u64 n = size - off > 4096 ? 4096 : size - off;
        for (u64 i = 0; i < n; i++)
            dst[off + i] = src[off + i];
    }
    early_puts("[takeover] kernel image relocated: ");
    early_puts_hex(phys_base);
    early_puts(" -> ");
    early_puts_hex(expected_phys);
    early_puts("\n");
}

static void takeover_paging(u64 kernel_phys_base) {
    u64 cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    cr3 &= ~0xFFFULL;                 /* сбросить биты PCID */

    /* глобальные значения — ДО relocate_kernel: копия образа должна
     * унести их с собой */
    kernel_cr3_phys = cr3;            /* PML4 Limine (с нашей правкой [511]) */
    kern_slide = 0;                   /* после релокации слайда нет */

    /* таблицы строятся под КОПИЮ образа (PA = VA - KERNBASE, слайд = 0):
     * PD_510[i] = 2MiB-страница на PA i*2MiB, покрытие 0..1GiB */
    for (int i = 0; i < 512; i++)
        boot_pds[i] = ((u64)i << 21) | 0x83;      /* PRESENT | WRITE | PS */
    boot_pdpt[510] = kv2ph((u64)boot_pds) | 0x03; /* PRESENT | WRITE */

    /* копия образа (с таблицами) на предпочтительный физический адрес */
    relocate_kernel(kernel_phys_base);

    /* PML4[511] Limine -> PDPT копии (HHDM-записи PML4[256..510] целы) */
    volatile u64 *limine_pml4 = (volatile u64 *)(boot_hhdm_offset + cr3);
    limine_pml4[511] = kv2ph((u64)boot_pdpt) | 0x03;

    early_puts("[takeover] direct map ready, cr3=");
    early_puts_hex(cr3);
    early_puts("\n");

    /* стаб: TLB flush + чистый стек + прыжок в копию образа */
    __asm__ volatile(
        "movq %0, %%cr3\n"
        "movabsq $boot_stack_top, %%rsp\n"
        "movabsq $after_cr3_switch, %%rax\n"
        "jmpq *%%rax"
        :: "r"(cr3) : "rax", "memory");
    __builtin_unreachable();
}

/* продолжение limine_boot_main ПОСЛЕ переключения таблиц (уже в копии) */
void after_cr3_switch(void) {
    early_puts("[limine-boot] paging takeover done, memmap=");
    early_puts_hex(boot_memmap_count);
    early_puts(" initrd=");
    early_puts_hex(boot_initrd_size);
    early_puts("\n");

    /* консоль: flanterm на фреймбуфере Limine (+ COM1). Нет фреймбуфера —
     * работаем чисто по COM1, не падая (graceful degradation). */
    console_init();
    if (!boot_have_fb)
        early_puts("[limine-boot] WARNING: no framebuffer, console = COM1 only\n");

    kmain();                          /* main.c, не возвращается */
}

/* ---- точка входа из boot.S ---- */

void limine_boot_main(void) {
    serial_init();
    early_puts("\n[limine-boot] kernel entry, ");

    struct limine_hhdm_response *hh =
        (struct limine_hhdm_response *)hhdm_request.response;
    boot_hhdm_offset = hh ? hh->offset : 0;
    early_puts("hhdm=");
    early_puts_hex(boot_hhdm_offset);

    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        early_puts(", base rev NOT supported (bootloader old); continuing\n");
    } else {
        early_puts(", base rev ok\n");
    }

    bridge_misc();

    /* фактический физический адрес образа (у Limine — физический KASLR) */
    u64 kernel_phys_base = 0;
    struct limine_kernel_address_response *ka =
        (struct limine_kernel_address_response *)kernel_address_request.response;
    if (ka != NULL)
        kernel_phys_base = ka->physical_base;

    bridge_memmap();
    bridge_modules();
    bridge_framebuffer();

    takeover_paging(kernel_phys_base);   /* не возвращается: стаб уходит
                                            в after_cr3_switch() */
}