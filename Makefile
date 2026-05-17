# Build directory: all intermediate files (.o .d .asm .sym) go here
B := build

OBJS = \
	$(B)/bio.o\
	$(B)/console.o\
	$(B)/exec.o\
	$(B)/file.o\
	$(B)/fs.o\
	$(B)/ide.o\
	$(B)/ioapic.o\
	$(B)/kalloc.o\
	$(B)/kbd.o\
	$(B)/lapic.o\
	$(B)/log.o\
	$(B)/main.o\
	$(B)/mp.o\
	$(B)/picirq.o\
	$(B)/pipe.o\
	$(B)/proc.o\
	$(B)/sleeplock.o\
	$(B)/spinlock.o\
	$(B)/string.o\
	$(B)/swtch.o\
	$(B)/syscall.o\
	$(B)/sysfile.o\
	$(B)/sysproc.o\
	$(B)/trapasm.o\
	$(B)/trap.o\
	$(B)/uart.o\
	$(B)/vectors.o\
	$(B)/vm.o\

# Cross-compiling (e.g., on Mac OS X)
# TOOLPREFIX = i386-jos-elf

# Using native tools (e.g., on X86 Linux)
#TOOLPREFIX = 

# Try to infer the correct TOOLPREFIX if not set
ifndef TOOLPREFIX
TOOLPREFIX := $(shell if i386-jos-elf-objdump -i 2>&1 | grep '^elf32-i386$$' >/dev/null 2>&1; \
	then echo 'i386-jos-elf-'; \
	elif objdump -i 2>&1 | grep 'elf32-i386' >/dev/null 2>&1; \
	then echo ''; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find an i386-*-elf version of GCC/binutils." 1>&2; \
	echo "*** Is the directory with i386-jos-elf-gcc in your PATH?" 1>&2; \
	echo "*** If your i386-*-elf toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than 'i386-jos-elf-', set your TOOLPREFIX" 1>&2; \
	echo "*** environment variable to that prefix and run 'make' again." 1>&2; \
	echo "*** To turn off this error, run 'gmake TOOLPREFIX= ...'." 1>&2; \
	echo "***" 1>&2; exit 1; fi)
endif

# If the makefile can't find QEMU, specify its path here
# QEMU = qemu-system-i386

# Try to infer the correct QEMU
ifndef QEMU
QEMU = $(shell if which qemu > /dev/null; \
	then echo qemu; exit; \
	elif which qemu-system-i386 > /dev/null; \
	then echo qemu-system-i386; exit; \
	elif which qemu-system-x86_64 > /dev/null; \
	then echo qemu-system-x86_64; exit; \
	else \
	qemu=/Applications/Q.app/Contents/MacOS/i386-softmmu.app/Contents/MacOS/i386-softmmu; \
	if test -x $$qemu; then echo $$qemu; exit; fi; fi; \
	echo "***" 1>&2; \
	echo "*** Error: Couldn't find a working QEMU executable." 1>&2; \
	echo "*** Is the directory containing the qemu binary in your PATH" 1>&2; \
	echo "*** or have you tried setting the QEMU variable in Makefile?" 1>&2; \
	echo "***" 1>&2; exit 1)
endif

CC = $(TOOLPREFIX)gcc
AS = $(TOOLPREFIX)gas
LD = $(TOOLPREFIX)ld
OBJCOPY = $(TOOLPREFIX)objcopy
OBJDUMP = $(TOOLPREFIX)objdump
CFLAGS = -fno-pic -static -fno-builtin -fno-strict-aliasing -O2 -Wall -MD -ggdb -m32 -Werror -fno-omit-frame-pointer
CFLAGS += -I./include -I./kernel -I./user -I./boot
CFLAGS += $(shell $(CC) -fno-stack-protector -E -x c /dev/null >/dev/null 2>&1 && echo -fno-stack-protector)
ASFLAGS = -m32 -gdwarf-2 -Wa,-divide -I./include -I./kernel -I./user -I./boot

# Search subdirectories for source files
vpath %.c kernel user boot
vpath %.S kernel user boot
# FreeBSD ld wants ``elf_i386_fbsd''
LDFLAGS += -m $(shell $(LD) -V | grep elf_i386 2>/dev/null | head -n 1)

# Disable PIE when possible (for Ubuntu 16.10 toolchain)
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]no-pie'),)
CFLAGS += -fno-pie -no-pie
endif
ifneq ($(shell $(CC) -dumpspecs 2>/dev/null | grep -e '[^f]nopie'),)
CFLAGS += -fno-pie -nopie
endif

# Default target
all: xv6.img fs.img

# Create build directory on demand
$(B):
	@mkdir -p $(B)

# Compile C sources into build/
$(B)/%.o: %.c | $(B)
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile assembly sources (in source root) into build/
$(B)/%.o: %.S | $(B)
	$(CC) $(ASFLAGS) -c -o $@ $<

# Compile generated assembly files (in build/) into build/
$(B)/%.o: $(B)/%.S | $(B)
	$(CC) $(ASFLAGS) -c -o $@ $<

xv6.img: $(B)/bootblock kernel
	dd if=/dev/zero of=xv6.img count=10000
	dd if=$(B)/bootblock of=xv6.img conv=notrunc
	dd if=$(B)/xv6kernel of=xv6.img seek=1 conv=notrunc

xv6memfs.img: $(B)/bootblock kernelmemfs
	dd if=/dev/zero of=xv6memfs.img count=10000
	dd if=$(B)/bootblock of=xv6memfs.img conv=notrunc
	dd if=$(B)/kernelmemfs of=xv6memfs.img seek=1 conv=notrunc

$(B)/bootblock: boot/bootasm.S boot/bootmain.c | $(B)
	$(CC) $(CFLAGS) -fno-pic -O -nostdinc -I./include -I./kernel -I./boot -c -o $(B)/bootmain.o boot/bootmain.c
	$(CC) $(CFLAGS) -fno-pic -nostdinc -I./include -I./kernel -I./boot -c -o $(B)/bootasm.o boot/bootasm.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x7C00 -o $(B)/bootblock.o $(B)/bootasm.o $(B)/bootmain.o
	$(OBJDUMP) -S $(B)/bootblock.o > $(B)/bootblock.asm
	$(OBJCOPY) -S -O binary -j .text $(B)/bootblock.o $(B)/bootblock
	tools/sign.pl $(B)/bootblock

entryother: kernel/entryother.S | $(B)
	$(CC) $(CFLAGS) -fno-pic -nostdinc -I./include -I./kernel -I./boot -c -o $(B)/entryother.o kernel/entryother.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0x7000 -o $(B)/bootblockother.o $(B)/entryother.o
	$(OBJCOPY) -S -O binary -j .text $(B)/bootblockother.o entryother
	$(OBJDUMP) -S $(B)/bootblockother.o > $(B)/entryother.asm

initcode: kernel/initcode.S | $(B)
	$(CC) $(CFLAGS) -nostdinc -I./include -I./kernel -I./boot -c -o $(B)/initcode.o kernel/initcode.S
	$(LD) $(LDFLAGS) -N -e start -Ttext 0 -o $(B)/initcode.out $(B)/initcode.o
	$(OBJCOPY) -S -O binary $(B)/initcode.out initcode
	$(OBJDUMP) -S $(B)/initcode.o > $(B)/initcode.asm

kernel: $(OBJS) $(B)/entry.o entryother initcode boot/kernel.ld
	$(LD) $(LDFLAGS) -T boot/kernel.ld -o $(B)/xv6kernel $(B)/entry.o $(OBJS) -b binary initcode entryother
	$(OBJDUMP) -S $(B)/xv6kernel > $(B)/kernel.asm
	$(OBJDUMP) -t $(B)/xv6kernel | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(B)/kernel.sym

# kernelmemfs is a copy of kernel that maintains the
# disk image in memory instead of writing to a disk.
# This is not so useful for testing persistent storage or
# exploring disk buffering implementations, but it is
# great for testing the kernel on real hardware without
# needing a scratch disk.
MEMFSOBJS = $(filter-out $(B)/ide.o,$(OBJS)) $(B)/memide.o
kernelmemfs: $(MEMFSOBJS) $(B)/entry.o entryother initcode boot/kernel.ld fs.img
	$(LD) $(LDFLAGS) -T boot/kernel.ld -o $(B)/kernelmemfs $(B)/entry.o  $(MEMFSOBJS) -b binary initcode entryother fs.img
	$(OBJDUMP) -S $(B)/kernelmemfs > $(B)/kernelmemfs.asm
	$(OBJDUMP) -t $(B)/kernelmemfs | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(B)/kernelmemfs.sym

tags: $(OBJS) kernel/entryother.S _init
	etags kernel/*.S kernel/*.c user/*.S user/*.c boot/*.S boot/*.c

$(B)/vectors.S: tools/vectors.pl | $(B)
	tools/vectors.pl > $(B)/vectors.S

ULIB = $(B)/ulib.o $(B)/usys.o $(B)/printf.o $(B)/umalloc.o

_%: $(B)/%.o $(ULIB)
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o $@ $^
	$(OBJDUMP) -S $@ > $(B)/$*.asm
	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > $(B)/$*.sym

_forktest: $(B)/forktest.o $(ULIB)
	# forktest has less library code linked in - needs to be small
	# in order to be able to max out the proc table.
	$(LD) $(LDFLAGS) -N -e main -Ttext 0 -o _forktest $(B)/forktest.o $(B)/ulib.o $(B)/usys.o
	$(OBJDUMP) -S _forktest > $(B)/forktest.asm

$(B)/mkfs: tools/mkfs.c | $(B)
	gcc -Werror -Wall -iquote ./include -iquote ./kernel -o $(B)/mkfs tools/mkfs.c

# Prevent deletion of intermediate files, e.g. cat.o, after first build, so
# that disk image changes after first build are persistent until clean.  More
# details:
# http://www.gnu.org/software/make/manual/html_node/Chained-Rules.html
.PRECIOUS: $(B)/%.o

UPROGS=\
	_cat\
	_echo\
	_forktest\
	_grep\
	_init\
	_kill\
	_ln\
	_ls\
	_mkdir\
	_rm\
	_sh\
	_stressfs\
	_usertests\
	_wc\
	_zombie\

fs.img: $(B)/mkfs docs/README $(UPROGS)
	$(B)/mkfs fs.img docs/README $(UPROGS)

-include $(B)/*.d

clean:
	rm -f *.tex *.dvi *.idx *.aux *.log *.ind *.ilg \n	xv6.img fs.img \n	xv6memfs.img .gdbinit \n	$(UPROGS)
	rm -f *.o *.d *.asm *.sym entryother initcode
	rm -rf $(B)/

# make a printout
FILES = $(shell grep -v '^\#' tools/runoff.list)
PRINT = tools/runoff.list tools/runoff.spec docs/README tools/toc.hdr tools/toc.ftr $(FILES)

xv6.pdf: $(PRINT)
	cd tools && ./runoff
	ls -l xv6.pdf

print: xv6.pdf

# run in emulators

bochs : fs.img xv6.img
	if [ ! -e .bochsrc ]; then ln -s dot-bochsrc .bochsrc; fi
	bochs -q

# try to generate a unique GDB port
GDBPORT = $(shell expr `id -u` % 5000 + 25000)
# QEMU's gdb stub command line changed in 0.11
QEMUGDB = $(shell if $(QEMU) -help | grep -q '^-gdb'; \
	then echo "-gdb tcp::$(GDBPORT)"; \
	else echo "-s -p $(GDBPORT)"; fi)
ifndef CPUS
CPUS := 2
endif
QEMUOPTS = -drive file=fs.img,index=1,media=disk,format=raw -drive file=xv6.img,index=0,media=disk,format=raw -smp $(CPUS) -m 512 $(QEMUEXTRA)

qemu: fs.img xv6.img
	$(QEMU) -serial mon:stdio $(QEMUOPTS)

qemu-memfs: xv6memfs.img
	$(QEMU) -drive file=xv6memfs.img,index=0,media=disk,format=raw -smp $(CPUS) -m 256

qemu-nox: fs.img xv6.img
	$(QEMU) -nographic $(QEMUOPTS)

.gdbinit: .gdbinit.tmpl
	sed "s/localhost:1234/localhost:$(GDBPORT)/" < $^ > $@

qemu-gdb: fs.img xv6.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -serial mon:stdio $(QEMUOPTS) -S $(QEMUGDB)

qemu-nox-gdb: fs.img xv6.img .gdbinit
	@echo "*** Now run 'gdb'." 1>&2
	$(QEMU) -nographic $(QEMUOPTS) -S $(QEMUGDB)

# CUT HERE
# prepare dist for students
# after running make dist, probably want to
# rename it to rev0 or rev1 or so on and then
# check in that version.

EXTRA=\
	tools/mkfs.c user/ulib.c user/user.h user/cat.c user/echo.c user/forktest.c user/grep.c user/kill.c\
	user/ln.c user/ls.c user/mkdir.c user/rm.c user/stressfs.c user/usertests.c user/wc.c user/zombie.c\
	user/printf.c user/umalloc.c\
	docs/README dot-bochsrc tools/*.pl tools/toc.* tools/runoff tools/runoff1 tools/runoff.list\
	.gdbinit.tmpl tools/gdbutil

dist:
	rm -rf dist
	mkdir dist
	for i in $(FILES); \
	do \
		grep -v PAGEBREAK $$i >dist/$$i; \
	done
	sed '/CUT HERE/,$$d' Makefile >dist/Makefile
	echo >dist/runoff.spec
	cp $(EXTRA) dist

dist-test:
	rm -rf dist
	make dist
	rm -rf dist-test
	mkdir dist-test
	cp dist/* dist-test
	cd dist-test; $(MAKE) print
	cd dist-test; $(MAKE) bochs || true
	cd dist-test; $(MAKE) qemu

# update this rule (change rev#) when it is time to
# make a new revision.
tar:
	rm -rf /tmp/xv6
	mkdir -p /tmp/xv6
	cp dist/* dist/.gdbinit.tmpl /tmp/xv6
	(cd /tmp; tar cf - xv6) | gzip >xv6-rev10.tar.gz  # the next one will be 10 (9/17)

.PHONY: dist-test dist
# ============================================================
# lab-boot-04-multiboot: GRUB Multiboot 启动目标
# 由 lab-Tests/lab-boot-04-multiboot/Makefile 追加到 xv6 根 Makefile
# ============================================================
# lab-boot-04-multiboot: GRUB targets marker (DO NOT REMOVE)

# B := build is already defined at the top of this Makefile

# ---------------------------------------------------------------
# qemu-multiboot-nox: 用 QEMU -kernel 直接测试 Multiboot 协议
#   QEMU 会设 EAX=0x2BADB002, EBX=multiboot_info，无需真正的 GRUB
#   xv6 的 IDE 仍从 xv6.img(disk0) + fs.img(disk1) 正常工作
# ---------------------------------------------------------------
qemu-multiboot-nox: fs.img xv6.img kernel
	$(QEMU) -nographic \
	  -drive file=xv6.img,index=0,media=disk,format=raw \
	  -drive file=fs.img,index=1,media=disk,format=raw \
	  -kernel $(B)/xv6kernel \
	  -smp $(CPUS) -m 512

qemu-multiboot: fs.img xv6.img kernel
	$(QEMU) -serial mon:stdio \
	  -drive file=xv6.img,index=0,media=disk,format=raw \
	  -drive file=fs.img,index=1,media=disk,format=raw \
	  -kernel $(B)/xv6kernel \
	  -smp $(CPUS) -m 512

# ---------------------------------------------------------------
# xv6.iso: 用 grub-mkrescue 构建可启动 GRUB ISO
#   依赖: grub-common (提供 grub-mkrescue), xorriso (ISO 生成)
#   安装: sudo apt-get install grub-common xorriso
# ---------------------------------------------------------------
$(B)/grub_iso/boot/grub/grub.cfg: | $(B)
	@mkdir -p $(B)/grub_iso/boot/grub
	@printf 'set timeout=5\nset default=0\n\nmenuentry "xv6" {\n\tmultiboot /boot/kernel\n\tboot\n}\n' \
	  > $(B)/grub_iso/boot/grub/grub.cfg

$(B)/xv6.iso: kernel $(B)/grub_iso/boot/grub/grub.cfg
	@cp $(B)/xv6kernel $(B)/grub_iso/boot/kernel
	grub-mkrescue -o $(B)/xv6.iso $(B)/grub_iso 2>/dev/null
	@echo "Built: $(B)/xv6.iso (boot with: make grub-qemu)"

xv6.iso: $(B)/xv6.iso

# ---------------------------------------------------------------
# grub-qemu: 使用 GRUB ISO 启动 xv6
#   从 CDROM 引导 GRUB → 加载 kernel → xv6 检测 Multiboot 并打印信息
#   fs.img 挂载为 disk1 供 IDE 驱动使用（文件系统正常工作）
# ---------------------------------------------------------------
grub-qemu: $(B)/xv6.iso fs.img xv6.img
	$(QEMU) -nographic \
	  -drive file=xv6.img,index=0,media=disk,format=raw \
	  -drive file=fs.img,index=1,media=disk,format=raw \
	  -cdrom $(B)/xv6.iso -boot d \
	  -smp $(CPUS) -m 512
