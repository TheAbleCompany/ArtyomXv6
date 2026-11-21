/* boot.S — точка входа протокола Limine (boot protocol, 64 бита).
 *
 * Отличие от GRUB/multiboot2: Limine входит в ядро УЖЕ в long mode,
 * по высокому виртуальному адресу (e_entry = _start = линковочный VA).
 * К моменту входа:
 *   - ядро замаплено по VMA (сегменты загружены по paddr из ELF);
 *   - HHDM (прямое отображение всей памяти) активен на 0xffff8000...;
 *   - CR3 = таблицы страниц Limine, ответы на запросы (limine.c) заполнены.
 * Самонастройка таблиц страниц из старой версии (PML4[511] -> 1GiB
 * гигабайтная страница) больше не делается ассемблером: прямое отображение
 * PA0..1GiB <-> [KERNBASE, KERNBASE+1GiB) устанавливает limine.c,
 * аккуратно подменяя PML4[511] в таблицах Limine (см. takeover_paging).
 * Остаётся только поставить СВОЙ стек (стек Limine не гарантирован по
 * размеру) и передать управление в C. */

.section .text
.code64
.globl _start
_start:
    cli
    cld
    movabs $boot_stack_top, %rsp
    xor %rbp, %rbp
    call limine_boot_main          /* limine.c: мост Limine -> ядро, не возвращается */
1:  hlt
    jmp 1b

.section .bss
.align 16
.globl boot_stack_top
boot_stack_bottom:
    .space 16384
boot_stack_top:
