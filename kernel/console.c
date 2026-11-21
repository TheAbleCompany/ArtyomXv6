/* console.c — консоль ядра: линейный фреймбуфер Limine через flanterm +
 * дублирование в COM1.
 *
 * VGA-текстовый буфер (0xB8000) из старой версии удалён сознательно:
 * при UEFI-загрузке и на современном железе текстового режима может не
 * быть вовсе (до чёрного экрана при «успешной» загрузке), а фреймбуфер,
 * выданный Limine, есть всегда. flanterm даёт скроллинг, ANSI-цвета и
 * аппаратно-независимый курсор. */
#include "console.h"
#include "bootinfo.h"
#include "serial.h"
#include <stdarg.h>
#include "../flanterm/flanterm.h"
#include "../flanterm/flanterm_backends/fb.h"

static struct flanterm_context *ft_ctx;

void console_init(void) {
    if (!boot_have_fb) return;      /* только COM1 — уже работает */

    static uint32_t ansi[8] = {
        0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
        0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA
    };
    static uint32_t ansi_bright[8] = {
        0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
        0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF
    };
    uint32_t bg = 0x000000;
    uint32_t fg = 0xD8D8D8;
    uint32_t bg_bright = 0x555555;
    uint32_t fg_bright = 0xFFFFFF;

    /* malloc == NULL: flanterm использует собственный bump-аллокатор
     * (тот же приём, что в xOS) — куча ядра ещё не инициализирована */
    ft_ctx = flanterm_fb_init(
        NULL, NULL,
        (uint32_t *)(uintptr_t)boot_fb.addr,
        (size_t)boot_fb.width,
        (size_t)boot_fb.height,
        (size_t)boot_fb.pitch,
        boot_fb.red_size, boot_fb.red_shift,
        boot_fb.green_size, boot_fb.green_shift,
        boot_fb.blue_size, boot_fb.blue_shift,
        NULL,                           /* canvas: нет */
        ansi, ansi_bright,
        &bg, &fg, &bg_bright, &fg_bright,
        NULL, 0, 0, 0,                  /* шрифт: встроенный */
        0, 0,                           /* масштаб шрифта: авто */
        2,                              /* отступ */
        0,                              /* поворот */
        true                            /* autoflush */
    );
}

void kputc(char c) {
    serial_putc(c);
    if (ft_ctx) {
        /* flanterm двигает курсор как настоящий терминал: \n — только
         * перевод строки (колонка сохраняется), поэтому для вывода
         * «как на VGA» переводим \n в \r\n */
        if (c == '\n')
            flanterm_write(ft_ctx, "\r\n", 2);
        else
            flanterm_write(ft_ctx, &c, 1);
    }
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
    kputs(" - system halted\n");
    qemu_exit(1);
}
