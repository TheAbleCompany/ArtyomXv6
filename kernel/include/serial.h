/* serial.h — COM1 (0x3F8), 38400 8N1: вывод тестов и логов в QEMU. */
#pragma once

#include "types.h"

void serial_init(void);
void serial_putc(char c);
int  serial_getc(void);        /* -1, если символа нет (неблокирующий poll) */
