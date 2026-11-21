# CHANGES: limine — перенос загрузки с GRUB2/multiboot2 на Limine

Порт загрузочного слоя по образцу xOS (TheAbleCompany/xOS): вендоренные
файлы Limine, `limine.conf`, ISO через xorriso + `limine bios-install`,
консоль на фреймббуфере через flanterm. Протокол — **limine boot protocol**
(не эмуляция multiboot: вход в ядро уже в long mode по высокому VA).

Проверено в QEMU 10: BIOS+ISO, UEFI+ISO (OVMF), BIOS+HDD, UEFI+HDD —
smoke-тест (куча, кадры, initrd, fork/wait/exit в ring 3) = **ALL TESTS
PASSED** во всех четырёх сценариях; скриншоты фреймбуфера показывают
полный вывод ядра (чёрного экрана нет).

## Зачем

| Проблема GRUB2/multiboot2 | Что даёт Limine |
|---|---|
| GRUB тянет свои модули и `grub-mkrescue`; ISO зависит от версии GRUB | вендоренные бинарники Limine в репозитории, воспроизводимая сборка |
| BIOS и UEFI — разные пути загрузки GRUB (i386-pc vs EFI-модули) | один ISO: El Torito BIOS (`limine-bios-cd.bin`) + UEFI (`limine-uefi-cd.bin`); один HDD: GPT+ESP+BIOS boot |
| VGA-текст (0xB8000) отсутствует на UEFI и на части современного железа → «чёрный экран при успешной загрузке» | линейный фреймбуфер выдаётся всегда; flanterm даёт консоль с ANSI-цветами и курсором |
| нет menu/timeout из коробки | `limine.conf`: меню с таймаутом 3 с, пути `boot():/boot/...` |
| multiboot2 не даёт HHDM и пишет mbi в «своём» формате | единый протокол: framebuffer/hhdm/memmap/module-запросы, base revision 3 |

## Изменения по файлам

| Файл | Изменение |
|---|---|
| `linker.ld` | убраны `.entry_stub`/`.multiboot` (multiboot2 больше не нужен); добавлены секции `.limine_requests_start/.limine_requests/.limine_requests_end` (маркеры + запросы, KEEP, ALIGN 8); **все секции получили `AT(ADDR(x) - KERNBASE)`** — иначе ld даёт PhysAddr = VirtAddr и Limine грузит ядро «куда придётся» (первый баг порта) |
| `kernel/boot.S` | переписан: точка входа протокола Limine — 64-битный код на высоком VA (Limine входит в ядро в long mode). Осталось: cli/cld, свой стек 16 КиБ (стек Limine по размеру не гарантирован), `call limine_boot_main` |
| `kernel/limine.c` | **новый** — мост Limine → ядро: запросы (framebuffer, hhdm, memmap, module rev1, bootloader info, cmdline, kernel address) в `.limine_requests`; конвертация карты памяти в mb2-совместимые записи для frames_init; модуль initrd (HHDM VA + размер); **takeover_paging** (см. ниже); ранний вывод в COM1 до инициализации консоли |
| `kernel/include/bootinfo.h` | **новый** — контракт моста: boot_memmap[], boot_initrd_va/size/phys, boot_fb, boot_hhdm_offset, kernel_cr3_phys, kern_slide, limine_boot_main() |
| `kernel/include/limine.h` | **новый** — официальный заголовок Limine v9 (вендорен) |
| `kernel/main.c` | разбор multiboot2 (parse_multiboot, magic-проверка) удалён; kmain(void) получает boot info из limine.c; копирование initrd идёт из HHDM-VA модуля; остальное тело kmain без изменений |
| `kernel/console.c` | VGA-текст удалён полностью (на UEFI его нет); консоль — **flanterm** на фреймбуфере Limine (`flanterm_fb_init`, malloc=NULL → внутренний bump-аллокатор, autoflush), вывод дублируется в COM1; `\n` переводится в `\r\n` для flanterm (в настоящем терминале \n не возвращает каретку — иначе «лесенка»); panic остался (fb+serial+qemu_exit) |
| `kernel/paging.c` | `paging_init()` берёт CR3 Limine (kernel_cr3_phys из limine.c) вместо таблиц boot.S; остальное без изменений (таблицы в куче, clone/free — как было) |
| `kernel/include/multiboot2.h` | остались только типы записей карты памяти (формат boot info для frames_init); теги multiboot2 удалены |
| `kernel/include/types.h` | p2v/kv2ph получают поправку `kern_slide` (после takeover она = 0; поле оставлено как документированный инвариант модели) |
| `flanterm/` | **новый** — вендоренный flanterm (BSD-2-Clause), те же исходники, что в xOS |
| `limine-binary/` | **новый** — вендоренные файлы Limine v9.x-binary: limine-bios.sys, limine-bios-cd.bin, limine-uefi-cd.bin, BOOTX64.EFI, BOOTIA32.EFI, limine.c/limine.h/limine-bios-hdd.h (host-утилита собирается `make`) |
| `limine.conf` | **новый** — `timeout: 3`, запись `protocol: limine`, `path: boot():/boot/kernel.elf`, `module_path: boot():/boot/initrd.bin`; кладётся в /boot/limine/ и в ISO, и на ESP |
| `Makefile` | цели GRUB/grub-mkrescue удалены; `all` = ISO + HDD; `limine-iso` через `xorriso -as mkisofs` (El Torito BIOS+UEFI, protective MBR) + `limine bios-install`; `limine-hdd` через `scripts/mkhdd.py` + `limine bios-install <img> 2`; `run-uefi` (OVMF); флаги ядра прежние |
| `scripts/mkhdd.py` | **новый** — raw HDD-образ 96 МиБ на чистом Python (зависимостей нет): GPT (CRC32 заголовков, protective MBR), раздел 1 = ESP FAT32 (BPB, FSInfo, LFN-записи, цепочки FAT, dot-записи) с /boot/{kernel.elf,initrd.bin}, /boot/limine/{limine.conf,limine-bios.sys}, /EFI/BOOT/{BOOTX64,BOOTIA32}.EFI; раздел 2 = BIOS boot (1 МиБ) для stage 2 (`limine bios-install <img> 2`) |
| `scripts/setup-tools.sh`, `grub.cfg` | удалены (grub-инфраструктура больше не нужна) |

## Путь загрузки (после порта)

```
Limine menu (limine.conf, timeout 3)
  → Limine грузит kernel.elf (сегменты по ELF paddr + ФИЗИЧЕСКИЙ KASLR)
    и initrd.bin (module_path), заполняет ответы запросов
  → _start (long mode, VA ядра, HHDM активен)
    → limine_boot_main():
        serial_init()
        мост: memmap → boot_memmap[], module → boot_initrd_*, fb → boot_fb
        takeover_paging():
            PD_510: 512 x 2MiB (PA i*2MiB <-> VA KERNBASE + i*2MiB)
            PML4[511] Limine -> наш PDPT
            relocate_kernel(): образ (memsz, с .bss и таблицами)
                копируется HHDM-записью с physical_base на 0x100000
            стаб: mov cr3 (TLB flush) → mov rsp, boot_stack_top
                → jmp after_cr3_switch
        after_cr3_switch(): console_init() (flanterm) → kmain()
  → kmain(): GDT/IDT/PIT/paging/kheap/frames → initrd → spawn init
    → планировщик (кольцо 3)
```

## Грабли, найденные при отладке (все воспроизведены в QEMU)

| № | Симптом | Причина | Исправление |
|---|---|---|---|
| L-1 | тройной фолт на входе в C: `readelf -l` показывает PhysAddr = VirtAddr (0xffffffff80100000) | в новом linker.ld секции писались без `AT(ADDR(x) - KERNBASE)` — LMA = VMA, Limine не может загрузить сегменты по ним | AT() во всех секциях; проверять `readelf -l build/kernel.elf` |
| L-2 | «P=0» на любом доступе после подмены PML4[511], хотя запись в PML4 читается обратно корректно | **физический KASLR Limine**: `kernel address request` даёт physical_base = 0x7e10000, а не ELF paddr 0x100000; ядро в памяти НЕ там, куда указывает прямое отображение | relocate_kernel(): копия образа на 0x100000 до смены отображения |
| L-3 | e=0x18 (RSVD, fetch) сразу после смены CR3 | 2MiB-записи клали в PDPT (PS=1 в PDPT = 1GiB-страница, у неё биты 29:21 зарезервированы); кроме того slide не выровнен на 2MiB | PDPT[510] → PD_510 c 2MiB-страницами PA i*2MiB (после релокации слайд = 0, выравнивание соблюдено) |
| L-4 | «лесенка»/мусор на экране при корректном выводе в serial | flanterm: `\n` — только LF (колонка сохраняется), VGA-консоль делала CR+LF | kputc: `\n` → `\r\n` для flanterm |
| L-5 | panic «kheap: out of memory» после успешной загрузки | KHEAP_VA_BASE = KERNBASE+32MiB → PA 32MiB+slide перекрывал образ ядра (физический KASLR кладёт образ у верха RAM) | устраняется релокацией (слайд = 0 → куча на PA 32MiB, как в GRUB-версии) |
| L-6 | дрейф регистров/«мусорный» код после переключения CR3 | кадры стека живут в физике СТАРОГО образа; после переключения VA стека показывает на копию, где этих кадров нет | стаб: после `mov cr3` немедленно `mov rsp, boot_stack_top; jmp after_cr3_switch` (функция без живых входных данных) |
| L-7 | Limine: «Staging: Unknown kernel executable format» на HDD | mkhdd.py не выделял кластер 2 под корень FAT32: первая alloc_chain выдала kernel.elf кластер 2, корневой каталог (пишется последним) затёр ELF-заголовок; кроме того FAT изначально была 64 сектора вместо ~768 | явный alloc для кластера 2; fat_sectors = ceil(sectors*4/512); проверка mcopy+cmp всех файлов ESP |
| L-8 | BIOS-загрузка ISO не стартовала вовсе | забыт `limine bios-install` — в Makefile есть, проверять его вывод | `limine bios-install <iso>` после xorriso (входит в цель) |

## Замечания для реального железа

- Протокол Limine (base revision 3) поддерживается бинарниками v8/v9;
  более старые загрузчики сообщат «base rev NOT supported» в COM1 и
  продолжат без feature-запросов уровня ревизии 3 (мы их не используем).
- Отсутствие фреймбуфера (не обнаружено ни одного framebuffer) — не
  критично: консоль деградирует в COM1, ядро продолжает загрузку.
- Отсутствие модуля initrd — panic с явным текстом («check limine.conf
  module_path»): молча продолжать бессмысленно, init всё равно не запустить.
- Ничего не зависит от: ATAPI/NVMe-дисков (только initrd), ACPI-таблиц
  (PIT и PIC, не HPET/APIC-таймер), MP-старта (один BSP), номера COM-порта
  выше 0x3F8 (вывод только если COM1 есть; при отсутствии — молчание, но не
  зависание: в отличие от xOS-r12 не опрашивается «плавающая» шина).
- `limine bios-install` вносит stage 2 в ISO/HDD — файлы ISO/HDD нельзя
  править после него байт-в-байт без повторной установки.

## Матрица проверок (QEMU 10.0.13)

| Сценарий | Прошивка | Результат |
|---|---|---|
| `-cdrom os.iso` | SeaBIOS | ALL TESTS PASSED |
| `-cdrom os.iso` + OVMF 4M | UEFI | ALL TESTS PASSED |
| `-hda os.hdd -boot c` | SeaBIOS | ALL TESTS PASSED |
| `-hda os.hdd` + OVMF 4M | UEFI | ALL TESTS PASSED |
| screendump (BIOS/UEFI) | — | полный вывод ядра на фреймбуфере, чёрного экрана нет |
