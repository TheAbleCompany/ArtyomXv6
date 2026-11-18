/* init.c — первый процесс пользователя: smoke-тест fork/wait/exit/ getpid
 * и файловых вызовов open/read/close. Печатает результат в /console (fd 1)
 * и завершается кодом 0 при успехе. */
#include "ulib.h"

int main(void) {
    printf("init: user mode OK, pid=%d\n", getpid());

    /* 1. файловая система: open/read/close */
    int fd = open("/hello.txt", 0);
    if (fd < 0) {
        printf("init: FAIL open /hello.txt\n");
        exit(1);
    }
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = read(fd, buf, 60);
    close(fd);
    if (n <= 0) {
        printf("init: FAIL read\n");
        exit(1);
    }
    buf[n] = 0;
    printf("init: /hello.txt -> \"%s\"\n", buf);

    /* 2. fork/wait/exit: ребёнок выходит с кодом 42, родитель жатву */
    printf("init: fork()...\n");
    int pid = fork();
    if (pid < 0) {
        printf("init: FAIL fork\n");
        exit(1);
    }
    if (pid == 0) {
        printf("child: pid=%d, exit(42)\n", getpid());
        exit(42);
    }
    int st = -1;
    int w = wait(&st);
    int ok = (w == pid && st == 42);
    printf("parent: wait()=%d status=%d -> %s\n", w, st, ok ? "PASS" : "FAIL");

    if (!ok) exit(1);

    /* 3. повторный fork с sleep: вытеснение таймером во время syscall */
    pid = fork();
    if (pid == 0) {
        sleep(150);
        exit(7);
    }
    w = wait(&st);
    ok = (w == pid && st == 7);
    printf("parent: sleep+wait -> %s\n", ok ? "PASS" : "FAIL");
    if (!ok) exit(1);

    printf("ALL TESTS PASSED\n");
    exit(0);
}
