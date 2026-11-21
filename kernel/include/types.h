/* types.h — базовые типы, порт-ввод/вывод, преобразование физ/вирт адресов.
 * Модель памяти: PML4[511] -> PDPT[2] -> гигабайтная страница:
 *   VA [KERNBASE, KERNBASE+1GiB) <-> PA [0, 1GiB)  (прямое отображение, как
 *   KERNBASE у xv6 — заменяет самонастройку PML4[511] из туториала Моллоя). */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
/* u64/s64 как long long: printf-модификаторы %llx/%lld совпадают
 * (uint64_t на x86_64-linux = unsigned long, что путает -Wformat). */
typedef unsigned long long u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef long long          s64;

#include "lib.h"

#define KERNBASE 0xFFFFFFFF80000000ULL

/* Слайд физической загрузки ядра: Limine грузит образ НЕ по ELF paddr
 * (физический KASLR), оставляя виртуальные адресы линковочными. Прямое
 * отображение поэтому сдвинуто: p2v(p) = KERNBASE + p - kern_slide даёт
 * VA, который транслируется на TRUE PA p. До takeover_paging() слайд = 0;
 * до него обращений через p2v к памяти не бывает (мосты limine.c работают
 * через HHDM-адреса напрямую). */
extern u64 kern_slide;

static inline u64 p2v(u64 phys) { return phys + KERNBASE - kern_slide; }
static inline u64 kv2ph(u64 va)  { return va - KERNBASE + kern_slide; }

static inline u64 align_up(u64 x, u64 a)   { return (x + a - 1) & ~(a - 1); }
static inline u64 align_down(u64 x, u64 a) { return x & ~(a - 1); }

static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port) : "memory");
}
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port) : "memory");
    return v;
}
static inline void io_wait(void) { outb(0x80, 0); }

static inline void cli(void) { __asm__ volatile("cli" ::: "memory"); }
static inline void sti(void) { __asm__ volatile("sti" ::: "memory"); }
static inline void hlt(void) { __asm__ volatile("hlt"); }
static inline u64 read_cr2(void) {
    u64 v;
    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}
static inline void invlpg(u64 va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}
static inline void barrier(void) { __asm__ volatile("" ::: "memory"); }

/* аварийная остановка (реализация в console.c) */
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* выход из QEMU через isa-debug-exit; порт — дефолт устройства
 * (iobase=1281=0x501 в QEMU 2.x..10.x), код выхода = (val<<1)|1 */
__attribute__((noreturn)) static inline void qemu_exit(int val) {
    outb(0x501, (u8)val);
    for (;;) hlt();
}
