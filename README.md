# Systems Programming Portfolio

운영체제 수업에서 만든 C 기반 시스템 프로젝트들을 포트폴리오용으로 정리했습니다. 과제 설명서나 테스트 파일을 그대로 올리는 대신, 직접 구현한 코드와 필요한 설명만 남겼습니다.

This is a cleaned portfolio repo for several C systems projects. The projects cover shell execution, kernel scheduling, synchronization, and a small filesystem.

## Projects

### WFS: Userspace Filesystem with FUSE

`wfs-fuse-filesystem/`

작은 disk image를 FUSE로 mount해서 실제 filesystem처럼 다루는 프로젝트입니다. inode, bitmap, directory entry, block allocation을 직접 관리하고, 파일/디렉터리 생성, 읽기, 쓰기, 삭제까지 구현했습니다.

A small FUSE filesystem backed by a disk image. It handles filesystem metadata, path lookup, block allocation, file I/O, directories, timestamps, and a simple extended attribute.

### Preemptive STCF Scheduler for xv6

`xv6-stcf-scheduler/`

xv6의 round-robin scheduler를 STCF(shortest-time-to-completion-first) 방식으로 바꾼 커널 프로젝트입니다. process의 남은 실행 시간을 추적하고, `fork`, `yield`, `sleep`, `exit` 같은 흐름에서 scheduling state가 자연스럽게 이어지도록 수정했습니다.

A modified xv6 kernel with a preemptive STCF scheduler. It adds runtime accounting, scheduling metadata, and a few small syscalls so user programs can provide runtime hints.

### Unix Shell in C

`unix-shell/`

C로 만든 작은 Unix-style shell입니다. command parsing, built-in command, alias, history, external command execution, pipeline을 구현했습니다.

A compact shell written in C. It uses Unix process APIs for command execution and file descriptor APIs for pipelines.

### Concurrency Primitives

`concurrency-primitives/`

`pthread`와 POSIX semaphore로 reader-writer lock과 bounded buffer를 구현했습니다. shared state를 보호하고, thread가 언제 기다리고 언제 깨어나야 하는지 직접 제어하는 데 초점을 맞췄습니다.

Threading utilities in C: a writer-priority reader-writer lock and a bounded buffer for producer-consumer workloads.

## Layout

```text
systems-programming-portfolio/
  wfs-fuse-filesystem/
  xv6-stcf-scheduler/
  unix-shell/
  concurrency-primitives/
```

## Build

Linux + GCC 환경을 기준으로 작성했습니다. WFS는 FUSE 개발 헤더가 필요하고, xv6는 QEMU/xv6 toolchain이 필요합니다.

```sh
cd unix-shell && make
cd ../concurrency-primitives && make
cd ../wfs-fuse-filesystem && make
```

```sh
cd xv6-stcf-scheduler
make SCHEDULER=STCF qemu-nox
```

## Notes

개인정보, 수업 제출용 문서, generated binaries, disk images, local test output은 제외했습니다.

Before cleanup, the selected projects passed their local validation in the original development setup.
