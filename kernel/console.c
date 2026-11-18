/* console.c — VGA-текстовый буфер (через прямое отображение KERNBASE+0xB8000,
 * поскольку identity-карта после загрузки сброшена) + panic. */
#include "console.h"
#include "serial.h"
#include <stdarg.h>

#define VGA_COLS 80
#define VGA_ROWS 25

static volatile u16 *const vga = (volatile u16 *)(KERNBASE + 0xB8000);
static int crow = 0, ccol = 0;
static u8 attr = 0x0F;            /* белый на чёрном */

static void vga_update_cursor(void) {
    u16 pos = (u16)(crow * VGA_COLS + ccol);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8)(pos >> 8));
}

static void vga_scroll(void) {
    for (int r = 1; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga[(r - 1) * VGA_COLS + c] = vga[r * VGA_COLS + c];
    for (int c = 0; c < VGA_COLS; c++)
        vga[(VGA_ROWS - 1) * VGA_COLS + c] = ((u16)attr << 8) | ' ';
    crow = VGA_ROWS - 1;
}

static void vga_putc(char c) {
    if (c == '\n') {
        ccol = 0;
        crow++;
    } else if (c == '\b') {
        if (ccol > 0) ccol--;
        vga[crow * VGA_COLS + ccol] = ((u16)attr << 8) | ' ';
    } else {
        vga[crow * VGA_COLS + ccol] = ((u16)attr << 8) | (u8)c;
        ccol++;
    }
    if (ccol >= VGA_COLS) { ccol = 0; crow++; }
    if (crow >= VGA_ROWS) vga_scroll();
    vga_update_cursor();
}

void console_init(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = ((u16)attr << 8) | ' ';
    crow = ccol = 0;
    /* включаем аппаратный курсор */
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | 13);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | 14);
}

void kputc(char c) {
    serial_putc(c);
    vga_putc(c);
}

void kputs(const char *s) {
    while (*s) kputc(*s++);
}

void panic(const char *fmt, ...) {
    cli();
    kputs("\n!!! PANIC: ");
    va_list ap;
    char buf[512];
    va_start(ap, fmt);
    /* повторно используем kprintf-движок через внутренний буфер */
    extern void kvsprintf(char *out, size_t cap, const char *fmt, va_list ap);
    kvsprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kputs(buf);
    kputs(" - system halted\n");   /* ASCII: VGA-шрифт без кириллицы */
    qemu_exit(1);
}
