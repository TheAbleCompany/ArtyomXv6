/* boot.S — вход multiboot2 (32 бита) -> long mode -> kmain(mbi_phys, magic).
 *
 * Карта памяти при загрузке:
 *   PML4[511] -> boot_pdpt[2]  = гигабайтная страница: PA 0..1GiB <-> VA -2GiB..-2GiB+1GiB
 *   PML4[0]   -> boot_pdpt0[0] = временная identity-карта для перехода,
 *                                сбрасывается сразу после входа в 64 бита.
 * Именно прямое отображение физики по KERNBASE заменяет самонастройку
 * PML4[511] из 32-бит туториала Моллоя (аналог KERNBASE в xv6). */

.set KERNBASE, 0xFFFFFFFF80000000

.section .entry_stub, "ax"
.code32
.globl entry_stub
entry_stub:                       /* e_entry MB2 (см. linker.ld): прыжок в _start */
    ljmpl $0x10, $(_start - KERNBASE)

.section .multiboot, "a"
.align 8
mb_header:
    .long 0xE85250D6                                  /* magic multiboot2 */
    .long 0                                           /* архитектура i386 */
    .long mb_header_end - mb_header
    .long -(0xE85250D6 + 0 + (mb_header_end - mb_header))
    .short 0
    .short 0
    .long 8                                           /* end tag */
mb_header_end:

.section .bss
.align 4096
.globl boot_pml4
boot_pml4:  .space 4096
boot_pdpt:  .space 4096
boot_pdpt0: .space 4096
boot_stack_bottom:
    .space 16384
boot_stack_top:

.section .rodata
.align 8
boot_gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF                          /* kernel code, L=1 */
    .quad 0x00CF92000000FFFF                          /* kernel data */
boot_gdt_desc:
    .word 23
    .long boot_gdt - KERNBASE
    .word 0

.section .text
.code32
.globl _start
_start:
    cli
    cld
    mov %eax, %ebp                /* GRUB: eax = magic, ebx = MBI (физ.) */
    mov $boot_stack_top - KERNBASE, %esp

    /* таблицы страниц обнуляем сами — на «GRUB занулил .bss» не полагаемся */
    mov $boot_pml4 - KERNBASE, %edi
    mov $3072, %ecx               /* 3 таблицы x 1024 двойных слова */
    xor %eax, %eax
    rep stosl

    /* PML4[511] -> boot_pdpt */
    mov $boot_pml4 - KERNBASE, %edi
    mov $boot_pdpt - KERNBASE, %eax
    or  $0x03, %eax               /* PRESENT | WRITE */
    mov %eax, 511*8(%edi)
    /* PML4[0] -> boot_pdpt0 (временная identity) */
    mov $boot_pdpt0 - KERNBASE, %eax
    or  $0x03, %eax
    mov %eax, 0(%edi)
    /* boot_pdpt[2] = 1GiB-страница на PA 0 (PRESENT|WRITE|PS) */
    mov $boot_pdpt - KERNBASE, %edi
    movl $0x83, 510*8(%edi)
    /* boot_pdpt0[0] = 1GiB identity */
    mov $boot_pdpt0 - KERNBASE, %edi
    movl $0x83, 0(%edi)

    mov %ebx, %edi                /* mbi — физический адрес (спасли ebx) */

    mov $boot_pml4 - KERNBASE, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or  $0x20, %eax               /* CR4.PAE */
    mov %eax, %cr4

    mov $0xC0000080, %ecx         /* MSR EFER */
    rdmsr
    or  $0x100, %eax              /* EFER.LME */
    wrmsr

    mov %cr0, %eax
    or  $0x80000001, %eax         /* CR0.PG | CR0.PE */
    mov %eax, %cr0

    lgdt (boot_gdt_desc - KERNBASE)
    ljmpl $0x08, $(long_mode_entry - KERNBASE)

.code64
/* Вход с НИЗКИМ (физическим) RIP: здесь можно исполнять только инструкции
 * без RIP-относительных обращений к данным. Перескакиваем на высокий VA
 * через push/ret — классический трюк перехода в higher half. */
long_mode_entry:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    movabs $boot_stack_top, %rsp
    movabs $high_entry, %rax
    pushq %rax
    retq                          /* RIP = высокий VA (PML4[511]) */

high_entry:
    /* сбрасываем временную identity-карту: PML4[0] = 0 */
    movq $0, boot_pml4(%rip)
    mov $boot_pml4 - KERNBASE, %eax
    mov %rax, %cr3                /* сброс TLB; PML4[0] свободен под user space */

    movl %edi, %edi               /* kmain(mbi_phys, magic): rdi = mbi (zero-ext) */
    movl %ebp, %esi               /* esi = magic (ebp ещё хранит его) */
    xor %rbp, %rbp
    call kmain
    cli
1:  hlt
    jmp 1b
