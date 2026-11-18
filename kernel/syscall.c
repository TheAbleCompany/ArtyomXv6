/* syscall.c — системные вызовы int 0x80: таблица + argint/argptr/argstr
 * из xv6 (syscall.c). Указатели пользователя проверяются на границы
 * [PAGE_SIZE, USER_TOP) — у Моллоя пользовательских вызовов вообще не было. */
#include "syscall.h"
#include "console.h"
#include "serial.h"
#include "timer.h"
#include "vfs.h"
#include "spinlock.h"
#include "paging.h"

static int fetch_arg(trapframe_t *tf, int n, u64 *out) {
    if (!tf || !current_task) return -1;
    switch (n) {
    case 0: *out = tf->rdi; return 0;
    case 1: *out = tf->rsi; return 0;
    case 2: *out = tf->rdx; return 0;
    case 3: *out = tf->r10; return 0;
    }
    return -1;
}

int argint(int n, int *ip) {
    u64 v;
    if (fetch_arg(current_task->tf, n, &v)) return -1;
    *ip = (int)(s64)v;
    return 0;
}

int argptr(int n, char **pp, int size) {
    u64 v;
    if (fetch_arg(current_task->tf, n, &v)) return -1;
    if (size < 0) return -1;
    u64 addr = v;
    if (addr < PAGE_SIZE || addr > USER_TOP || (u64)size > USER_TOP - addr)
        return -1;
    *pp = (char *)addr;
    return 0;
}

int argstr(int n, char **pp) {
    u64 v;
    if (fetch_arg(current_task->tf, n, &v)) return -1;
    u64 addr = v;
    if (addr < PAGE_SIZE || addr >= USER_TOP) return -1;
    const char *s = (const char *)addr;
    u64 i = 0;
    while (addr + i < USER_TOP) {
        if (s[i] == 0) { *pp = (char *)addr; return 0; }
        i++;
    }
    return -1;
}

/* устройство /console: read — serial poll, write — serial+VGA */
static u32 console_read(fs_node_t *node, u64 off, u32 len, u8 *buf) {
    (void)node; (void)off;
    u32 i = 0;
    while (i < len) {
        int c = serial_getc();
        if (c < 0) break;
        buf[i++] = (u8)c;
    }
    return i;
}

static u32 console_write(fs_node_t *node, u64 off, u32 len, const u8 *buf) {
    (void)node; (void)off;
    for (u32 i = 0; i < len; i++) kputc((char)buf[i]);
    return len;
}

fs_node_t *console_node = &(
    (fs_node_t){
        .name = "console",
        .flags = FS_CHARDEVICE,
        .read = console_read,
        .write = console_write,
    });

static int sys_open(const char *path) {
    task_t *cur = current_task;
    const char *name = (*path == '/') ? path + 1 : path;
    fs_node_t *node = NULL;
    if (strcmp(name, "console") == 0)
        node = console_node;
    else if (fs_root && fs_root->finddir)
        node = fs_root->finddir(fs_root, name);
    if (!node) return -1;

    for (int i = 0; i < NFD; i++) {
        if (!cur->fds[i]) {
            cur->fds[i] = node;
            cur->fd_off[i] = 0;
            vfs_open(node);
            return i;
        }
    }
    return -1;
}

static int sys_read(int fd, char *buf, int n) {
    task_t *cur = current_task;
    if (fd < 0 || fd >= NFD || !cur->fds[fd] || !cur->fds[fd]->read) return -1;
    if (n <= 0) return 0;
    u32 got = vfs_read(cur->fds[fd], cur->fd_off[fd], (u32)n, (u8 *)buf);
    cur->fd_off[fd] += got;
    return (int)got;
}

static int sys_write(int fd, const char *buf, int n) {
    task_t *cur = current_task;
    if (fd < 0 || fd >= NFD || !cur->fds[fd] || !cur->fds[fd]->write) return -1;
    if (n <= 0) return 0;
    u32 put = vfs_write(cur->fds[fd], cur->fd_off[fd], (u32)n, (const u8 *)buf);
    cur->fd_off[fd] += put;
    return (int)put;
}

static void sys_sleep(int ms) {
    if (ms <= 0) return;
    u64 target = timer_ticks + (u64)ms * TIMER_HZ / 1000 + 1;
    while ((s64)(timer_ticks - target) < 0) {
        spin_lock(&task_lock);
        if ((s64)(timer_ticks - target) < 0)
            sleep_chan((void *)&timer_ticks);   /* таймер будит каждый тик */
        spin_unlock(&task_lock);
    }
}

trapframe_t *syscall_dispatch(trapframe_t *tf) {
    task_t *cur = current_task;
    u64 nr = tf->rax;
    if (!cur) return tf;
    tf->rax = (u64)(s64)-1;                /* код ошибки по умолчанию */

    switch (nr) {
    case SYS_exit: {
        int code = 0;
        argint(0, &code);
        do_exit(code);                     /* не возвращается */
        break; }
    case SYS_fork:
        tf->rax = (u64)(s64)do_fork();
        break;
    case SYS_wait: {
        char *p = NULL;
        int r = -1;
        if (argptr(0, &p, sizeof(int)) == 0) r = do_wait((int *)p);
        tf->rax = (u64)(s64)r;
        break; }
    case SYS_getpid:
        tf->rax = (u64)cur->id;
        break;
    case SYS_read: {
        int fd, n;
        char *buf;
        if (argint(0, &fd) || argint(2, &n) || n <= 0 ||
            argptr(1, &buf, n)) break;
        tf->rax = (u64)(s64)sys_read(fd, buf, n);
        break; }
    case SYS_write: {
        int fd, n;
        char *buf;
        if (argint(0, &fd) || argint(2, &n) || n <= 0 ||
            argptr(1, &buf, n)) break;
        tf->rax = (u64)(s64)sys_write(fd, buf, n);
        break; }
    case SYS_open: {
        char *path;
        if (argstr(0, &path)) break;
        tf->rax = (u64)(s64)sys_open(path);
        break; }
    case SYS_close: {
        int fd;
        if (argint(0, &fd)) break;
        if (fd < 0 || fd >= NFD || !cur->fds[fd]) break;
        vfs_close(cur->fds[fd]);
        cur->fds[fd] = NULL;
        tf->rax = 0;
        break; }
    case SYS_sleep: {
        int ms;
        if (argint(0, &ms)) break;
        sys_sleep(ms);
        tf->rax = 0;
        break; }
    case SYS_uptime:
        tf->rax = timer_ticks;
        break;
    default:
        kprintf("[syscall] unknown nr=%lld from task %d\n",
                (s64)nr, cur->id);
        break;
    }
    return tf;
}
