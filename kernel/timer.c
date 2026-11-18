/* timer.c — PIT (канал 0) и обработчик IRQ0: тики, будим спящих на канале
 * &timer_ticks, вытеснение через yield(). В отличие от туториала Моллоя,
 * switch вызывается НЕ «в обход кадра прерывания», а через swtch: кадр
 * остаётся на стеке ядра задачи. */
#include "timer.h"
#include "task.h"
#include "spinlock.h"
#include "types.h"

volatile u64 timer_ticks = 0;

void timer_init(u32 freq_hz) {
    u32 div = 1193182 / freq_hz;
    outb(0x43, 0x36);             /* канал 0, режим 3 */
    outb(0x40, (u8)(div & 0xFF));
    outb(0x40, (u8)((div >> 8) & 0xFF));
}

void timer_irq(trapframe_t *tf) {
    (void)tf;
    timer_ticks++;
    wakeup_chan((void *)&timer_ticks);
    /* вытесняем только «чистую» задачу: без удержания блокировок (ncli == 0),
     * как yield() в xv6 — иначе риск нарушения дисциплины блокировок */
    if (current_task && current_task->state == TASK_RUNNING && current_task->ncli == 0)
        yield();
}
