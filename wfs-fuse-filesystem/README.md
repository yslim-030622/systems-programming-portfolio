# WFS: Userspace Filesystem with FUSE

WFS는 작은 disk image를 실제 filesystem처럼 다룰 수 있게 만든 userspace filesystem입니다. FUSE callback을 구현해서 `ls`, `mkdir`, `touch`, `cat`, `rm` 같은 일반 command가 이 filesystem 위에서 동작하도록 구성했습니다.

WFS is a small Unix-like filesystem implemented in userspace with FUSE. It stores metadata and file contents inside a disk image, maps that image into memory, and exposes filesystem behavior through FUSE operations.

## Why I Built It

파일시스템은 단순히 file read/write만 처리하는 코드가 아닙니다. inode, directory entry, bitmap, block allocation, timestamp 같은 metadata가 서로 일관되게 움직여야 합니다. 이 프로젝트는 그 내부 구조를 직접 구현해 보기 위한 작업입니다.

Filesystem work is a good systems exercise because every operation has to keep user-visible behavior and on-disk metadata in sync.

## What It Does

- Initializes a disk image with `mkfs`
- Lays out the superblock, inode bitmap, data bitmap, inode region, and data blocks
- Resolves absolute paths by walking directory entries
- Supports file and directory creation/removal
- Implements FUSE operations such as `getattr`, `readdir`, `mknod`, `mkdir`, `unlink`, `rmdir`, `read`, `write`, `truncate`, and `statfs`
- Uses direct blocks and an indirect block for file contents
- Updates access, modification, and metadata-change timestamps
- Supports a `user.color` extended attribute

## Implementation Notes

`wfs.c` maps the disk image once and uses pointer arithmetic to access filesystem structures. Allocation is bitmap-based: inode bits track active inodes, and data bits track allocated data blocks. Directories are regular inodes whose data blocks contain directory entries.

`mkfs.c` creates the initial disk layout and root inode. `wfs.h` defines the on-disk structures shared by both programs.

## Build

Install FUSE development headers first.

```sh
make
```

Some Linux distributions expose FUSE through `fuse3.pc` instead of `fuse.pc`:

```sh
make FUSE_PKG=fuse3
```

## Example

```sh
dd if=/dev/zero of=disk.img bs=1M count=1
./mkfs -d disk.img -i 32 -b 200
mkdir -p mnt
./wfs disk.img -f -s mnt
```

In another terminal:

```sh
touch mnt/hello.txt
echo "hello from wfs" > mnt/hello.txt
cat mnt/hello.txt
```

Unmount when finished:

```sh
fusermount -u mnt
```

## Files

- `wfs.c`: FUSE callbacks, allocation, inode operations, directories, file I/O, timestamps, and xattrs
- `wfs.h`: on-disk structures and constants
- `mkfs.c`: disk image formatter
