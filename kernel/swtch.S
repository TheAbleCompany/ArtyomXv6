/* swtch.S — переключение контекста ядра (порт из xv6, swtch.S).
 * Протокол xv6: старый аргумент — УКАЗАТЕЛЬ НА СЛОТ (context_t **old):
 * сохранённый rsp пишется в *old (поле task->ctx / слот планировщика —
 * вне стека!), новый аргумент — ЗНАЧЕНИЕ context_t *new = адрес кадра
 * (для новой задачи — фиктивный кадр [r15..rbp][rip=forkret] на её
 * kstack, для продолжающейся — rsp, сохранённый при её последнем
 * swtch). Кадр: [r15][r14][r13][r12][rbx][rbp][ret-addr] — 7 qword.
 * Сохраняются только callee-saved регистры: полное состояние пользователя
 * лежит в trapframe на стеке ядра. */

.text
.globl swtch
/* void swtch(context_t **old, context_t *new);  rdi = &old_slot, rsi = new */
swtch:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, (%rdi)             /* *old = rsp: слот в task-структуре */
    movq %rsi, %rsp               /* rsp := new (значение = адрес кадра) */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret
