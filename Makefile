# ============================================================================
# molloy-xv6-synth — синтез ядра: структура туториалов Дж. Моллоя + механизмы xv6
# Сборка: make        Запуск: make run       Smoke-тест: make test
# ============================================================================

# Локально распакованные инструменты (без root); при наличии системных можно убрать.
TOOLROOT ?= /home/z/my-project/tools/root
QLIB     = $(TOOLROOT)/usr/lib/x86_64-linux-gnu:$(TOOLROOT)/lib/x86_64-linux-gnu
export LD_LIBRARY_PATH := $(QLIB)
QEMU          = qemu-system-x86_64 #$(TOOLROOT)/usr/bin/qemu-system-x86_64 -L $(TOOLROOT)/usr/share/seabios -L $(TOOLROOT)/usr/share/qemu -cpu max -nic none
# pkgdatadir нужен старым (shell) grub-mkrescue; -d обязателен для новых
# (ELF, GRUB>=2.12): без него молча собирается ISO без BIOS-загрузчика.
GRUB_MKRESCUE = grub-mkrescue

#pkgdatadir=$(TOOLROOT)/usr/lib/grub PATH=$(TOOLROOT)/usr/bin:$$PATH \
                $(TOOLROOT)/usr/bin/grub-mkrescue -d $(TOOLROOT)/usr/lib/grub/i386-pc

BUILD   = build
KERNEL  = $(BUILD)/kernel.elf
INITRD  = $(BUILD)/initrd.bin
ISO     = $(BUILD)/os.iso

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

KOBJS = $(addprefix $(BUILD)/, kernel/boot.o kernel/main.o kernel/console.o \
    kernel/serial.o kernel/kprintf.o kernel/gdt.o kernel/idt.o kernel/isr.o \
    kernel/timer.o kernel/paging.o kernel/kheap.o kernel/spinlock.o \
    kernel/task.o kernel/swtch.o kernel/vfs.o kernel/initrd.o kernel/syscall.o \
    kernel/lib.o)

UOBJS = $(addprefix $(BUILD)/, user/usys.o user/ulib.o user/init.o)

.PHONY: all run test clean
all: $(ISO)

$(BUILD) $(BUILD)/kernel $(BUILD)/user:
	mkdir -p $@

$(BUILD)/kernel.elf: $(KOBJS) linker.ld | $(BUILD)/kernel
	ld $(KLDFLAGS) -o $@ $(KOBJS)

$(BUILD)/kernel/%.o: kernel/%.c | $(BUILD)/kernel
	$(CC) $(KCFLAGS) -c -o $@ $<

$(BUILD)/kernel/%.o: kernel/%.S | $(BUILD)/kernel
	$(CC) $(KCFLAGS) -c -o $@ $<

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

$(ISO): $(KERNEL) $(INITRD) grub.cfg | $(BUILD)/kernel
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(KERNEL) $(BUILD)/isodir/boot/kernel.elf
	cp $(INITRD) $(BUILD)/isodir/boot/initrd.bin
	cp grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(BUILD)/isodir

run: $(ISO)
	$(QEMU) -cdrom $(ISO) -m 128 -serial stdio -no-reboot

test: $(ISO)
	@timeout 90 $(QEMU) -device isa-debug-exit -cdrom $(ISO) -m 128 \
	  -display none -serial stdio -no-reboot > $(BUILD)/test.log 2>&1 || true
	@grep -q "ALL TESTS PASSED" $(BUILD)/test.log && echo "SMOKE TEST: PASS" || \
	  { echo "SMOKE TEST: FAIL"; cat $(BUILD)/test.log; exit 1; }

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
