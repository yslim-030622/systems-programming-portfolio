# WFS: Userspace Filesystem with FUSE

## 한국어

WFS는 작은 disk image를 FUSE로 mount해서 쓰는 userspace filesystem입니다. 파일시스템이 내부에서 metadata를 어떻게 관리하는지 직접 구현해 보기 위한 프로젝트였습니다.

### 구현 내용

- `mkfs`를 통한 disk image 초기화
- superblock, inode bitmap, data bitmap, inode table 관리
- directory entry 기반 path lookup
- file/directory 생성, 읽기, 쓰기, 삭제
- direct/indirect data block 사용
- timestamps, `statfs`, 간단한 `user.color` xattr 지원

### 빌드

```sh
make
```

FUSE 3 환경에서는:

```sh
make FUSE_PKG=fuse3
```

## English

WFS is a small FUSE filesystem backed by a disk image. It was built to practice how filesystem metadata is laid out and kept consistent.

### What It Handles

- disk image formatting with `mkfs`
- superblock, inode bitmap, data bitmap, and inode table
- path lookup through directory entries
- file and directory create/read/write/remove operations
- direct and indirect data blocks
- timestamps, `statfs`, and a simple `user.color` xattr

### Build

```sh
make
```

If your system uses FUSE 3:

```sh
make FUSE_PKG=fuse3
```
