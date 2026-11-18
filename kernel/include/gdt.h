/* gdt.h — GDT long mode + TSS (RSP0 для входа в ядро из ring 3). */
#pragma once

#include "types.h"

#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   0x1B      /* селектор 3 | RPL 3 */
#define USER_DS   0x23      /* селектор 4 | RPL 3 */
#define TSS_SEL   0x28

void gdt_init(void);
void tss_set_rsp0(u64 rsp);
