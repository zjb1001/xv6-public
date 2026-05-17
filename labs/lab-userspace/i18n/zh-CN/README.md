# Lab: Userspace Management (用户身份与文件权限)

[English](../../README.md)

## 目标

为 xv6 添加用户身份 (uid/gid) 和 Unix 风格文件权限系统，实现 kernel 与应用层的权限隔离。

## 实验内容

1. 在进程控制块 (proc.h) 中添加 uid/gid 字段
2. 在磁盘 inode (fs.h) 和内存 inode (file.h) 中添加 mode/uid/gid
3. 实现三级权限检查: owner → group → other (fs.c: check_permission)
4. 添加系统调用: getuid, getgid, setuid, setgid, chmod, chown
5. 在 sys_open 中添加权限检查
6. 编写用户态测试程序 (user/uidtest.c)

## 涉及的 OS 概念

- 进程凭证 (Process Credentials)
- 文件权限位 (Permission Bits: rwxrwxrwx)
- 系统调用接口 (System Call Interface)
- 磁盘 inode vs 内存 inode
- Root 权限与权限绕过

## 验证

运行 `uidtest` 程序，预期输出:

```
=== Userspace Management Test ===
Current uid=0 gid=0 (expected 0 0)
setuid(1) succeeded, now uid=1
setuid(0) correctly denied for non-root
Created testfile
chmod(testfile, 0444) succeeded
Write to 0444 file correctly denied
=== Test Complete ===
```

## 关键代码路径

- 进程身份初始化: src/proc.c:userinit() → uid=0, gid=0
- fork 继承身份: src/proc.c:fork() → np->uid = curproc->uid
- 权限检查: src/fs.c:check_permission()
- 文件创建时设置 ownership: src/sysfile.c:create() → ip->uid = myproc()->uid

## 进阶挑战

- [ ] 在 exec() 中添加执行权限检查
- [ ] 实现目录搜索权限 (chdir 检查 x 位)
- [ ] 添加 umask 系统调用
- [ ] 实现用户态用户数据库 (/etc/passwd)
