# WFS: Userspace Filesystem with FUSE

WFS는 작은 disk image를 FUSE로 mount해서 쓰는 userspace filesystem입니다. 파일시스템이 내부에서 metadata를 어떻게 관리하는지 직접 구현해 보기 위한 프로젝트였습니다.

WFS is a small FUSE filesystem backed by a disk image.

## What It Handles

- disk image formatting with `mkfs`
- superblock, inode bitmap, data bitmap, and inode table
- path lookup through directory entries
- file and directory create/read/write/remove operations
- direct and indirect data blocks
- timestamps, `statfs`, and a simple `user.color` xattr

## Build

```sh
make
```

If your system uses FUSE 3:

```sh
make FUSE_PKG=fuse3
```

## Quick Run

```sh
dd if=/dev/zero of=disk.img bs=1M count=1
./mkfs -d disk.img -i 32 -b 200
mkdir -p mnt
./wfs disk.img -f -s mnt
```

Then from another terminal:

```sh
touch mnt/hello.txt
echo "hello from wfs" > mnt/hello.txt
cat mnt/hello.txt
```

## Files

- `wfs.c`: filesystem logic and FUSE callbacks
- `wfs.h`: on-disk structures
- `mkfs.c`: disk formatter
