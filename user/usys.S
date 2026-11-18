/* ulib.S — точка входа в кольцо 3 и заглушки системных вызовов.
 * ABI: номер в rax, аргументы rdi, rsi, rdx, r10; результат в rax.
 * _start настраивает сегменты данных пользователя (DS/ES/FS/GS = 0x23):
 * ядро не трогает сегменты при входе/выходе, поэтому достаточно один раз. */

.set SYS_exit,   1
.set SYS_fork,   2
.set SYS_wait,   3
.set SYS_getpid, 4
.set SYS_read,   5
.set SYS_write,  6
.set SYS_open,   7
.set SYS_close,  8
.set SYS_sleep,  9
.set SYS_uptime, 10

.section .text.start
.globl _start
_start:
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    call main
    mov %eax, %edi
    call exit
    hlt

.section .text

.macro SYSCALL nr, name
.globl \name
\name:
    mov $\nr, %rax
    int $0x80
    ret
.endm

SYSCALL SYS_fork,   fork
SYSCALL SYS_wait,   wait
SYSCALL SYS_getpid, getpid
SYSCALL SYS_read,   read
SYSCALL SYS_write,  write
SYSCALL SYS_open,   open
SYSCALL SYS_close,  close
SYSCALL SYS_sleep,  sleep
SYSCALL SYS_uptime, uptime

.globl exit
exit:
    mov $SYS_exit, %rax
    int $0x80
    hlt
