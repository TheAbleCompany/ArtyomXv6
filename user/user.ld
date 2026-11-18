/* user.ld — пользовательская программа: код по 0x400000, стек кладёт ядро.
 * Плоский бинарник получается через objcopy -O binary. */
ENTRY(_start)

SECTIONS
{
  . = 0x400000;
  .text   : { *(.text.start) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*) }
  .bss    : { *(.bss .bss.*) *(COMMON) }
  /DISCARD/ : { *(.eh_frame) *(.eh_frame.*) *(.comment) *(.note*) }
}
