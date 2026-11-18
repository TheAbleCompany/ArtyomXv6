/* main.c — kmain: разбор multiboot2, инициализация подсистем, самотесты
 * (куча, кадры, VFS), запуск init из initrd и передача управления
 * планировщику. */
#include "types.h"
#include "console.h"
#include "serial.h"
#include "multiboot2.h"
#include "gdt.h"
#include "idt.h"
#include "timer.h"
#include "paging.h"
#include "kheap.h"
#include "spinlock.h"
#include "vfs.h"
#include "initrd.h"
#include "task.h"

static mb2_memmap_entry_t memmap[32];
static u32 memmap_count;
static u64 mod_start, mod_end;

static void parse_multiboot(u64 mbi_phys) {
    u8 *mbi = (u8 *)p2v(mbi_phys);
    u32 total = *(u32 *)mbi;
    u8 *tag = mbi + 8;
    u8 *end = mbi + total;
    while (tag + sizeof(mb2_tag_t) <= end) {
        mb2_tag_t *t = (mb2_tag_t *)tag;
        if (t->type == MB2_TAG_END) break;
        if (t->type == MB2_TAG_MEMMAP && t->size >= sizeof(mb2_memmap_t)) {
            mb2_memmap_t *mm = (mb2_memmap_t *)tag;
            u8 *e = tag + sizeof(mb2_memmap_t);
            u8 *eend = tag + t->size;
            while (e + sizeof(mb2_memmap_entry_t) <= eend && memmap_count < 32) {
                memmap[memmap_count++] = *(mb2_memmap_entry_t *)e;
                e += mm->entry_size;
            }
        } else if (t->type == MB2_TAG_MODULE && t->size >= sizeof(mb2_module_t)) {
            mb2_module_t *m = (mb2_module_t *)tag;
            mod_start = m->mod_start;
            mod_end = m->mod_end;
        }
        tag += (t->size + 7u) & ~7u;
    }
}

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

void kmain(u64 mbi_phys, u64 magic) {
    serial_init();
    console_init();
    kprintf("\n=== molloy-xv6-synth: x86_64, GRUB2 multiboot2 ===\n");
    kprintf("(Molloy tutorial layout + xv6 mechanisms)\n\n");

    if (magic != MB2_BOOTLOADER_MAGIC)
        panic("bad multiboot2 magic: 0x%llx", magic);
    parse_multiboot(mbi_phys);
    kprintf("[boot] memmap ranges=%u, module: 0x%x..0x%x\n",
            memmap_count, (u32)mod_start, (u32)mod_end);

    gdt_init();
    idt_init();
    timer_init(TIMER_HZ);

    paging_init();
    heap_init();
    spin_init(&task_lock, "task");
    frames_init(memmap, memmap_count, mod_start, mod_end);
    kprintf("[boot] paging + kheap + frames ready\n\n");

    test_heap();
    test_frames();

    /* initrd: копируем модуль GRUB в кучу и монтируем */
    u32 isz = (u32)(mod_end - mod_start);
    if (isz == 0) panic("no initrd module");
    u8 *icopy = kmalloc(isz);
    memcpy(icopy, (const void *)p2v(mod_start), isz);
    fs_root = initrd_mount((u64)icopy, isz);
    kprintf("\n");
    test_vfs();

    kprintf("\n[boot] spawning init from initrd...\n");
    if (spawn_initrd_program("init.bin") < 0)
        panic("cannot spawn init");

    sti();
    scheduler();                    /* не возвращается */
}
