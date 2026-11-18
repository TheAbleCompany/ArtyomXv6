/* linker.ld — ядро в «старшей половине» (higher half), PML4[511] = -2GiB.
 * VMA = KERNBASE + phys; GRUB (multiboot2) загружает сегменты по paddr.
 * Точка входа e_entry = физический адрес _start (GRUB входит в 32-бит режиме). */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(entry_stub)

KERNBASE = 0xFFFFFFFF80000000;

SECTIONS
{
  /* Стаб входа: GRUB (multiboot2) проверяет, что e_entry лежит внутри
   * загружаемого сегмента. Держим отдельный сегмент на 0x300000 (вне
   * основного образа), откуда уходим в _start (физ. 0x100040). */
  .entry_stub 0x300000 : AT(ADDR(.entry_stub)) { KEEP(*(.entry_stub)) }

  . = KERNBASE + 0x100000;

  .multiboot : AT(ADDR(.multiboot) - KERNBASE) { KEEP(*(.multiboot)) }
  .text      : AT(ADDR(.text)      - KERNBASE) { *(.text .text.*) }
  .rodata    : AT(ADDR(.rodata)    - KERNBASE) { *(.rodata .rodata.*) }
  .data      : AT(ADDR(.data)      - KERNBASE) { *(.data .data.*) }
  .bss       : AT(ADDR(.bss)       - KERNBASE) { *(COMMON) *(.bss .bss.*) }

  kern_end = ALIGN(., 4096);

  boot_entry_phys = _start - KERNBASE;

  /DISCARD/ : { *(.eh_frame) *(.eh_frame.*) *(.comment) *(.note*) }
}
