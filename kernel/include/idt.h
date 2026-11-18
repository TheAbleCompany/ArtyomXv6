/* idt.h — IDT + обработка прерываний/исключений (入口 через isr.S). */
#pragma once

#include "types.h"

struct trapframe;                 /* определён в task.h */
typedef struct trapframe trapframe_t;

void idt_init(void);              /* заполнение IDT, ремап PIC, маски */

/* C-точка входа из isr.S: возвращает (возможно, подменённый) trapframe. */
trapframe_t *isr_dispatch(trapframe_t *tf);
