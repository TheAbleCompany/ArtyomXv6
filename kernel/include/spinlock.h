/* spinlock.h — блокировки xv6 (spinlock.c/spinlock.h): xchg + дисциплина
 * push_off/pop_off вместо «глобальных cli/sti» туториала Моллоя. */
#pragma once

#include "types.h"

typedef struct {
    volatile u32 locked;
    const char  *name;
} spinlock_t;

void push_off(void);              /* cli + учёт глубины (в текущей задаче) */
void pop_off(void);
void spin_init(spinlock_t *lk, const char *name);
void spin_lock(spinlock_t *lk);
void spin_unlock(spinlock_t *lk);

/* единая блокировка таблицы процессов (аналог ptable.lock в xv6) */
extern spinlock_t task_lock;
