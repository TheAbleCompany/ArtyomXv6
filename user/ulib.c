/* ulib.c — printf и строки для user space (вывод через write(1,...)). */
#include "ulib.h"

void *memset(void *dst, int c, unsigned n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

unsigned strlen(const char *s) {
    unsigned n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void put_num(char *buf, int *n, int cap, int v) {
    char tmp[16];
    int neg = 0;
    unsigned u = (unsigned)v;
    if (v < 0) { neg = 1; u = (unsigned)(-v); }
    int i = 0;
    do { tmp[i++] = '0' + u % 10; u /= 10; } while (u);
    if (neg) tmp[i++] = '-';
    while (i && *n < cap - 1) buf[(*n)++] = tmp[--i];
}

void printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    char buf[512];
    int n = 0;
    for (; *fmt && n < (int)sizeof(buf) - 1; fmt++) {
        if (*fmt != '%') { buf[n++] = *fmt; continue; }
        fmt++;
        switch (*fmt) {
        case 's': {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            while (*s && n < (int)sizeof(buf) - 1) buf[n++] = *s++;
            break; }
        case 'c':
            buf[n++] = (char)__builtin_va_arg(ap, int);
            break;
        case 'd': case 'i':
            put_num(buf, &n, (int)sizeof(buf), __builtin_va_arg(ap, int));
            break;
        case '%':
            buf[n++] = '%';
            break;
        default:
            buf[n++] = '%';
            buf[n++] = *fmt;
            break;
        }
    }
    buf[n] = 0;
    __builtin_va_end(ap);
    write(1, buf, n);
}
