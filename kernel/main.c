/* main.c — kmain: инициализация подсистем, самотесты (куча, кадры, VFS),
 * запуск init из initrd и передача управления планировщику.
 *
 * Port на Limine: разбор multiboot2 заменён на boot info из limine.c
 * (bootinfo.h): карта памяти, модуль initrd (HHDM VA + размер). Магия
 * загрузчика не проверяется — протокол Limine гарантирует целостность
 * ответов. Остальное тело kmain не изменилось. */
#include "types.h"
#include "console.h"
#include "serial.h"
#include "bootinfo.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "paging.h"
#include "kheap.h"
#include "spinlock.h"
#include "vfs.h"
#include "initrd.h"
#include "task.h"

/* ---- самотесты (smoke) ---- */

static void test_heap(void) {
    kprintf("[TEST] heap: alloc/free/merge/align\n");
    u32 holes0 = heap_hole_count();
    void *a = kmalloc(1000);
    void *b = kmalloc(2000);
    KASSERT(a && b);
    kfree(a);                       /* a-дыра не сливается: b занят */
    KASSERT(heap_hole_count() == holes0 + 1);
    kfree(b);                       /* слияние слева и справа -> как было */
    KASSERT(heap_hole_count() == holes0);

    void *c = kmalloc(3000);        /* помещается только в объединённую дыру */
    KASSERT(c);
    kfree(c);

    void *pa = kmalloc_a(4096);
    KASSERT(pa && ((u64)pa & (PAGE_SIZE - 1)) == 0);
    kfree(pa);

    u64 ph = 0;
    void *pp = kmalloc_p(128, &ph);
    KASSERT(pp && kv2ph((u64)pp) == ph);
    kfree(pp);

    kprintf("[TEST] heap: PASS (merge ok, aligned ok, phys ok)\n");
}

static void test_frames(void) {
    kprintf("[TEST] frames: alloc/test/clear\n");
    u64 f1 = alloc_frame();
    u64 f2 = alloc_frame();
    KASSERT(f1 != f2);
    KASSERT(test_frame(f1) && test_frame(f2));
    clear_frame(f2);
    KASSERT(!test_frame(f2));
    KASSERT(alloc_frame() == f2);   /* освободившийся кадр переиспользуется */
    clear_frame(f2);
    kprintf("[TEST] frames: PASS\n");
}

static void test_vfs(void) {
    kprintf("[TEST] initrd: file list + read\n");
    u32 idx = 0;
    for (dirent_t *d; (d = fs_root->readdir(fs_root, idx)) != NULL; idx++)
        kprintf("  /%s (inode %u)\n", d->name, d->ino);

    fs_node_t *f = fs_root->finddir(fs_root, "hello.txt");
    KASSERT(f);
    char buf[64];
    memset(buf, 0, sizeof(buf));
    u32 n = vfs_read(f, 0, sizeof(buf) - 1, (u8 *)buf);
    kprintf("  /hello.txt: \"%s\"\n", buf);
    if (n != 19 || memcmp(buf, "Hello from initrd!\n", 19) != 0)
        panic("initrd selftest: content mismatch");
    kprintf("[TEST] initrd: PASS\n");
}

void kmain(void) {
    /* serial_init() и console_init() уже выполнены в limine_boot_main() */
    kprintf("\n=== molloy-xv6-synth: x86_64, Limine boot protocol ===\n");
    kprintf("(Molloy tutorial layout + xv6 mechanisms)\n\n");

    kprintf("[boot] bootloader: %s, cmdline: %s\n", boot_loader_name, boot_cmdline);
    kprintf("[boot] hhdm: 0x%llx, cr3: 0x%llx\n", boot_hhdm_offset, kernel_cr3_phys);
    kprintf("[boot] memmap ranges=%u, initrd: %u bytes\n",
            boot_memmap_count, (u32)boot_initrd_size);
    if (boot_initrd_size == 0)
        panic("no initrd module (check limine.conf module_path)");
    if (!boot_have_fb)
        kprintf("[boot] WARNING: no framebuffer, console output = COM1 only\n");

    gdt_init();
    idt_init();
    timer_init(TIMER_HZ);

    paging_init();
    heap_init();
    spin_init(&task_lock, "task");
    frames_init(boot_memmap, boot_memmap_count,
                boot_initrd_phys, boot_initrd_phys + boot_initrd_size);
    kprintf("[boot] paging + kheap + frames ready\n\n");

    test_heap();
    test_frames();

    /* initrd: модуль Limine уже в памяти (HHDM); копируем в кучу и монтируем */
    u32 isz = (u32)boot_initrd_size;
    u8 *icopy = kmalloc(isz);
    memcpy(icopy, boot_initrd_va, isz);
    fs_root = initrd_mount((u64)icopy, isz);
    kprintf("\n");
    test_vfs();

    kprintf("\n[boot] spawning init from initrd...\n");
    if (spawn_initrd_program("init.bin") < 0)
        panic("cannot spawn init");

    sti();
    scheduler();                    /* не возвращается */
}
