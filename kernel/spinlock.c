/* spinlock.c — порт spinlock.c/spinlock.h из xv6: xchg + push_off/pop_off.
 * Заменяет «глобальные cli/sti» туториала Моллоя: каждая критическая секция
 * точно знает, что прерывания выключены, глубина учитывается в задаче. */
#include "spinlock.h"
#include "task.h"
#include "console.h"

spinlock_t task_lock;

static u64 boot_ncli = 0;
static int boot_intena = 0;

static inline u32 xchg32(volatile u32 *p, u32 v) {
    u32 old;
    __asm__ volatile("xchg %0, %1" : "=r"(old), "+m"(*p) : "0"(v) : "memory");
    return old;
}

void spin_init(spinlock_t *lk, const char *name) {
    lk->locked = 0;
    lk->name = name;
}

void push_off(void) {
    u64 flags;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags));
    cli();
    task_t *t = current_task;
    if (t) {
        if (t->ncli == 0) t->intena = (flags & 0x200) ? 1 : 0;
        t->ncli++;
    } else {
        if (boot_ncli == 0) boot_intena = (flags & 0x200) ? 1 : 0;
        boot_ncli++;
    }
}

void pop_off(void) {
    task_t *t = current_task;
    u64 n = t ? t->ncli : boot_ncli;
    if (n == 0) panic("spinlock.c",41,"pop_off: underflow");
    if (t) {
        if (--t->ncli == 0 && t->intena) sti();
    } else {
        if (--boot_ncli == 0 && boot_intena) sti();
    }
}

void spin_lock(spinlock_t *lk) {
    push_off();
    while (xchg32(&lk->locked, 1) != 0)
        __asm__ volatile("pause");
}

void spin_unlock(spinlock_t *lk) {
    lk->locked = 0;
    barrier();
    pop_off();
}
