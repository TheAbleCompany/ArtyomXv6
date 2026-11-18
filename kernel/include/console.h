/* console.h — VGA-консоль + kprintf + panic (вывод дублируется в COM1). */
#pragma once

#include "types.h"

void console_init(void);
void kputc(char c);
void kputs(const char *s);
void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define KASSERT(cond) \
    do { if (!(cond)) panic("assert failed: %s (%s:%d)", #cond, __FILE__, __LINE__); } while (0)
