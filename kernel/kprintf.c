/* kprintf.c — минимальный форматированный вывод: %s %c %d %i %u %x %p,
 * модификаторы l/ll, %% и флаг '-' (выравнивание влево). */
#include "console.h"
#include <stdarg.h>

static void out_char(char *buf, size_t cap, size_t *n, char c) {
    if (*n + 1 < cap) buf[*n] = c;
    (*n)++;
}

static void out_str(char *buf, size_t cap, size_t *n, const char *s) {
    while (*s) out_char(buf, cap, n, *s++);
}

static void out_num(char *buf, size_t cap, size_t *n, s64 v, u64 base,
                    int upper, int pad_zero, int width, int left) {
    char tmp[32];
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int neg = 0;
    u64 uv;
    if (base == 10 && v < 0) { neg = 1; uv = (u64)(-v); } else uv = (u64)v;
    int i = 0;
    do { tmp[i++] = digits[uv % base]; uv /= base; } while (uv);
    int total = i + neg;
    if (!left && !pad_zero)
        while (width > total) { out_char(buf, cap, n, ' '); width--; }
    if (neg) out_char(buf, cap, n, '-');
    if (!left && pad_zero)
        while (width > total) { out_char(buf, cap, n, '0'); width--; }
    while (i) out_char(buf, cap, n, tmp[--i]);
    if (left)
        while (width > total) { out_char(buf, cap, n, ' '); width--; }
}

void kvsprintf(char *out, size_t cap, const char *fmt, va_list ap) {
    size_t n = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out_char(out, cap, &n, *fmt); continue; }
        fmt++;
        int left = 0, pad_zero = 0, width = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { pad_zero = 1; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int lmod = 0;
        while (*fmt == 'l') { lmod++; fmt++; }
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0; while (s[len]) len++;
            while (!left && width > len) { out_char(out, cap, &n, ' '); width--; }
            out_str(out, cap, &n, s);
            while (left && width > 0) { out_char(out, cap, &n, ' '); width--; }
            break; }
        case 'c':
            out_char(out, cap, &n, (char)va_arg(ap, int));
            break;
        case 'd': case 'i':
            out_num(out, cap, &n, lmod >= 2 ? va_arg(ap, s64)
                          : (lmod == 1 ? (s64)va_arg(ap, long) : (s64)va_arg(ap, int)),
                    10, 0, pad_zero, width, left);
            break;
        case 'u':
            out_num(out, cap, &n, lmod >= 2 ? va_arg(ap, u64)
                          : (lmod == 1 ? (u64)va_arg(ap, unsigned long)
                                       : (u64)va_arg(ap, unsigned int)),
                    10, 0, pad_zero, width, left);
            break;
        case 'x':
            out_num(out, cap, &n, lmod >= 2 ? va_arg(ap, u64)
                          : (lmod == 1 ? (u64)va_arg(ap, unsigned long)
                                       : (u64)va_arg(ap, unsigned int)),
                    16, 0, pad_zero, width, left);
            break;
        case 'p': {
            out_str(out, cap, &n, "0x");
            out_num(out, cap, &n, va_arg(ap, u64), 16, 0, 1, 16, 0);
            break; }
        case '%':
            out_char(out, cap, &n, '%');
            break;
        default:
            out_char(out, cap, &n, '%');
            out_char(out, cap, &n, *fmt);
            break;
        }
    }
    if (n < cap) out[n] = 0;
    else if (cap) out[cap - 1] = 0;
}

void kprintf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    kvsprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kputs(buf);
}
