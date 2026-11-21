# ============================================================================
# molloy-xv6-synth — синтез ядра: структура туториалов Дж. Моллоя + механизмы xv6
# Загрузчик: Limine (boot protocol) вместо GRUB2/multiboot2 — порт по образцу xOS.
#
# Сборка:   make          -> build/os.iso (BIOS+UEFI) и build/os.hdd (BIOS+UEFI)
# Запуск:   make run      -> QEMU (BIOS)
#           make run-uefi -> QEMU с OVMF (UEFI), OVMF_CODE=/usr/share/OVMF/OVMF_CODE_4M.fd
# Тест:     make test     -> smoke-тест ядра (куча, кадры, initrd, fork/wait в ring 3)
# ============================================================================

BUILD   = build
KERNEL  = $(BUILD)/kernel.elf
INITRD  = $(BUILD)/initrd.bin
ISO     = $(BUILD)/os.iso
HDD     = $(BUILD)/os.hdd

LIMINE_BIN  = limine-binary
LIMINE_TOOL = $(LIMINE_BIN)/limine
XORRISO    ?= xorriso
OVMF_CODE  ?= /usr/share/OVMF/OVMF_CODE_4M.fd

QEMU = qemu-system-x86_64

KCFLAGS = -m64 -ffreestanding -mno-red-zone -mcmodel=kernel \
          -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
          -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables \
          -fno-builtin -fno-strict-aliasing -O2 -g -Wall -Wextra -std=gnu11 \
          -Ikernel/include -MMD -MP
KLDFLAGS = -m elf_x86_64 -T linker.ld -nostdlib -no-pie -z max-page-size=0x1000

UCFLAGS = -m64 -ffreestanding -mno-mmx -mno-sse -mno-sse2 -mgeneral-regs-only \
          -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables \
          -fno-builtin -O2 -g -Wall -Wextra -std=gnu11 -Iuser -MMD -MP
ULDFLAGS = -m elf_x86_64 -T user/user.ld -nostdlib -no-pie -z max-page-size=0x1000

KOBJS = $(addprefix $(BUILD)/, kernel/boot.o kernel/limine.o kernel/main.o \
    kernel/console.o kernel/serial.o kernel/kprintf.o kernel/gdt.o \
    kernel/idt.o kernel/isr.o kernel/timer.o kernel/paging.o kernel/kheap.o \
    kernel/spinlock.o kernel/task.o kernel/swtch.o kernel/vfs.o \
    kernel/initrd.o kernel/syscall.o kernel/lib.o \
    flanterm/flanterm.o flanterm/flanterm_backends/fb.o)

UOBJS = $(addprefix $(BUILD)/, user/usys.o user/ulib.o user/init.o)

.PHONY: all run run-uefi test iso hdd clean
all: $(ISO) $(HDD)

$(BUILD) $(BUILD)/kernel $(BUILD)/user $(BUILD)/flanterm:
	mkdir -p $@

# ---- хост-утилита limine (bios-install) ----
$(LIMINE_TOOL): $(LIMINE_BIN)/limine.c $(LIMINE_BIN)/limine.h
	$(CC) -O2 -std=c99 -o $@ $(LIMINE_BIN)/limine.c

# ---- ядро ----
$(BUILD)/kernel.elf: $(KOBJS) linker.ld | $(BUILD)/kernel
	ld $(KLDFLAGS) -o $@ $(KOBJS)

$(BUILD)/kernel/%.o: kernel/%.c | $(BUILD)/kernel
	$(CC) $(KCFLAGS) -c -o $@ $<

$(BUILD)/kernel/%.o: kernel/%.S | $(BUILD)/kernel
	$(CC) $(KCFLAGS) -c -o $@ $<

# flanterm — внешние исходники (BSD-2-Clause), собираются теми же флагами
$(BUILD)/flanterm/%.o: flanterm/%.c | $(BUILD)/flanterm
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -Iflanterm -c -o $@ $<

$(BUILD)/flanterm/flanterm_backends/%.o: flanterm/flanterm_backends/%.c | $(BUILD)/flanterm
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -Iflanterm -c -o $@ $<

# ---- initrd (как раньше: init.bin + hello.txt) ----
$(BUILD)/init.elf: $(UOBJS) user/user.ld | $(BUILD)/user
	ld $(ULDFLAGS) -o $@ $(UOBJS)

$(BUILD)/user/%.o: user/%.S | $(BUILD)/user
	$(CC) $(UCFLAGS) -c -o $@ $<

$(BUILD)/user/%.o: user/%.c | $(BUILD)/user
	$(CC) $(UCFLAGS) -c -o $@ $<

$(BUILD)/init.bin: $(BUILD)/init.elf
	objcopy -O binary $< $@

$(BUILD)/hello.txt:
	printf 'Hello from initrd!\n' > $@

$(INITRD): scripts/mkinitrd.py $(BUILD)/init.bin $(BUILD)/hello.txt
	python3 scripts/mkinitrd.py $@ $(BUILD)/init.bin $(BUILD)/hello.txt

# ---- ISO: El Torito BIOS (limine-bios-cd.bin) + UEFI (limine-uefi-cd.bin) ----
$(ISO): $(KERNEL) $(INITRD) limine.conf $(LIMINE_TOOL) | $(BUILD)
	rm -rf $(BUILD)/isodir
	mkdir -p $(BUILD)/isodir/boot/limine $(BUILD)/isodir/EFI/BOOT
	cp $(KERNEL)  $(BUILD)/isodir/boot/kernel.elf
	cp $(INITRD)  $(BUILD)/isodir/boot/initrd.bin
	cp limine.conf $(BUILD)/isodir/boot/limine/limine.conf
	cp $(LIMINE_BIN)/limine-bios.sys    $(BUILD)/isodir/boot/limine/
	cp $(LIMINE_BIN)/limine-bios-cd.bin $(BUILD)/isodir/boot/limine/
	cp $(LIMINE_BIN)/limine-uefi-cd.bin $(BUILD)/isodir/boot/limine/
	cp $(LIMINE_BIN)/BOOTX64.EFI        $(BUILD)/isodir/EFI/BOOT/
	cp $(LIMINE_BIN)/BOOTIA32.EFI       $(BUILD)/isodir/EFI/BOOT/
	$(XORRISO) -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table \
	    --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(BUILD)/isodir -o $@
	$(LIMINE_TOOL) bios-install $@

# ---- HDD: GPT (ESP с FAT32 + BIOS boot) — для dd на USB-флешку ----
$(HDD): $(KERNEL) $(INITRD) limine.conf $(LIMINE_TOOL) | $(BUILD)
	python3 scripts/mkhdd.py $@ $(KERNEL) $(INITRD) limine.conf $(LIMINE_BIN)
	$(LIMINE_TOOL) bios-install $@ 2

# ---- запуск ----
run: $(ISO)
	$(QEMU) -cdrom $(ISO) -m 128 -serial stdio -no-reboot

run-uefi: $(ISO)
	$(QEMU) -cdrom $(ISO) -m 128 -serial stdio -no-reboot \
	    -drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE)

test: $(ISO)
	@timeout 90 $(QEMU) -device isa-debug-exit -cdrom $(ISO) -m 128 \
	  -display none -serial stdio -no-reboot > $(BUILD)/test.log 2>&1 || true
	@grep -q "ALL TESTS PASSED" $(BUILD)/test.log && echo "SMOKE TEST: PASS" || \
	  { echo "SMOKE TEST: FAIL"; cat $(BUILD)/test.log; exit 1; }

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d) $(wildcard $(BUILD)/*/*.d) $(wildcard $(BUILD)/*/*/*.d)
