/* gdt.c — GDT long mode (код/данные ядра и пользователя) + 64-бит TSS.
 * Селекторы: 0x08 kcode, 0x10 kdata, 0x18 ucode(|3 -> 0x1B), 0x20 udata(|3 -> 0x23),
 * 0x28 TSS. RSP0 в TSS перезагружается при каждом переключении задач —
 * замена «стекового хака initial_esp» из туториала Моллоя. */
#include "gdt.h"
#include "types.h"

/* Раскладка TSS64 по Intel SDM (обязательна, packed!):
 *   офсет 0  — u32 reserved, офсет 4 — rsp0 (у процессора именно 4, не 2!)
 *   12/20 — rsp1/rsp2, 28 — reserved, 36..83 — ist[7], 84 — reserved,
 *   92 — reserved u16, 94 — iopb u16. Ошибка «u16 reserved0» сдвигала rsp0
 *   на 2 байта -> #SS при первом int 0x80 из ring 3. */
typedef struct {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iopb;
} __attribute__((packed)) tss_t;

static tss_t tss;
static u64 gdt[8];                 /* 0..4 сегменты, 5-6 TSS, 7 резерв */
static struct { u16 lim; u64 base; } __attribute__((packed)) gdt_desc;

void tss_set_rsp0(u64 rsp) {
    tss.rsp0 = rsp;
}

void gdt_init(void) {
    gdt[1] = 0x00AF9A000000FFFFULL;   /* kernel code: L=1, exec/read, DPL0 */
    gdt[2] = 0x00CF92000000FFFFULL;   /* kernel data: read/write, DPL0 */
    gdt[3] = 0x0020FA000000FFFFULL;   /* user code: L=1, exec/read, DPL3 */
    gdt[4] = 0x0020F2000000FFFFULL;   /* user data: read/write, DPL3 */

    u64 base = (u64)&tss;
    u64 lim = sizeof(tss) - 1;
    memset(&tss, 0, sizeof(tss));
    tss.iopb = sizeof(tss);

    gdt[5] = (lim & 0xFFFF)
           | ((base & 0xFFFF) << 16)
           | (((base >> 16) & 0xFF) << 32)
           | (0x89ULL << 40)          /* present, 64-bit available TSS */
           | (((lim >> 16) & 0xF) << 48)
           | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    gdt_desc.lim = sizeof(gdt) - 1;
    gdt_desc.base = (u64)gdt;

    __asm__ volatile("lgdt %0" :: "m"(gdt_desc) : "memory");

    /* перезагружаем сегменты данных */
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        ::: "memory");

    /* дальний возврат для перезагрузки CS */
    __asm__ volatile(
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        ::: "rax", "memory");

    __asm__ volatile("ltr %0" :: "r"((u16)TSS_SEL) : "memory");
}
