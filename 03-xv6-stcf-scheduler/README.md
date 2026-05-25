# Preemptive STCF Scheduler for xv6

## 한국어

xv6의 기본 round-robin scheduler를 STCF(shortest-time-to-completion-first) 방식으로 바꾼 프로젝트입니다. 핵심은 process마다 남은 실행 시간을 추적하고, scheduler가 가장 빨리 끝날 process를 먼저 고르게 만드는 것이었습니다.

이 폴더는 xv6가 빌드 가능한 형태로 남아 있습니다. 포트폴리오 관점에서 핵심 변경 파일은 아래 파일들입니다.

### 먼저 볼 파일

- `proc.c`: STCF scheduler selection, runtime accounting, yield/sleep/exit integration
- `proc.h`: per-process scheduling metadata
- `sysproc.c`: runtime hint and scheduler-related syscalls
- `syscall.c`, `syscall.h`, `user.h`, `usys.S`: syscall table and user-facing syscall wiring
- `workload1.c`, `workload2.c`, `work.c`: scheduler behavior demo programs

### 변경 내용

- runnable process 중 remaining time이 가장 짧은 process 선택
- xv6 tick 기반 runtime accounting
- `fork` 이후 scheduling metadata 유지
- yield/sleep/exit/context switch 흐름에서 remaining time 갱신
- user program이 runtime hint를 줄 수 있는 syscall 추가
- PID 기반 tie-breaker 적용

### 빌드

```sh
make SCHEDULER=STCF qemu-nox
```

기본 round-robin mode:

```sh
make SCHEDULER=RR qemu-nox
```

## English

This is a modified xv6 kernel with a preemptive STCF scheduler. The scheduler tracks each process's remaining runtime and chooses the runnable process expected to finish first.

This folder keeps xv6 in buildable form. For portfolio review, the main implementation files are the ones below.

### Start Here

- `proc.c`: STCF scheduler selection, runtime accounting, yield/sleep/exit integration
- `proc.h`: per-process scheduling metadata
- `sysproc.c`: runtime hint and scheduler-related syscalls
- `syscall.c`, `syscall.h`, `user.h`, `usys.S`: syscall table and user-facing syscall wiring
- `workload1.c`, `workload2.c`, `work.c`: scheduler behavior demo programs

### What Changed

- scheduler chooses the runnable process with the shortest remaining time
- process runtime is accounted using xv6 ticks
- scheduling metadata is carried across `fork`
- remaining time is updated around yield/sleep/exit/context-switch paths
- user programs can update runtime hints through small syscalls
- ties are handled deterministically with PID ordering

### Build

```sh
make SCHEDULER=STCF qemu-nox
```

Baseline round-robin mode:

```sh
make SCHEDULER=RR qemu-nox
```
