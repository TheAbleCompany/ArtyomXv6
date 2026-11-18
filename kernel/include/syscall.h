/* syscall.h — системные вызовы: int 0x80, таблица + argint/argptr/argstr
 * из xv6 (syscall.c) с проверкой указателей пользователя. */
#pragma once

#include "types.h"
#include "task.h"

#define SYS_exit   1
#define SYS_fork   2
#define SYS_wait   3
#define SYS_getpid 4
#define SYS_read   5
#define SYS_write  6
#define SYS_open   7
#define SYS_close  8
#define SYS_sleep  9
#define SYS_uptime 10

/* ABI: номер в rax, аргументы rdi, rsi, rdx, r10; результат в rax.
 * int 0x80 (в отличие от syscall) не портит rcx/r11, поэтому r10 выбран
 * вместо rcx только для единообразия на будущее. */
trapframe_t *syscall_dispatch(trapframe_t *tf);

int  argint(int n, int *ip);
int  argptr(int n, char **pp, int size);
int  argstr(int n, char **pp);
