/* task.c — процессы: xv6-механика (ptable, states, sleep/wakeup, swtch,
 * fork/exit/wait) поверх структуры туториала Моллоя (список задач, next_pid,
 * fork/getpid). Замена сломанных мест Моллоя:
 *  T-1  switch_task() из обработчика таймера больше НЕ дёргает esp/eip
 *       вручную: полный trapframe лежит на стеке ядра, переключение ядра
 *       через swtch (как yield() в xv6);
 *  T-2  fork(): ребёнок получает копию полного кадра с rax=0 и копию
 *       адресного пространства — «хак read_eip()==0x12345» устранён;
 *  T-3  exit()/wait() с состоянием ZOMBIE и передачей кода возврата
 *       (у Моллоя завершившаяся задача просто исчезала, детей никто не ждал);
 *  T-4  move_stack/initial_esp устранены: у каждой задачи свой стек из
 *       kmalloc, TSS.rsp0 перезагружается при каждом переключении;
 *  T-5  вытеснение только когда задача не держит блокировок (ncli==0),
 *       как в xv6 — нет вытеснения внутри критических секций. */
#include "task.h"
#include "paging.h"
#include "kheap.h"
#include "spinlock.h"
#include "console.h"
#include "timer.h"
#include "vfs.h"
#include "gdt.h"

task_t *current_task = NULL;
task_t *task_init = NULL;

static task_t *tasks = NULL;      /* односвязный список */
static int next_pid = 1;
/* сохранённый rsp планировщика (аналог cpu->scheduler в xv6): слот, куда
 * swtch пишет rsp при уходе задачи, и значение-кадр при возврате в цикл */
static context_t *sched_ctx;

extern void swtch(context_t **old, context_t *new);   /* swtch.S */
void trap_return(trapframe_t *tf);                   /* isr.S */

/* ---- создание задачи (аналог allocproc в xv6) ---- */

task_t *allocproc(void) {
    spin_lock(&task_lock);
    task_t *t = kmalloc(sizeof(task_t));
    if (!t) { spin_unlock(&task_lock); return NULL; }
    memset(t, 0, sizeof(task_t));

    t->kstack = kmalloc_a(KSTACK_SIZE);
    if (!t->kstack) { kfree(t); spin_unlock(&task_lock); return NULL; }

    t->id = next_pid++;

    /* раскладка нового стека ядра: [tf][pad8][context] — при входе в
     * forkret rsp % 16 == 8, как требует ABI */
    u8 *sp = t->kstack + KSTACK_SIZE;
    sp -= sizeof(trapframe_t);
    t->tf = (trapframe_t *)sp;
    memset(t->tf, 0, sizeof(trapframe_t));
    sp -= 8;
    sp -= sizeof(context_t);
    t->ctx = (context_t *)sp;
    /* фиктивный кадр первого входа: [r15..rbp = 0][rip = forkret].
     * swtch загрузит rsp := t->ctx (значение указателя) и ret уйдёт на
     * forkret; после первого входа этот кадр больше не используется. */
    memset(t->ctx, 0, sizeof(context_t));
    t->ctx->rip = (u64)forkret;

    t->state = TASK_EMBRYO;
    t->next = tasks;
    tasks = t;
    spin_unlock(&task_lock);
    return t;
}

/* первый вход в задачу (аналог forkret в xv6): уходим в кольцо 3 */
void forkret(void) {
    trap_return(current_task->tf);
}

/* ---- sleep/wakeup (xv6, proc.c) ---- */

/* входит и выходит с удержанным task_lock */
void sleep_chan(void *chan) {
    task_t *cur = current_task;
    cur->chan = chan;
    cur->state = TASK_SLEEPING;
    spin_unlock(&task_lock);
    swtch(&cur->ctx, sched_ctx);
    spin_lock(&task_lock);
    cur->chan = NULL;
}

/* внутренняя версия: вызывать только с УЖЕ удержанным task_lock
 * (иначе do_exit -> wakeup_chan -> spin_lock = само-дедлок на одном ядре) */
static void wakeup_locked(void *chan) {
    for (task_t *t = tasks; t; t = t->next)
        if (t->chan == chan && t->state == TASK_SLEEPING)
            t->state = TASK_RUNNABLE;
}

void wakeup_chan(void *chan) {
    spin_lock(&task_lock);
    wakeup_locked(chan);
    spin_unlock(&task_lock);
}

/* ---- вытеснение по таймеру (T-1, T-5) ---- */

void yield(void) {
    task_t *cur = current_task;
    KASSERT(cur && cur->state == TASK_RUNNING);
    spin_lock(&task_lock);
    cur->state = TASK_RUNNABLE;
    spin_unlock(&task_lock);
    swtch(&cur->ctx, sched_ctx);
    /* вернулись: кадр прерывания на месте, epilogue вернёт нас по iretq */
}

/* ---- планировщик (аналог scheduler в xv6, однопроцессорный) ---- */

void scheduler(void) {
    for (;;) {
        push_off();                    /* boot-путь: current_task == NULL */
        task_t *p = NULL;
        spin_lock(&task_lock);
        for (task_t *t = tasks; t; t = t->next)
            if (t->state == TASK_RUNNABLE) { p = t; break; }
        if (p) p->state = TASK_RUNNING;
        spin_unlock(&task_lock);
        if (p) {
            /* ВАЖНО: смена current_task — только между парами push/pop,
             * никогда внутри них. pop_off считает глубину в current_task:
             * смена внутри блокировки дала бы push в boot_ncli, а pop в
             * p->ncli (или наоборот) -> «pop_off: underflow»/лишний sti. */
            current_task = p;
            switch_cr3(p->cr3);
            tss_set_rsp0((u64)p->kstack + KSTACK_SIZE);   /* T-4 */
            swtch(&sched_ctx, p->ctx);
            current_task = NULL;
        }
        pop_off();
        if (!p) hlt();
    }
}

/* ---- fork (T-2) ---- */

int do_fork(void) {
    task_t *cur = current_task;
    KASSERT(cur);

    task_t *ch = allocproc();
    if (!ch) return -1;

    ch->parent = cur;
    ch->cr3 = clone_address_space(cur->cr3);
    if (!ch->cr3) {
        spin_lock(&task_lock);
        task_t **pp = &tasks;
        while (*pp && *pp != ch) pp = &(*pp)->next;
        if (*pp) *pp = ch->next;
        spin_unlock(&task_lock);
        kfree(ch->kstack);
        kfree(ch);
        return -1;
    }

    /* полный кадр + rax = 0: ребёнок «вернёт» 0 из fork() */
    memcpy(ch->tf, cur->tf, sizeof(trapframe_t));
    ch->tf->rax = 0;

    /* файловые дескрипторы наследуются */
    for (int i = 0; i < NFD; i++) {
        ch->fds[i] = cur->fds[i];
        ch->fd_off[i] = cur->fd_off[i];
    }

    spin_lock(&task_lock);
    ch->state = TASK_RUNNABLE;
    spin_unlock(&task_lock);
    return ch->id;
}

/* ---- exit (T-3) ---- */

void do_exit(int status) {
    task_t *cur = current_task;
    KASSERT(cur);

    for (int i = 0; i < NFD; i++) cur->fds[i] = NULL;
    /* адресное пространство освободит wait() при жатве: cr3 ещё активен */

    spin_lock(&task_lock);
    cur->exit_status = status;
    cur->state = TASK_ZOMBIE;

    /* репарентинг сирот к init (xv6) */
    for (task_t *t = tasks; t; t = t->next)
        if (t->parent == cur && t != cur) t->parent = task_init;

    wakeup_locked(cur->parent);    /* родитель спит на канале == себе; lock уже держим */

    /* автоматизация QEMU: init завершился и жить больше некому */
    int alive = 0;
    for (task_t *t = tasks; t; t = t->next)
        if (t != cur && (t->state == TASK_EMBRYO || t->state == TASK_RUNNABLE ||
                         t->state == TASK_RUNNING || t->state == TASK_SLEEPING))
            alive++;
    spin_unlock(&task_lock);

    if (cur == task_init && alive == 0)
        qemu_exit(status == 0 ? 0 : 1);

    push_off();
    swtch(&cur->ctx, sched_ctx);
    panic("zombie task resumed");
}

/* ---- wait (T-3) ---- */

int do_wait(int *ustatus) {
    spin_lock(&task_lock);
    for (;;) {
        int havekids = 0;
        task_t *z = NULL;
        for (task_t *t = tasks; t; t = t->next) {
            if (t->parent == current_task) {
                havekids = 1;
                if (t->state == TASK_ZOMBIE) { z = t; break; }
            }
        }
        if (!z) {
            if (!havekids) { spin_unlock(&task_lock); return -1; }
            sleep_chan(current_task);      /* ребёнок будит wakeup(parent) */
            continue;
        }
        int pid = z->id;
        int st = z->exit_status;
        /* жатва: кадры пользователя, таблицы и стек ядра */
        if (z->cr3) free_address_space(z->cr3);
        kfree(z->kstack);
        task_t **pp = &tasks;
        while (*pp && *pp != z) pp = &(*pp)->next;
        KASSERT(*pp);
        *pp = z->next;
        kfree(z);
        spin_unlock(&task_lock);
        if (ustatus) *ustatus = st;        /* указатель проверен argptr */
        return pid;
    }
}

int do_getpid(void) {
    return current_task ? current_task->id : 0;
}

/* ---- старт init-программы из initrd (замена «exec»): плоский бинарник ---- */

int spawn_initrd_program(const char *name) {
    if (!fs_root || !fs_root->finddir) return -1;
    fs_node_t *f = fs_root->finddir(fs_root, name);
    if (!f || f->flags == FS_DIRECTORY) return -1;

    task_t *t = allocproc();
    if (!t) return -1;

    t->cr3 = create_kernel_address_space();
    if (!t->cr3) {
        /* задача уже в списке tasks (allocproc) — сначала исключаем,
         * иначе kfree оставит висячий узел в планировщике */
        spin_lock(&task_lock);
        task_t **pp = &tasks;
        while (*pp && *pp != t) pp = &(*pp)->next;
        if (*pp) *pp = t->next;
        spin_unlock(&task_lock);
        kfree(t->kstack);
        kfree(t);
        return -1;
    }

    /* код: плоский образ по USER_CODE_BASE, страницы из alloc_frame */
    u32 size = f->size;
    for (u64 va = USER_CODE_BASE; va < USER_CODE_BASE + size; va += PAGE_SIZE) {
        u64 fr = alloc_frame();
        memset((void *)p2v(fr), 0, PAGE_SIZE);
        u64 *pte = walk_addr(t->cr3, va, 1);
        *pte = fr | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }
    for (u64 off = 0; off < size; ) {
        u64 va = USER_CODE_BASE + align_down(off, PAGE_SIZE);
        u64 pageoff = off & (PAGE_SIZE - 1);
        u32 chunk = (u32)(PAGE_SIZE - pageoff);
        if (chunk > size - off) chunk = size - off;
        u64 *pte = walk_addr(t->cr3, va, 0);
        vfs_read(f, off, chunk, (u8 *)(p2v(*pte & PTE_ADDR)) + pageoff);
        off += chunk;
    }

    /* стек пользователя */
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        u64 va = USER_STACK_TOP - (u64)(i + 1) * PAGE_SIZE;
        u64 fr = alloc_frame();
        memset((void *)p2v(fr), 0, PAGE_SIZE);
        u64 *pte = walk_addr(t->cr3, va, 1);
        *pte = fr | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    /* кадр: iretq в кольцо 3 */
    t->tf->rip = USER_CODE_BASE;
    t->tf->cs = USER_CS;
    t->tf->ss = USER_DS;
    t->tf->rflags = 0x202;             /* IF = 1 */
    t->tf->rsp = USER_STACK_TOP;

    /* как init в xv6: stdio. console_node определён в syscall.c */
    extern fs_node_t *console_node;
    for (int i = 0; i < 3 && i < NFD; i++)
        t->fds[i] = console_node;

    spin_lock(&task_lock);
    t->state = TASK_RUNNABLE;
    spin_unlock(&task_lock);

    task_init = t;
    return t->id;
}
