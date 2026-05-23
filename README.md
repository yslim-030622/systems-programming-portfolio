# Systems Programming Portfolio

운영체제 수업에서 다룬 C 기반 시스템 프로그래밍 프로젝트들을 공개 포트폴리오용으로 정리한 저장소입니다. 단순히 과제 파일을 모아둔 형태가 아니라, Unix-like 시스템의 핵심 동작을 직접 구현해 본 경험이 드러나도록 구조와 문서를 다시 다듬었습니다.

This repository is a cleaned portfolio version of several C systems programming projects. The focus is on the lower-level pieces behind Unix-like systems: process creation, shell execution, kernel scheduling, synchronization, and filesystem internals.

## What This Shows

이 저장소는 아래 역량을 보여주기 위한 목적입니다.

- C로 메모리와 리소스를 직접 관리하는 능력
- `fork`, `exec`, `wait`, `pipe`, `dup2` 같은 Unix process API 이해
- xv6 커널 내부에서 scheduler와 syscall path를 수정한 경험
- `pthread`, mutex, semaphore 기반 동시성 제어
- FUSE 기반 userspace filesystem 구현 경험
- 테스트 가능한 형태로 기능을 쪼개고 설명하는 습관

In short, these projects are meant to show practical systems-level work rather than framework usage.

## Projects

### 1. WFS: Userspace Filesystem with FUSE

`wfs-fuse-filesystem/`

작은 disk image 위에 동작하는 block-based filesystem입니다. FUSE를 통해 userspace에서 mount되고, 일반적인 shell command가 이 filesystem 위에서 동작할 수 있도록 callback을 구현했습니다.

This is a small block-based filesystem backed by a disk image and mounted through FUSE.

주요 구현 내용:

- superblock, inode bitmap, data bitmap 관리
- inode table과 directory entry 기반 path resolution
- file/directory 생성, 삭제, 읽기, 쓰기
- direct block pointer와 indirect block pointer
- `getattr`, `readdir`, `mknod`, `mkdir`, `unlink`, `rmdir`, `read`, `write`, `truncate`, `statfs`
- access/modify/change timestamp 갱신
- `user.color` extended attribute 지원

Why it matters:

Filesystem code forces the implementation to keep metadata, allocation state, and user-visible behavior consistent. This project is the strongest standalone piece in the portfolio because it connects data layout, path lookup, block allocation, and FUSE callbacks end to end.

### 2. Preemptive STCF Scheduler for xv6

`xv6-stcf-scheduler/`

xv6의 기본 round-robin scheduler를 shortest-time-to-completion-first 방식으로 확장한 커널 프로젝트입니다. 단순히 scheduling policy만 바꾼 것이 아니라, process metadata와 syscall을 추가해 scheduler가 runtime hint를 기반으로 결정을 내리도록 만들었습니다.

This project modifies xv6 to support a preemptive shortest-time-to-completion-first scheduler.

주요 구현 내용:

- `scheduler()`에서 runnable process 중 남은 시간이 가장 짧은 process 선택
- tick 기반 runtime accounting
- `fork`, `yield`, `sleep`, `exit`, context switch 시 scheduling metadata 갱신
- 동일 remaining time일 때 higher PID tie-breaker 적용
- `remain(int)` syscall 추가
- `exec2(int, char *, char **)` syscall 추가
- `give_cpu(void)` syscall 추가
- optional scheduler tracing hook

Why it matters:

Scheduler work requires understanding where the kernel switches context, how process state changes over time, and how user-facing syscalls connect back into kernel policy. It is a good interview discussion point for OS, systems, infrastructure, and backend roles.

### 3. Unix Shell in C

`unix-shell/`

C로 구현한 작은 Unix-style shell입니다. command line을 parsing하고, built-in command와 external command를 구분하며, child process를 생성하고 pipeline을 연결합니다.

This is a compact Unix-style shell implemented in C.

주요 구현 내용:

- interactive mode와 batch mode
- command parsing과 single quote 처리
- built-ins: `exit`, `alias`, `unalias`, `which`, `path`, `cd`, `history`
- alias expansion
- `PATH` 기반 executable lookup
- `fork`, `execv`, `waitpid` 기반 process execution
- `pipe`, `dup2` 기반 multi-stage pipeline
- history 저장용 dynamic array
- alias table용 hash map

Why it matters:

A shell is a practical way to show process control. It touches parsing, process lifecycle, file descriptors, pipe wiring, and cleanup logic in one small program.

### 4. Concurrency Primitives

`concurrency-primitives/`

`pthread`와 POSIX semaphore를 사용해 reader-writer lock과 bounded buffer를 구현한 프로젝트입니다. shared state를 안전하게 보호하고, producer/consumer workload에서 blocking과 wakeup을 제어하는 데 초점을 맞췄습니다.

This project implements synchronization utilities for threaded C programs.

주요 구현 내용:

- writer-priority `rwlock_t`
- reader count, waiting writer count, active writer state 관리
- bounded buffer를 circular queue로 구현
- `empty` / `full` semaphore 기반 producer-consumer synchronization
- mutex 기반 head/tail update 보호
- initialization / destruction path 정리

Why it matters:

Concurrency bugs are often about small state transitions. This project shows explicit synchronization design rather than relying on a high-level runtime.

## Repository Layout

```text
systems-programming-portfolio/
  wfs-fuse-filesystem/
  xv6-stcf-scheduler/
  unix-shell/
  concurrency-primitives/
```

## Build Notes

이 프로젝트들은 Linux + GCC 환경을 기준으로 작성되었습니다. FUSE filesystem은 libfuse development headers가 필요하고, xv6 scheduler는 xv6/QEMU toolchain이 필요합니다.

These projects target a Linux development environment with GCC.

```sh
cd unix-shell && make
cd ../concurrency-primitives && make
cd ../wfs-fuse-filesystem && make
```

For xv6:

```sh
cd xv6-stcf-scheduler
make SCHEDULER=STCF qemu-nox
```

## Verification

공개 저장소에는 원본 테스트 harness와 로컬 실행 결과 파일을 포함하지 않았습니다. 대신 공개 가능한 구현 코드와 설명 문서만 남겼습니다.

Before cleanup, the selected implementations passed local validation in their original development layout:

- WFS FUSE filesystem: 25/25 visible tests returned `0`
- Concurrency primitives: 20/20 visible tests returned `0`
- xv6 STCF scheduler: local validation passed the provided checks
- Unix shell: local validation passed the provided checks

## Cleanup Notes

개인정보, 수업 제출용 문서, generated binaries, disk images, local test output은 공개용 저장소에서 제외했습니다. 이 repo는 과제 배포물이 아니라, 구현 경험을 설명하기 위한 포트폴리오 저장소입니다.

Personal information, generated files, disk images, local test outputs, and course handouts were removed. The goal is to present the engineering work clearly without redistributing private scaffolding.
