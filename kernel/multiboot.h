// multiboot.h — Multiboot 1 规范数据结构
// 参考: https://www.gnu.org/software/grub/manual/multiboot/multiboot.html
//
// GRUB 在跳入内核前向内核传递两个值:
//   EAX = MULTIBOOT_MAGIC (0x2BADB002): 标识 Multiboot 兼容 bootloader
//   EBX = multiboot_info 结构的物理地址

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

// GRUB 跳入内核时 EAX 中的魔数
#define MULTIBOOT_MAGIC         0x2BADB002

// Multiboot header 中的魔数（嵌入内核 ELF 的前 8KB）
#define MULTIBOOT_HDR_MAGIC     0x1BADB002

// Multiboot header flags:
//   bit 0 (0x01): 请求内存大小信息 (mem_lower/mem_upper)
//   bit 2 (0x04): 请求引导设备信息 (boot_device)
#define MULTIBOOT_HDR_FLAGS     (0x01 | 0x04)

// multiboot_info.flags 各位掩码
#define MBI_MEM_INFO    (1 << 0)    // mem_lower / mem_upper 有效
#define MBI_BOOT_DEV    (1 << 1)    // boot_device 有效
#define MBI_CMDLINE     (1 << 2)    // cmdline 有效
#define MBI_MODS        (1 << 3)    // modules 信息有效
// bits 4-5: a.out 或 ELF 符号表信息
#define MBI_MMAP        (1 << 6)    // mmap_addr / mmap_length 有效
#define MBI_DRIVES      (1 << 7)    // drives 有效
#define MBI_CONFIG      (1 << 8)    // config_table 有效
#define MBI_LOADER      (1 << 9)    // boot_loader_name 有效
#define MBI_APM         (1 << 10)   // APM table 有效

// multiboot_mmap_entry.type 值
#define MULTIBOOT_MMAP_AVAILABLE    1   // 可用 RAM
#define MULTIBOOT_MMAP_RESERVED     2   // 保留区域

// 内存映射条目 (来自 GRUB 的 E820 等价信息)
// 注意: size 字段不计入自身 (迭代时需 +sizeof(size) 跨过它)
struct multiboot_mmap_entry {
    unsigned int size;      // 此条目剩余字节数 (不含 size 本身，通常 = 20)
    unsigned int addr_lo;   // 区域基地址低 32 位
    unsigned int addr_hi;   // 区域基地址高 32 位
    unsigned int len_lo;    // 区域长度低 32 位
    unsigned int len_hi;    // 区域长度高 32 位
    unsigned int type;      // 1=可用RAM, 2=保留
} __attribute__((packed));

// GRUB 传递给内核的完整信息包 (物理地址由 EBX 指向)
struct multiboot_info {
    unsigned int flags;             // 有效字段位图
    unsigned int mem_lower;         // [MBI_MEM_INFO] 低端内存 KB (< 1MB)
    unsigned int mem_upper;         // [MBI_MEM_INFO] 高端内存 KB (> 1MB)
    unsigned int boot_device;       // [MBI_BOOT_DEV] 启动设备
    unsigned int cmdline;           // [MBI_CMDLINE] 命令行字符串的物理地址
    unsigned int mods_count;        // [MBI_MODS] 模块数量
    unsigned int mods_addr;         // [MBI_MODS] 模块列表物理地址
    unsigned int syms[4];           // ELF 或 a.out 符号信息（不常用）
    unsigned int mmap_length;       // [MBI_MMAP] 内存映射缓冲区字节数
    unsigned int mmap_addr;         // [MBI_MMAP] 内存映射缓冲区物理地址
    unsigned int drives_length;     // [MBI_DRIVES] drives 缓冲区字节数
    unsigned int drives_addr;       // [MBI_DRIVES] drives 缓冲区物理地址
    unsigned int config_table;      // [MBI_CONFIG] BIOS 配置表物理地址
    unsigned int boot_loader_name;  // [MBI_LOADER] bootloader 名称字符串物理地址
    unsigned int apm_table;         // [MBI_APM] APM 表物理地址
} __attribute__((packed));

#endif /* MULTIBOOT_H */
