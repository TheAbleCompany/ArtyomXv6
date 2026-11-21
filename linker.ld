/* linker.ld — ядро в «старшей половине» (higher half), PML4[511] = -2GiB.
 * VMA = KERNBASE + phys; Limine (протокол limine) загружает сегменты по
 * paddr из ELF и мапит их по линковочному VMA, после чего входит в ядро
 * уже в 64-бит режиме на e_entry = _start (высокий VA).
 *
 * Требования Limine: маркеры запросов (.limine_requests_start / .limine_requests /
 * .limine_requests_end) должны идти в указанном порядке по виртуальным адресам
 * и лежать внутри загружаемых сегментов — поэтому они идут первыми секциями. */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNBASE = 0xFFFFFFFF80000000;

SECTIONS
{
  . = KERNBASE + 0x100000;

  .limine_requests_start ALIGN(8) :
      AT(ADDR(.limine_requests_start) - KERNBASE) {
      KEEP(*(.limine_requests_start))
  }

  .limine_requests ALIGN(8) :
      AT(ADDR(.limine_requests) - KERNBASE) {
      KEEP(*(.limine_requests))
  }

  .limine_requests_end ALIGN(8) :
      AT(ADDR(.limine_requests_end) - KERNBASE) {
      KEEP(*(.limine_requests_end))
  }

  .text   ALIGN(16) : AT(ADDR(.text)   - KERNBASE) { *(.text .text.*) }
  .rodata ALIGN(16) : AT(ADDR(.rodata) - KERNBASE) { *(.rodata .rodata.*) }
  .data   ALIGN(16) : AT(ADDR(.data)   - KERNBASE) { *(.data .data.*) }
  .bss    ALIGN(16) : AT(ADDR(.bss)    - KERNBASE) { *(COMMON) *(.bss .bss.*) }

  kern_end = ALIGN(., 4096);

  /DISCARD/ : { *(.eh_frame) *(.eh_frame.*) *(.comment) *(.note*) }
}
