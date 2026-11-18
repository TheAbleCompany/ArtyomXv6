/* idt.c — IDT (256 гейтов), ремап PIC на 32..47, диспетчер прерываний.
 * Гейт 0x80 открыт для ring 3 (DPL=3) — системные вызовы. */
#include "idt.h"
#include "console.h"
#include "gdt.h"
#include "task.h"
#include "syscall.h"
#include "types.h"

typedef struct {
    u16 base_lo;
    u16 sel;
    u8  ist;
    u8  flags;
    u16 base_mid;
    u32 base_hi;
    u32 reserved;
} __attribute__((packed)) idt_entry_t;

static idt_entry_t idt[256];
static struct { u16 lim; u64 base; } __attribute__((packed)) idt_desc;

extern const void *isr_stub_table[256];   /* isr.S */
void timer_irq(trapframe_t *tf);          /* timer.c */

static void idt_set_gate(int n, u64 handler, u8 flags) {
    idt[n].base_lo = (u16)handler;
    idt[n].sel = KERNEL_CS;
    idt[n].ist = 0;
    idt[n].flags = flags;
    idt[n].base_mid = (u16)(handler >> 16);
    idt[n].base_hi = (u32)(handler >> 32);
    idt[n].reserved = 0;
}

static void pic_remap(void) {
    outb(0x20, 0x11); io_wait();
    outb(0xA0, 0x11); io_wait();
    outb(0x21, 0x20);               /* master -> 32 */
    outb(0xA1, 0x28);               /* slave  -> 40 */
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFE);               /* открыт только IRQ0 (таймер) */
    outb(0xA1, 0xFF);
}

static void send_eoi(int irq) {
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void idt_init(void) {
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, (u64)isr_stub_table[i], 0x8E);   /* P|DPL0|interrupt gate */
    idt_set_gate(128, (u64)isr_stub_table[128], 0xEE);   /* P|DPL3|int gate */

    pic_remap();

    idt_desc.lim = sizeof(idt) - 1;
    idt_desc.base = (u64)idt;
    __asm__ volatile("lidt %0" :: "m"(idt_desc) : "memory");
}

static const char *ex_names[32] = {
    "#DE", "#DB", "#NMI", "#BP", "#OF", "#BR", "#UD", "#NM",
    "#DF", "#CSo", "#TS", "#NP", "#SS", "#GP", "#PF", "-",
    "#MF", "#AC", "#MC", "#XM", "#VE", "-", "-", "-",
    "-", "-", "-", "-", "-", "-", "#SX", "-"
};

static void exception_handler(trapframe_t *tf) {
    if (tf->int_no == 14) {
        u64 fa = read_cr2();
        if (tf->cs & 3) {          /* отказ в задаче пользователя: убиваем её */
            kprintf("[fault] page fault (user) at 0x%llx, task %d killed\n",
                    fa, current_task ? current_task->id : -1);
            do_exit(139);
        }
        panic("page fault in kernel at 0x%llx, rip=0x%llx", fa, tf->rip);
    }
    panic("exception %llu %s err=%llu rip=0x%llx",
          tf->int_no,
          tf->int_no < 32 ? ex_names[tf->int_no] : "?",
          tf->err, tf->rip);
}

trapframe_t *isr_dispatch(trapframe_t *tf) {
    if (tf->int_no < 32) {
        exception_handler(tf);     /* не возвращается для фатальных */
        return tf;
    }
    if (tf->int_no < 48) {
        /* EOI ДО обработчика: timer_irq может вызвать yield() и уйти в
         * планировщик (возврат в этот кадр — после swtch обратно). EOI
         * после обработчика, как раньше, замораживал PIC в in-service,
         * пока задача не возобновится -> потеря тиков, зависание hlt(). */
        send_eoi(tf->int_no - 32);
        if (tf->int_no == 32) timer_irq(tf);
        return tf;
    }
    if (tf->int_no == 128) return syscall_dispatch(tf);
    kprintf("[irq] unexpected vector %llu (ignored)\n", tf->int_no);
    return tf;
}
