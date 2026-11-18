/* ulib.h — API пользовательских программ (обёртки int 0x80). */
#pragma once

int   fork(void);
void  exit(int status) __attribute__((noreturn));
int   wait(int *status);
int   getpid(void);
int   read(int fd, void *buf, int n);
int   write(int fd, const void *buf, int n);
int   open(const char *path, int flags);
int   close(int fd);
void  sleep(int ms);
int   uptime(void);

int   main(void);

void *memset(void *dst, int c, unsigned n);
void *memcpy(void *dst, const void *src, unsigned n);
unsigned strlen(const char *s);
int   strcmp(const char *a, const char *b);

void  printf(const char *fmt, ...);
