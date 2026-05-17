# The xv6 kernel starts executing in this file. This file is linked with
# the kernel C code, so it can refer to kernel symbols such as main().
# The boot block (bootasm.S and bootmain.c) jumps to entry below.
        
# Multiboot header, for multiboot boot loaders like GNU Grub.
# http://www.gnu.org/software/grub/manual/multiboot/multiboot.html
#
# Using GRUB 2, you can boot xv6 from a file stored in a
# Linux file system by copying kernel or kernelmemfs to /boot
# and then adding this menu entry:
#
# menuentry "xv6" {
# 	insmod ext2
# 	set root='(hd0,msdos1)'
# 	set kernel='/boot/kernel'
# 	echo "Loading ${kernel}..."
# 	multiboot ${kernel} ${kernel}
# 	boot
# }

#include "asm.h"
#include "memlayout.h"
#include "mmu.h"
#include "param.h"

# Multiboot header.  Data to direct multiboot loader.
# flags = 0x01|0x04:
#   bit 0: 请求内存大小信息 (mem_lower/mem_upper)
#   bit 2: 请求引导设备信息 (boot_device)
# checksum: -(magic + flags)，使三个 .long 之和为 0
.p2align 2
.text
.globl multiboot_header
multiboot_header:
  #define magic 0x1badb002
  #define flags (0x01|0x04)
  .long magic
  .long flags
  .long (-(magic+flags))

# By convention, the _start symbol specifies the ELF entry point.
# Since we haven't set up virtual memory yet, our entry point is
# the physical address of 'entry'.
.globl _start
_start = V2P_WO(entry)

# Entering xv6 on boot processor, with paging off.
.globl entry
entry:
  # ---------------------------------------------------------------
  # Multiboot 检测: GRUB 跳入时 EAX = 0x2BADB002, EBX = multiboot_info 物理地址
  # 必须在开分页前保存 EBX（通过物理地址写入变量）
  # ---------------------------------------------------------------
  #
  # 先清零 multiboot_info_ptr，防止 BSS 未初始化时残留脏值
  movl    $0, (V2P_WO(multiboot_info_ptr))
  cmpl    $0x2BADB002, %eax
  jne     .Lno_multiboot
  # 保存 EBX（multiboot_info 物理地址）到 multiboot_info_ptr 的物理地址
  # 分页开启后可通过虚拟地址 multiboot_info_ptr 访问
  movl    %ebx, (V2P_WO(multiboot_info_ptr))
.Lno_multiboot:

  # Turn on page size extension for 4Mbyte pages
  movl    %cr4, %eax
  orl     $(CR4_PSE), %eax
  movl    %eax, %cr4
  # Set page directory
  movl    $(V2P_WO(entrypgdir)), %eax
  movl    %eax, %cr3
  # Turn on paging.
  movl    %cr0, %eax
  orl     $(CR0_PG|CR0_WP), %eax
  movl    %eax, %cr0

  # Set up the stack pointer.
  movl $(stack + KSTACKSIZE), %esp

  # Jump to main(), and switch to executing at
  # high addresses. The indirect call is needed because
  # the assembler produces a PC-relative instruction
  # for a direct jump.
  mov $main, %eax
  jmp *%eax

.comm stack, KSTACKSIZE

# Multiboot 信息指针（物理地址，由 entry 在分页前写入）
# C 代码通过 P2V(multiboot_info_ptr) 访问 multiboot_info 结构
# 为 0 表示传统 bootloader 启动，非 0 表示 GRUB Multiboot 启动
.comm multiboot_info_ptr, 4
