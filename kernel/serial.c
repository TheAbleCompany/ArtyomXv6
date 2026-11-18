/* serial.c — COM1, 38400 8N1 (порт туториала Моллоя, вывод тестов). */
#include "serial.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);         /* прерывания выключены */
    outb(COM1 + 3, 0x80);         /* DLAB */
    outb(COM1 + 0, 0x03);         /* делитель 3 -> 38400 бод */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);         /* 8N1 */
    outb(COM1 + 2, 0xC7);         /* FIFO, очистка, 14-байтовый порог */
    outb(COM1 + 4, 0x0B);         /* RTS | DTR */
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) ;   /* ждём THRE */
    outb(COM1, (u8)c);
}

int serial_getc(void) {
    if (!(inb(COM1 + 5) & 0x01)) return -1;   /* нет данных */
    return inb(COM1);
}
