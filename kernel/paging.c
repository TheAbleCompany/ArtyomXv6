/* paging.c — 4-уровневая трансляция x86_64: порт paging.c Моллоя.
 *
 * Исправления относительно оригинала (подробности — в отчёте):
 *  P-1  самонастройка PML4[511] не нужна: все таблицы живут в куче ядра
 *       (kernel half) и доступны через p2v(phys) из ЛЮБОГО адресного
 *       пространства — clone работает без жонглирования cr3;
 *  P-2  copy_page_physical через глобальный флаг+cli заменён на прямое
 *       копирование через p2v (без конкуренции на однопроцессорной
 *       системе, без «ковыряния» в таблицах чужого каталога);
 *  P-3  clone_address_space расшаривает kernel half по ссылке (как у
 *       Моллоя), но пользовательскую половину строит заново и копирует
 *       только пользовательские страницы (бит U) — нет смешения флагов;
 *  P-4  free_address_space освобождает кадры и таблицы пользователя,
 *       не трогая общие таблицы ядра (у Моллоя destroy отсутствовал). */
#include "paging.h"
#include "kheap.h"
#include "console.h"
#include "multiboot2.h"

u64 kernel_cr3 = 0;

/* limine.c — PML4 Limine (после takeover_paging: PML4[511] — наше
 * 1GiB прямое отображение, PML4[256..510] — HHDM Limine) */
extern u64 kernel_cr3_phys;

static u8 *frame_bitmap;
static u64 nframes;

static u64 *zero_table(void) {
    u64 *t = kmalloc_a(PAGE_SIZE);
    memset(t, 0, PAGE_SIZE);
    return t;
}

void paging_init(void) {
    /* таблицы Limine уже активны и захвачены; берём их под управление */
    kernel_cr3 = kernel_cr3_phys;
}

u64 *walk_addr(u64 dir_phys, u64 va, int make) {
    u64 user = va < KERNBASE;
    u64 flags = PTE_PRESENT | PTE_WRITE | (user ? PTE_USER : 0);
    u64 *pml4 = (u64 *)p2v(dir_phys);
    u64 *e = &pml4[PML4_INDEX(va)];
    if (!(*e & PTE_PRESENT)) {
        if (!make) return NULL;
        *e = kv2ph((u64)zero_table()) | flags;
    }
    u64 *pdpt = (u64 *)p2v(*e & PTE_ADDR);
    e = &pdpt[PDPT_INDEX(va)];
    if (!(*e & PTE_PRESENT)) {
        if (!make) return NULL;
        *e = kv2ph((u64)zero_table()) | flags;
    }
    u64 *pd = (u64 *)p2v(*e & PTE_ADDR);
    e = &pd[PD_INDEX(va)];
    if (!(*e & PTE_PRESENT)) {
        if (!make) return NULL;
        *e = kv2ph((u64)zero_table()) | flags;
    }
    u64 *pt = (u64 *)p2v(*e & PTE_ADDR);
    return &pt[PT_INDEX(va)];
}

/* ---- физические кадры (API Моллоя: alloc/clear/test) ---- */

void frames_init(const mb2_memmap_entry_t *mem, u32 mem_count,
                 u64 mod_start, u64 mod_end) {
    nframes = (1ULL << 30) / PAGE_SIZE;          /* покрытие: 0..1GiB */
    frame_bitmap = kmalloc(nframes / 8);
    memset(frame_bitmap, 0, nframes / 8);

    /* [0, early_top) — ядро, boot-таблицы, bump-аллокации */
    u64 top = align_up(heap_early_top(), PAGE_SIZE);
    for (u64 p = 0; p < top; p += PAGE_SIZE) {
        frame_bitmap[(p / PAGE_SIZE) >> 3] |= 1 << ((p / PAGE_SIZE) & 7);
    }
    /* вся область кучи резервируется целиком */
    u64 hb = kv2ph(KHEAP_VA_BASE);
    for (u64 p = hb; p < hb + KHEAP_MAX_SIZE; p += PAGE_SIZE) {
        frame_bitmap[(p / PAGE_SIZE) >> 3] |= 1 << ((p / PAGE_SIZE) & 7);
    }
    /* недоступные диапазоны из карты памяти */
    for (u32 i = 0; i < mem_count; i++) {
        if (mem[i].type == MB2_MEM_AVAILABLE) continue;
        for (u64 p = mem[i].base; p < mem[i].base + mem[i].len; p += PAGE_SIZE) {
            if (p >= (1ULL << 30)) break;
            frame_bitmap[(p / PAGE_SIZE) >> 3] |= 1 << ((p / PAGE_SIZE) & 7);
        }
    }
    /* кадры модуля GRUB (initrd) — уже скопирован, пометим занятыми */
    for (u64 p = mod_start; p < mod_end; p += PAGE_SIZE) {
        if (p >= (1ULL << 30)) break;
        frame_bitmap[(p / PAGE_SIZE) >> 3] |= 1 << ((p / PAGE_SIZE) & 7);
    }
}

u64 alloc_frame(void) {
    for (u64 i = 0; i < nframes; i++) {
        if (!(frame_bitmap[i >> 3] & (1u << (i & 7)))) {
            frame_bitmap[i >> 3] |= 1u << (i & 7);
            return i * PAGE_SIZE;
        }
    }
    panic("alloc_frame: out of memory");
}

void clear_frame(u64 phys) {
    u64 i = phys / PAGE_SIZE;
    if (i >= nframes) panic("clear_frame: out of range 0x%llx", phys);
    frame_bitmap[i >> 3] &= ~(1u << (i & 7));
}

int test_frame(u64 phys) {
    u64 i = phys / PAGE_SIZE;
    if (i >= nframes) return 1;
    return (frame_bitmap[i >> 3] & (1u << (i & 7))) != 0;
}

/* ---- адресные пространства ---- */

u64 create_kernel_address_space(void) {
    u64 *p = zero_table();
    u64 *k = (u64 *)p2v(kernel_cr3);
    for (int i = 256; i < 512; i++) p[i] = k[i];   /* kernel half по ссылке */
    return kv2ph((u64)p);
}

u64 clone_address_space(u64 parent_dir) {
    u64 *pp = (u64 *)p2v(parent_dir);
    u64 *cp = zero_table();
    for (int i = 256; i < 512; i++) cp[i] = pp[i];

    /* пользовательская половина (PML4 0..255): полная копия */
    for (int i = 0; i < 256; i++) {
        u64 pe = pp[i];
        if (!(pe & PTE_PRESENT)) continue;
        u64 *pdpt = (u64 *)p2v(pe & PTE_ADDR);
        u64 *cpdpt = zero_table();
        cp[i] = kv2ph((u64)cpdpt) | PTE_PRESENT | PTE_WRITE | PTE_USER;
        for (int j = 0; j < 512; j++) {
            u64 de = pdpt[j];
            if (!(de & PTE_PRESENT)) continue;
            u64 *pd = (u64 *)p2v(de & PTE_ADDR);
            u64 *cpd = zero_table();
            cpdpt[j] = kv2ph((u64)cpd) | PTE_PRESENT | PTE_WRITE | PTE_USER;
            for (int k = 0; k < 512; k++) {
                u64 ke = pd[k];
                if (!(ke & PTE_PRESENT)) continue;
                u64 *pt = (u64 *)p2v(ke & PTE_ADDR);
                u64 *cpt = zero_table();
                cpd[k] = kv2ph((u64)cpt) | PTE_PRESENT | PTE_WRITE | PTE_USER;
                for (int l = 0; l < 512; l++) {
                    u64 pte = pt[l];
                    if (!(pte & PTE_PRESENT)) continue;
                    u64 nf = alloc_frame();
                    /* P-2: копирование напрямую через p2v — кадры < 1GiB */
                    memcpy((void *)p2v(nf), (void *)p2v(pte & PTE_ADDR), PAGE_SIZE);
                    cpt[l] = nf | (pte & PTE_FLAGS);
                }
            }
        }
    }
    return kv2ph((u64)cp);
}

void free_address_space(u64 dir) {
    u64 *pml4 = (u64 *)p2v(dir);
    for (int i = 0; i < 256; i++) {
        u64 pe = pml4[i];
        if (!(pe & PTE_PRESENT)) continue;
        u64 *pdpt = (u64 *)p2v(pe & PTE_ADDR);
        for (int j = 0; j < 512; j++) {
            u64 de = pdpt[j];
            if (!(de & PTE_PRESENT)) continue;
            u64 *pd = (u64 *)p2v(de & PTE_ADDR);
            for (int k = 0; k < 512; k++) {
                u64 ke = pd[k];
                if (!(ke & PTE_PRESENT)) continue;
                u64 *pt = (u64 *)p2v(ke & PTE_ADDR);
                for (int l = 0; l < 512; l++) {
                    u64 pte = pt[l];
                    if (pte & PTE_PRESENT) clear_frame(pte & PTE_ADDR);
                }
                kfree(pt);
            }
            kfree(pd);
        }
        kfree(pdpt);
    }
    kfree(pml4);
}
