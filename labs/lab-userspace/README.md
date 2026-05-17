# Lab: Userspace Management (User Identity and File Permissions)

[中文](i18n/zh-CN/README.md)

## Goal

Add user identity (uid/gid) and Unix-style file permissions to xv6, implementing permission isolation between kernel and application layers.

## Lab Tasks

1. Add uid/gid fields to the process control block (proc.h)
2. Add mode/uid/gid to disk inode (fs.h) and in-memory inode (file.h)
3. Implement three-level permission checking: owner -> group -> other (fs.c: check_permission)
4. Add system calls: getuid, getgid, setuid, setgid, chmod, chown
5. Add permission checking in sys_open
6. Write a user-space test program (user/uidtest.c)

## OS Concepts

- Process Credentials
- Permission Bits (rwxrwxrwx)
- System Call Interface
- Disk inode vs. in-memory inode
- Root privilege and permission bypass

## Verification

Run the `uidtest` program. Expected output:

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

## Key Code Paths

- Process identity initialization: src/proc.c:userinit() -> uid=0, gid=0
- fork inherits identity: src/proc.c:fork() -> np->uid = curproc->uid
- Permission check: src/fs.c:check_permission()
- Set ownership on file creation: src/sysfile.c:create() -> ip->uid = myproc()->uid

## Advanced Challenges

- [ ] Add execute permission check in exec()
- [ ] Implement directory search permission (chdir checks x bit)
- [ ] Add umask system call
- [ ] Implement user-space user database (/etc/passwd)
