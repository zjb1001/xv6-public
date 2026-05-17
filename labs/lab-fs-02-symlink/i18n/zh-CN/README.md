# Lab: Symbolic Links (软链接实现)

[English](../../README.md)

难度: ★★★☆☆

## 设计初衷

Unix 文件系统有两种链接：

- **硬链接**（xv6 已有）：两个目录项指向同一个 inode，`link` 系统调用实现，inode 引用计数（nlink）追踪
- **软链接（符号链接）**：一种特殊文件，其内容是目标路径字符串。访问软链接时，内核会透明地跳转到目标路径

软链接是文件系统中"路径即名字"哲学的体现——`/usr/bin/python` 可以是 `/usr/bin/python3.11` 的别名，升级只需修改软链接，所有脚本无感知。

本实验实现 `symlink(target, path)` 系统调用和路径解析时的软链接跟随逻辑。

核心问题：*"cd /usr/bin/../lib 和 cd /lib 是一样的——内核在哪里展开这个路径？软链接在哪里展开？"*

## 前置知识

- **`namei`/`namex`**: `src/fs.c` 中的路径解析函数，将字符串路径转换为 inode 指针。是所有文件操作的入口
- **`dirlookup`**: 在目录 inode 中查找某个名字，返回对应 inode
- **inode 类型**: `T_DIR`=目录，`T_FILE`=普通文件，`T_DEV`=设备文件。软链接需要新增 `T_SYMLINK`
- **循环链接**: `a → b → a`，跟随软链接时必须检测并终止，否则内核无限递归

```
路径解析示意（namex 跟随软链接）:
open("/usr/bin/python")
  namex → 找到 "python" → inode 类型 T_SYMLINK
  读取内容 → "/usr/bin/python3"
  重新调用 namex("/usr/bin/python3") → T_FILE → 返回
```

## 实验内容

### 1. 添加软链接 inode 类型 (include/stat.h)

```c
#define T_DIR     1    // Directory
#define T_FILE    2    // File
#define T_DEV     3    // Device
#define T_SYMLINK 4    // Symbolic link（新增）
```

### 2. 实现 sys_symlink (src/sysfile.c)

```c
int sys_symlink(void)
```

参数: `target`（目标路径字符串），`path`（软链接的路径）

实现步骤：
1. 用 `argstr` 读取两个字符串参数
2. `begin_op()` 开始事务
3. 调用 `create(path, T_SYMLINK, 0, 0)` 创建 inode（类型为 T_SYMLINK）
4. 调用 `writei(ip, target, 0, strlen(target))` 将目标路径写入 inode 数据区
5. `iunlockput(ip)` + `end_op()`
6. 失败时正确回滚

**关键约束**: 软链接 inode 的数据区存目标路径字符串，最长为 `MAXPATH`（128 字节）

### 3. 修改 open 支持跟随软链接 (src/sysfile.c)

在 `sys_open` 中，得到 inode 后检查是否为软链接：

```c
#define MAX_SYMLINK_DEPTH  10   // 防止循环链接

// 在 sys_open 中 namei 之后：
for(int depth = 0; depth < MAX_SYMLINK_DEPTH; depth++) {
    if(ip->type != T_SYMLINK) break;
    if(omode & O_NOFOLLOW) break;   // 不跟随标志
    // 读取目标路径
    char target[MAXPATH];
    int n = readi(ip, target, 0, MAXPATH - 1);
    target[n] = '\0';
    iunlockput(ip);
    // 重新解析目标路径
    ip = namei(target);
    if(ip == 0) return -1;
    ilock(ip);
}
if(depth >= MAX_SYMLINK_DEPTH) {
    // 循环链接，返回错误
    iunlockput(ip);
    return -1;
}
```

### 4. 添加 O_NOFOLLOW 标志 (include/fcntl.h)

```c
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800   // 新增：不跟随软链接
```

`O_NOFOLLOW` 允许程序打开软链接文件本身（而非目标），用于实现 `readlink`、`lstat` 等。

### 5. 实现 sys_readlink（扩展，可选） (src/sysfile.c)

```c
int sys_readlink(void)
// 参数: path, buf, bufsize
// 读取软链接目标路径到 buf，不跟随链接
```

### 6. 注册系统调用 (src/syscall.c, include/syscall.h)

在系统调用表中添加 `SYS_symlink`（和可选的 `SYS_readlink`）。

### 7. 编写测试 (user/symlinktest.c)

```
测试 1: symlink("target", "link") 后 open("link") 能读到 target 的内容
测试 2: 多级软链接（a→b→c→文件）
测试 3: 循环软链接（a→b→a）→ open 返回 -1
测试 4: O_NOFOLLOW 打开软链接本身（readlink）
测试 5: 软链接指向目录时，ls 和 cd 行为
测试 6: 删除软链接（unlink）不影响目标文件
```

## 涉及的 OS 概念

| 概念 | 在本实验中的体现 |
|------|----------------|
| 软链接 vs 硬链接 | 软链接是特殊文件（路径间接），硬链接是 inode 的多个目录项 |
| 路径解析（namei） | namex 解析路径时遇到 T_SYMLINK 需递归解析目标路径 |
| inode 类型 | T_SYMLINK 是第四种 inode 类型，数据区存目标路径 |
| 循环检测 | 深度限制（MAX_SYMLINK_DEPTH）防止无限跟随 |
| 日志事务 | create + writei 必须在 begin_op/end_op 中，原子性保证 |
| POSIX 兼容 | O_NOFOLLOW、readlink 是标准 POSIX 接口 |

## 涉及的文件

| 文件 | 改动类型 | 说明 |
|------|---------|------|
| include/stat.h | 修改 | 添加 `T_SYMLINK = 4` |
| include/fcntl.h | 修改 | 添加 `O_NOFOLLOW` 标志 |
| src/sysfile.c | 修改 | 实现 `sys_symlink`，改造 `sys_open` 跟随逻辑 |
| include/syscall.h | 修改 | 添加 `SYS_symlink` 编号 |
| src/syscall.c | 修改 | 注册 `sys_symlink` |
| include/user.h | 修改 | 添加 `symlink` 用户态声明 |
| user/usys.S | 修改 | 添加系统调用汇编入口 |
| user/symlinktest.c | 新增 | 软链接功能测试 |

## 验证

```bash
make clean && make qemu-nox
$ symlinktest
$ usertests
```

### 验证目标

| 目标 | 预期行为 | 观察方式 |
|------|---------|---------|
| 基础软链接 | open 跟随软链接读到目标内容 | symlinktest |
| 多级跟随 | 链深度 < MAX 时正常跟随 | symlinktest |
| 循环检测 | 循环软链接 open 返回 -1 | symlinktest |
| O_NOFOLLOW | 打开软链接本身，不跟随 | symlinktest |
| 删除软链接 | 目标文件不受影响 | symlinktest |
| usertests 通过 | 硬链接等已有功能不受影响 | usertests |

### 常见问题

| 问题 | 原因 | 解决 |
|------|------|------|
| open 软链接返回 -1 | namei 返回了 T_SYMLINK inode，但未进行跟随 | 检查 sys_open 中跟随逻辑的位置 |
| 循环链接内核崩溃 | 深度检测逻辑写在循环外 | 确认 `depth >= MAX_SYMLINK_DEPTH` 正确判断 |
| inode 锁泄漏 | 跟随软链接时未 `iunlockput` 旧 inode | 每次重新解析前必须释放当前 inode |
| 目标路径截断 | writei 写入的字节数不含 '\0' | readlink/open 读取后手动补 `target[n] = '\0'` |

## 关键代码路径

- 创建软链接: `sys_symlink()` → `create(T_SYMLINK)` → `writei(target)`
- 跟随软链接: `sys_open()` → `namei()` → T_SYMLINK → `readi(target)` → 递归 `namei(target)`
- 循环检测: `for(depth < MAX_SYMLINK_DEPTH)` 计数，超限返回 -1

## 设计权衡

| 方面 | 硬链接 | 软链接 |
|------|--------|-------|
| 存储 | 额外目录项（共享 inode） | 独立 inode + 目标路径数据 |
| 跨文件系统 | 不支持（同一设备） | 支持 |
| 目标删除 | 不影响（nlink 保护） | 悬挂链接（dangling link） |
| 实现复杂度 | 已有（`link` 系统调用） | 需要路径解析改造 |
| 目录链接 | 通常禁止（防止循环） | 支持（可链接到目录） |

## 进阶挑战

- [ ] 实现 `lstat`：`stat` 软链接本身（不跟随），填写 `T_SYMLINK` 类型
- [ ] 实现 `readlink(path, buf, size)`：读取软链接目标路径
- [ ] 修改 `ls` 命令：显示软链接时输出 `link -> target`
- [ ] 研究**相对路径软链接**：`symlink("../lib/foo.so", "/usr/lib/foo.so")` 时路径解析的基目录问题
- [ ] 实现 **`chown`/`chmod` 不跟随软链接**：`lchown`/`lchmod` 修改软链接本身的权限
