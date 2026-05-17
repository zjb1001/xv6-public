#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "multiboot.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // first address after kernel loaded from ELF file

// multiboot_info_ptr: 由 entry.S 在分页前写入的 multiboot_info 物理地址
// 0 = 传统 bootloader 启动；非 0 = GRUB Multiboot 启动
extern uint multiboot_info_ptr;

// parse_multiboot — 解析 GRUB 传递的 Multiboot 信息并打印
//
// 必须在 kvmalloc() 之后调用（需要内核虚拟地址映射，用于 P2V 转换），
// 且在 consoleinit()/uartinit() 之后调用（确保 cprintf 输出有效）。
//
// OS 设计要点:
//   multiboot_info_ptr 是物理地址，需要 P2V() 转换后才能在 C 中解引用。
//   GRUB 在低端物理内存构造 multiboot_info 结构，xv6 页表将低端物理内存
//   恒等映射到 KERNBASE 以上的虚拟地址，所以 P2V 在此合法有效。
static void
parse_multiboot(void)
{
  struct multiboot_info *mbi;
  struct multiboot_mmap_entry *mmap;
  uint mmap_end;

  if (multiboot_info_ptr == 0) {
    cprintf("multiboot: traditional bootloader (xv6 bootblock)\n");
    return;
  }

  // P2V: 将 GRUB 提供的物理地址转为内核虚拟地址
  mbi = (struct multiboot_info *)P2V(multiboot_info_ptr);
  cprintf("multiboot: GRUB Multiboot-compliant bootloader detected\n");

  // 打印 bootloader 名称
  if (mbi->flags & MBI_LOADER) {
    cprintf("multiboot: loader = \"%s\"\n",
            (char *)P2V(mbi->boot_loader_name));
  }

  // 打印内存大小信息
  if (mbi->flags & MBI_MEM_INFO) {
    cprintf("multiboot: mem_lower = %dKB, mem_upper = %dKB (~%dMB)\n",
            mbi->mem_lower, mbi->mem_upper, mbi->mem_upper / 1024);
  }

  // 打印详细内存映射 (E820 等价)
  if (mbi->flags & MBI_MMAP) {
    cprintf("multiboot: memory map (addr/len/type):\n");
    mmap_end = mbi->mmap_addr + mbi->mmap_length;
    mmap = (struct multiboot_mmap_entry *)P2V(mbi->mmap_addr);
    while ((uint)mmap < (uint)P2V(mmap_end)) {
      cprintf("  [%x - %x] %s\n",
              mmap->addr_lo,
              mmap->addr_lo + mmap->len_lo - 1,
              mmap->type == MULTIBOOT_MMAP_AVAILABLE ? "usable" : "reserved");
      // 迭代: mmap->size 不含自身的 4 字节，故加 sizeof(mmap->size)
      mmap = (struct multiboot_mmap_entry *)
             ((char *)mmap + mmap->size + sizeof(mmap->size));
    }
  }

  // 打印启动设备
  if (mbi->flags & MBI_BOOT_DEV) {
    cprintf("multiboot: boot_device = 0x%x\n", mbi->boot_device);
  }
}

// Bootstrap processor starts running C code here.
// Allocate a real stack and switch to it, first
// doing some setup required for memory allocator to work.
int
main(void)
{
  kinit1(end, P2V(4*1024*1024)); // phys page allocator
  kvmalloc();      // kernel page table
  mpinit();        // detect other processors
  lapicinit();     // interrupt controller
  seginit();       // segment descriptors
  picinit();       // disable pic
  ioapicinit();    // another interrupt controller
  consoleinit();   // console hardware
  uartinit();      // serial port
  parse_multiboot(); // 解析 Multiboot 信息（需要 console 和虚拟地址映射）
  pinit();         // process table
  tvinit();        // trap vectors
  binit();         // buffer cache
  fileinit();      // file table
  ideinit();       // disk 
  startothers();   // start other processors
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // must come after startothers()
  userinit();      // first user process
  mpmain();        // finish this processor's setup
}

// Other CPUs jump here from entryother.S.
static void
mpenter(void)
{
  switchkvm();
  seginit();
  lapicinit();
  mpmain();
}

// Common CPU setup code.
static void
mpmain(void)
{
  cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       // load idt register
  xchg(&(mycpu()->started), 1); // tell startothers() we're up
  scheduler();     // start running processes
}

pde_t entrypgdir[];  // For entry.S

// Start the non-boot (AP) processors.
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // Write entry code to unused memory at 0x7000.
  // The linker has placed the image of entryother.S in
  // _binary_entryother_start.
  code = P2V(0x7000);
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == mycpu())  // We've started already.
      continue;

    // Tell entryother.S what stack to use, where to enter, and what
    // pgdir to use. We cannot use kpgdir yet, because the AP processor
    // is running in low  memory, so we use entrypgdir for the APs too.
    stack = kalloc();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void(**)(void))(code-8) = mpenter;
    *(int**)(code-12) = (void *) V2P(entrypgdir);

    lapicstartap(c->apicid, V2P(code));

    // wait for cpu to finish mpmain()
    while(c->started == 0)
      ;
  }
}

// The boot page table used in entry.S and entryother.S.
// Page directories (and page tables) must start on page boundaries,
// hence the __aligned__ attribute.
// PTE_PS in a page directory entry enables 4Mbyte pages.

__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // Map VA's [0, 4MB) to PA's [0, 4MB)
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};
