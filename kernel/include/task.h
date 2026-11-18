/* task.h — процессы: кадр прерывания (полный, как в xv6), контекст ядра для
 * swtch, таблица задач с состояниями, fork/exit/wait/sleep/wakeup. */
#pragma once

#include "types.h"
#include "idt.h"

/* ---- полный кадр прерывания (x86_64) ----
 * Раскладка стека в isr.S обязана совпадать байт-в-байт. */
struct trapframe {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 int_no, err;                 /* int_no всегда, err — фиктивный 0 при отсутствии */
    u64 rip, cs, rflags, rsp, ss;
};

/* ---- контекст ядра для swtch.S (только callee-saved + точка входа) ----
 * Протокол xv6: swtch(&task->ctx, new) сохраняет rsp В ПОЛЕ task->ctx
 * (вне стека задачи!) и загружает rsp := new. Для новой задачи task->ctx
 * указывает на фиктивный кадр [r15..rbp=0][rip=forkret] на её kstack;
 * после первого входа поле task->ctx хранит rsp последнего swtch. */
typedef struct context {
    u64 r15, r14, r13, r12, rbx, rbp;
    u64 rip;                         /* заполняется только у новых задач (forkret) */
} context_t;

/* ---- состояния (аналог xv6) ---- */
enum task_state {
    TASK_EMBRYO = 0,                 /* создана, ещё не запускалась */
    TASK_RUNNABLE,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_ZOMBIE,
};

#define NFD          8
#define KSTACK_SIZE  0x4000           /* 16 КиБ стек ядра на задачу */

/* пользовательская модель памяти (кольцо 3, целиком в PML4[0]) */
#define USER_CODE_BASE   0x400000ULL
#define USER_STACK_TOP   0x80000000ULL
#define USER_STACK_PAGES 4
#define USER_TOP         USER_STACK_TOP

typedef struct fs_node fs_node_t;

typedef struct task {
    int           id;
    volatile int  state;
    int           exit_status;
    struct task  *parent;
    void         *chan;             /* канал sleep/wakeup */
    u64           ncli;             /* глубина push_off (дисциплина xv6) */
    int           intena;           /* сохранённый IF при push_off */
    u64           cr3;              /* физический адрес PML4 */
    u8           *kstack;           /* база стека ядра */
    trapframe_t  *tf;               /* кадр прерывания (на стеке ядра) */
    context_t    *ctx;              /* контекст ядра (для swtch) */
    fs_node_t    *fds[NFD];
    u64           fd_off[NFD];
    struct task  *next;
} task_t;

extern task_t *current_task;         /* NULL, пока крутится планировщик */
extern task_t *task_init;            /* процесс init (для репарентинга) */

void         scheduler(void) __attribute__((noreturn));
task_t      *allocproc(void);
void         forkret(void);          /* первый вход в задачу (см. task.c) */
void         trap_return(trapframe_t *tf) __attribute__((noreturn)); /* asm */

int  do_fork(void);
void do_exit(int status) __attribute__((noreturn));
int  do_wait(int *ustatus);
int  do_getpid(void);
void sleep_chan(void *chan);         /* входит с task_lock, выходит с task_lock */
void wakeup_chan(void *chan);

int  spawn_initrd_program(const char *name);   /* pid или -1 */
