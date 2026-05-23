# WFS: Userspace Filesystem with FUSE

WFS is a small Unix-like filesystem implemented in userspace with FUSE. It stores metadata and file contents in a disk image, maps that image into memory, and implements the filesystem callbacks that let normal commands such as `ls`, `mkdir`, `touch`, `cat`, and `rm` operate on it.

## What It Implements

- Disk image initialization with `mkfs`
- Superblock layout and metadata offsets
- Inode and data-block bitmaps
- Root directory initialization
- Path resolution through directory entries
- File and directory creation
- `read`, `write`, `mknod`, `mkdir`, `unlink`, `rmdir`, `readdir`, `getattr`, and `truncate` behavior
- Direct block pointers plus an indirect block for larger files
- Timestamp updates for access, modification, and metadata changes
- `statfs` reporting
- `user.color` extended attributes for colored directory listings

## Build

Install FUSE development headers first if your environment does not already have them.

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

On macOS with macFUSE, the unmount command may be different.

## Files

- `wfs.c` - FUSE callbacks, block allocation, inode operations, path resolution, file I/O, directories, timestamps, and xattrs
- `wfs.h` - on-disk structures and constants
- `mkfs.c` - disk image formatter
