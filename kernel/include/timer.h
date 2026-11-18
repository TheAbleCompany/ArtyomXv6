/* timer.h — PIT (IRQ0), тики для планировщика и sys_sleep/sys_uptime. */
#pragma once

#include "types.h"

extern volatile u64 timer_ticks;
#define TIMER_HZ 100

void timer_init(u32 freq_hz);
void yield(void);                 /* вытеснение по таймеру (см. task.c) */
