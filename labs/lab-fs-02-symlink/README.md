# Lab: Symbolic Links (Symlink Implementation)

[中文](i18n/zh-CN/README.md)

Difficulty: ★★★☆☆

## Motivation

Unix file systems have two types of links:

- **Hard links** (already in xv6): Two directory entries point to the same inode, implemented via the `link` system call, tracked by the inode reference count (nlink)
- **Symbolic links (soft links)**: A special file whose content is the target path string. When accessing a symlink, the kernel transparently follows it to the target path

Symbolic links embody the "path as name" philosophy of file systems — `/usr/bin/python` can be an alias for `/usr/bin/python3.11`. Upgrading only requires changing the symlink, and all scripts are unaffected.

This lab implements the `symlink(target, path)` system call and the symlink following logic during path resolution.

Core question: *"Why are `cd /usr/bin/../lib` and `cd /lib` the same — where does the kernel expand this path? Where are symlinks expanded?"*

## Prerequisites

- **`namei`/`namex`**: Path resolution functions in `src/fs.c` that convert a string path to an inode pointer. The entry point for all file operations
- **`dirlookup`**: Looks up a name within a directory inode, returns the corresponding inode
- **inode types**: `T_DIR`=directory, `T_FILE`=regular file, `T_DEV`=device file. Symlinks require a new type `T_SYMLINK`
- **Circular links**: `a → b → a`. Following symlinks must detect and terminate cycles, otherwise the kernel recurses infinitely

```
Path resolution example (namex following symlinks):
open("/usr/bin/python")
  namex → found "python" → inode type T_SYMLINK
  read content → "/usr/bin/python3"
  re-invoke namex("/usr/bin/python3") → T_FILE → return
```

## Lab Tasks

### 1. Add symlink inode type (include/stat.h)

```c
#define T_DIR     1    // Directory
#define T_FILE    2    // File
#define T_DEV     3    // Device
#define T_SYMLINK 4    // Symbolic link (new)
```

### 2. Implement sys_symlink (src/sysfile.c)

```c
int sys_symlink(void)
```

Parameters: `target` (target path string), `path` (symlink path)

Implementation steps:
1. Read the two string arguments using `argstr`
2. `begin_op()` to start a transaction
3. Call `create(path, T_SYMLINK, 0, 0)` to create an inode (type T_SYMLINK)
4. Call `writei(ip, target, 0, strlen(target))` to write the target path into the inode data area
5. `iunlockput(ip)` + `end_op()`
6. Properly roll back on failure

**Key constraint**: The symlink inode's data area stores the target path string, with a maximum length of `MAXPATH` (128 bytes)

### 3. Modify open to support symlink following (src/sysfile.c)

In `sys_open`, after obtaining the inode, check whether it is a symlink:

```c
#define MAX_SYMLINK_DEPTH  10   // Prevent circular links

// In sys_open after namei:
for(int depth = 0; depth < MAX_SYMLINK_DEPTH; depth++) {
    if(ip->type != T_SYMLINK) break;
    if(omode & O_NOFOLLOW) break;   // No-follow flag
    // Read target path
    char target[MAXPATH];
    int n = readi(ip, target, 0, MAXPATH - 1);
    target[n] = '\0';
    iunlockput(ip);
    // Re-resolve target path
    ip = namei(target);
    if(ip == 0) return -1;
    ilock(ip);
}
if(depth >= MAX_SYMLINK_DEPTH) {
    // Circular link, return error
    iunlockput(ip);
    return -1;
}
```

### 4. Add O_NOFOLLOW flag (include/fcntl.h)

```c
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400
#define O_NOFOLLOW 0x800   // New: do not follow symlinks
```

`O_NOFOLLOW` allows programs to open the symlink file itself (rather than the target), used to implement `readlink`, `lstat`, etc.

### 5. Implement sys_readlink (extension, optional) (src/sysfile.c)

```c
int sys_readlink(void)
// Parameters: path, buf, bufsize
// Read symlink target path into buf, without following the link
```

### 6. Register system calls (src/syscall.c, include/syscall.h)

Add `SYS_symlink` (and optionally `SYS_readlink`) to the system call table.

### 7. Write tests (user/symlinktest.c)

```
Test 1: symlink("target", "link") then open("link") reads target's content
Test 2: Multi-level symlinks (a→b→c→file)
Test 3: Circular symlinks (a→b→a) → open returns -1
Test 4: O_NOFOLLOW opens the symlink itself (readlink)
Test 5: Symlink pointing to a directory — ls and cd behavior
Test 6: Deleting a symlink (unlink) does not affect the target file
```

## OS Concepts

| Concept | How it appears in this lab |
|---------|---------------------------|
| Symlinks vs. hard links | Symlinks are special files (path indirection); hard links are multiple directory entries for the same inode |
| Path resolution (namei) | namex must recursively resolve target paths when encountering T_SYMLINK |
| inode type | T_SYMLINK is the fourth inode type; its data area stores the target path |
| Cycle detection | Depth limit (MAX_SYMLINK_DEPTH) prevents infinite following |
| Log transactions | create + writei must be within begin_op/end_op for atomicity guarantees |
| POSIX compatibility | O_NOFOLLOW, readlink are standard POSIX interfaces |

## Files to Modify

| File | Change Type | Description |
|------|------------|-------------|
| include/stat.h | Modify | Add `T_SYMLINK = 4` |
| include/fcntl.h | Modify | Add `O_NOFOLLOW` flag |
| src/sysfile.c | Modify | Implement `sys_symlink`, refactor `sys_open` following logic |
| include/syscall.h | Modify | Add `SYS_symlink` number |
| src/syscall.c | Modify | Register `sys_symlink` |
| include/user.h | Modify | Add `symlink` user-space declaration |
| user/usys.S | Modify | Add system call assembly entry |
| user/symlinktest.c | New | Symlink functionality test |

## Verification

```bash
make clean && make qemu-nox
$ symlinktest
$ usertests
```

### Verification Goals

| Goal | Expected Behavior | How to Observe |
|------|------------------|----------------|
| Basic symlink | open follows symlink to read target content | symlinktest |
| Multi-level following | Chain depth < MAX follows normally | symlinktest |
| Cycle detection | Circular symlink open returns -1 | symlinktest |
| O_NOFOLLOW | Opens the symlink itself, does not follow | symlinktest |
| Delete symlink | Target file unaffected | symlinktest |
| usertests passes | Existing features like hard links unaffected | usertests |

### Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| open on symlink returns -1 | namei returned a T_SYMLINK inode but no following was performed | Check the position of following logic in sys_open |
| Circular link kernel crash | Depth check logic is outside the loop | Confirm `depth >= MAX_SYMLINK_DEPTH` is checked correctly |
| inode lock leak | Did not `iunlockput` old inode when following symlink | Must release current inode before each re-resolution |
| Target path truncation | writei byte count does not include '\0' | After readi/open read, manually add `target[n] = '\0'` |

## Key Code Paths

- Creating a symlink: `sys_symlink()` → `create(T_SYMLINK)` → `writei(target)`
- Following a symlink: `sys_open()` → `namei()` → T_SYMLINK → `readi(target)` → recursive `namei(target)`
- Cycle detection: `for(depth < MAX_SYMLINK_DEPTH)` counter, return -1 on overflow

## Design Trade-offs

| Aspect | Hard Links | Symbolic Links |
|--------|-----------|----------------|
| Storage | Extra directory entry (shared inode) | Separate inode + target path data |
| Cross-file system | Not supported (same device) | Supported |
| Target deletion | No effect (nlink protects) | Dangling link |
| Implementation complexity | Already exists (`link` system call) | Requires path resolution changes |
| Directory links | Usually prohibited (prevents cycles) | Supported (can link to directories) |

## Advanced Challenges

- [ ] Implement `lstat`: `stat` on the symlink itself (no follow), filling in `T_SYMLINK` type
- [ ] Implement `readlink(path, buf, size)`: Read symlink target path
- [ ] Modify the `ls` command: display `link -> target` for symlinks
- [ ] Research **relative path symlinks**: base directory issue when `symlink("../lib/foo.so", "/usr/lib/foo.so")`
- [ ] Implement **`chown`/`chmod` without following symlinks**: `lchown`/`lchmod` modify the symlink's own permissions
