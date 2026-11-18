# molloy-xv6-synth

Синтез ядра: **структура туториалов Джеймса Моллоя** («Roll your own toy
UNIX-clone OS») + **механизмы xv6** (MIT), x86_64, GRUB2/multiboot2.

Задача: убрать известные недостатки туториалов Моллоя, сохранив их
минимализм, добавив корректный и недостающий код из xv6.

## Сборка и запуск

```bash
make          # build/os.iso (ядро + initrd + GRUB)
make run      # QEMU с выводом на serial/VGA
make test     # smoke-тест: куча, кадры, initrd, fork/wait/exit в ring 3
```

Инструменты (gcc, ld, objcopy — системные). grub-mkrescue/qemu/xorriso
могут отсутствовать в системе: скрипт `scripts/setup-tools.sh` скачивает
нужные .deb (apt-get download) и распаковывает их без root в
`/home/z/my-project/tools/root` (TOOLROOT в Makefile). Новые (ELF)
grub-mkrescue игнорируют pkgdatadir — в Makefile модули передаются через
`-d $(TOOLROOT)/usr/lib/grub/i386-pc`.

## Архитектура

- **Загрузка**: boot.S — multiboot2 → long mode → kmain. Прямое
  отображение всей физики <1GiB по `KERNBASE = 0xFFFFFFFF80000000`
  (аналог KERNBASE у xv6) одной 1GiB-страницей; identity-карта
  используется только для перехода и сбрасывается.
- **Память**: paging.c — 4-уровневый walk через `p2v()` (все таблицы в
  куче ядра), `clone_address_space`/`free_address_space` для fork/exit;
  битовая карта кадров (API Моллоя: alloc_frame/clear_frame/test_frame).
- **Куча**: kheap.c — first-fit Моллоя с исправлениями (см. таблицу).
- **Процессы**: task.c + swtch.S — полный trapframe на стеке ядра,
  struct context + swtch (xv6), состояния EMBRYO/RUNNABLE/RUNNING/
  SLEEPING/ZOMBIE, fork/exit/wait, sleep/wakeup, репарентинг сирот.
- **Блокировки**: spinlock.c — xv6-дисциплина push_off/pop_off,
  единая task_lock (аналог ptable.lock).
- **VFS + initrd**: узлы-функции Моллоя + ustar-подобный образ
  (mkinitrd.py) с проверкой границ.
- **Syscalls**: int 0x80 (DPL 3), argint/argptr/argstr из xv6 с
  проверкой указателей пользователя; exit/fork/wait/getpid/read/write/
  open/close/sleep/uptime.
- **User space**: init.bin (кольцо 3) грузится из initrd по 0x400000,
  стек 0x7FFFC000..0x80000000, TSS.rsp0 перепривязывается при каждом
  переключении.

## Таблица «недостаток Моллоя → решение из xv6»

| № | Недостаток туториалов Моллоя | Решение (xv6/исправление) | Файл |
|---|---|---|---|
| T-1 | `switch_task()` дёргает esp/eip из обработчика таймера вручную; неполное сохранение регистров | полный trapframe на стеке ядра + переключение ядра через `swtch` (yield как в xv6) | task.c, swtch.S, isr.S |
| T-2 | `fork()` с хаком `read_eip()==0x12345`, ломаный кадр ребёнка | ребёнок получает копию полного кадра, `rax=0`, копию адресного пространства | task.c, paging.c |
| T-3 | нет exit/wait: задача исчезает, код возврата теряется | состояния ZOMBIE, `wait()` жатва (кадры, таблицы, стек), передача статуса | task.c |
| T-4 | `move_stack`/`initial_esp` — перенос стека ядра, источник порчи | свой стек ядра на задачу (kmalloc_a), `tss_set_rsp0` при каждом переключении | task.c, gdt.c |
| T-5 | вытеснение внутри критических секций (глобальные cli/sti) | вытеснение только при `ncli==0` (xv6 yield), spinlock + push_off/pop_off | spinlock.c, timer.c |
| K-1 | индекс дыр лежит «внутри самой кучи» | индекс в .bss, фиксированная ёмкость, переполнение → panic | kheap.c |
| K-2 | остаток блока после split теряется (утечка) | суффикс-дыра отрезается всегда; мелкий остаток живёт в списке и восстанавливается при слиянии | kheap.c |
| K-3 | allocate_aligned портит индекс и футеры | префиксная дыра = валидный блок (>= HDR+FOOT), футер перед aligned-заголовком | kheap.c |
| K-4 | удаление из индекса по совпадению размера удаляет не ту дыру | удаление строго по указателю | kheap.c |
| K-5 | нет защиты от двойного kfree | проверка magic/hole → panic | kheap.c |
| K-6 | слияние слева/справа портит футеры и список | согласованное обновление футеров/списка (проверено юнит-тестом) | kheap.c |
| K-7 | contract() ломает границы кучи | сжатие только целыми страницами, только у конца кучи; кадры возвращаются в битовую карту | kheap.c |
| P-1 | самонастройка PML4[511] усложняет clone | таблицы в куче ядра → доступ через p2v() из любого пространства | paging.c |
| P-2 | copy_page_physical через глобальный флаг+cli | копирование напрямую через p2v (кадры <1GiB в прямом отображении) | paging.c |
| P-3 | clone_directory смешивает флаги и «недосоздаёт» таблицы | пользовательская половина строится заново, копируются только PTE с U | paging.c |
| P-4 | нет destroy адресного пространства | free_address_space: кадры + таблицы пользователя, общие таблицы ядра не трогаются | paging.c |
| I-1 | initrd: нет проверки магии/границ | проверка magic, границ каждого файла, обрезка read за концом | initrd.c |
| S-1 | нет системных вызовов с валидацией | таблица syscall + argint/argptr/argstr с проверкой [PAGE_SIZE, USER_TOP) | syscall.c |

## Таблица «дефекты первой сборки → исправление» (отладка QEMU)

| № | Симптом / дефект | Исправление | Файл |
|---|---|---|---|
| X-1 | `pop_off: underflow` в первом же планировании: смена `current_task` внутри пары push/pop — push учёл глубину в `boot_ncli`, pop читает `p->ncli==0` | смена `current_task`/CR3/RSP0 вынесена за пределы блокировки (только между парами push/pop) | task.c |
| X-2 | self-дедлок: `do_exit()` зовёт `wakeup_chan()` при удержанном `task_lock` (внутри он делает `spin_lock`) | `wakeup_locked()` без взятия блокировки; публичный `wakeup_chan` = lock+scan | task.c |
| X-3 | EOI отправлялся ПОСЛЕ обработчика: `yield()` уходит в планировщик и кадр IRQ0 висел в PIC in-service → потеря тиков, зависание `hlt` | `send_eoi` до обработчика | idt.c |
| X-4 | при `int 0x80` из ring 3 — #SS: rsp0 в TSS на офсете 2 (u16 reserved), процессор читает офсет 4 | раскладка TSS64 по Intel SDM: `u32 reserved0` перед rsp0 | gdt.c |
| X-5 | «дёрганые» адреса/двойной fault с RSP=0: swtch сохранял rsp в ctx+0, а ctx лежит ВНУТРИ стека задачи — C-кадры ядра затирали его | протокол как в xv6: `swtch(&task->ctx, new)` — rsp пишется в слот task-структуры, `new` — значение-адрес кадра | swtch.S, task.c |
| X-6 | после exit(0) QEMU не завершался (таймаут теста): qemu_exit писал в 0xF4, у isa-debug-exit дефолт iobase=0x501 | порт 0x501 (комментарий обновлён) | types.h |
| X-7 | printf в user-режиме молчал: fd 1 не открыт (spawn не назначал stdio) | fds[0..2] = console_node, как init в xv6 | task.c, syscall.c |
| X-8 | кракозябры на VGA-консоли: кириллические строки ядра | весь вывод ядра — ASCII | console.c, main.c |

## Статус smoke-теста

**PASS** (3 прогона подряд, QEMU завершается сам через isa-debug-exit):

```
[TEST] heap: PASS (merge ok, aligned ok, phys ok)
[TEST] frames: PASS
[TEST] initrd: PASS
init: user mode OK, pid=1            <- кольцо 3, int 0x80
init: /hello.txt -> "Hello from initrd!"
init: fork()...
child: pid=2, exit(42)               <- fork/exit, ZOMBIE
parent: wait()=2 status=42 -> PASS   <- жатва: кадры+таблицы+стек
parent: sleep+wait -> PASS           <- вытеснение таймером, sleep/wakeup
ALL TESTS PASSED                     <- exit(0) -> QEMU exit
```

## Структура

```
Makefile linker.ld grub.cfg scripts/mkinitrd.py scripts/setup-tools.sh
kernel/  boot.S isr.S swtch.S main.c console.c serial.c kprintf.c
         gdt.c idt.c timer.c paging.c kheap.c spinlock.c task.c
         vfs.c initrd.c syscall.c lib.c  include/*.h
user/    usys.S ulib.c ulib.h init.c user.ld
```

Примечания: пользовательский бинарник — плоский образ (без ELF-загрузчика,
exec не входит в минимальный объём); /console — устройство VFS, привязанное
к COM1+VGA; вытеснение — по IRQ0 (PIT 100 Гц).
