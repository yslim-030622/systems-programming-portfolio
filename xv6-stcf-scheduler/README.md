# Preemptive STCF Scheduler for xv6

xv6의 기본 round-robin scheduler를 STCF(shortest-time-to-completion-first) 방식으로 바꾼 프로젝트입니다. 핵심은 process마다 남은 실행 시간을 추적하고, scheduler가 가장 빨리 끝날 process를 먼저 고르게 만드는 것이었습니다.

This is a modified xv6 kernel with a preemptive STCF scheduler.

## What Changed

- scheduler chooses the runnable process with the shortest remaining time
- process runtime is accounted using xv6 ticks
- scheduling metadata is carried across `fork`
- remaining time is updated around yield/sleep/exit/context-switch paths
- user programs can update runtime hints through small syscalls
- ties are handled deterministically with PID ordering

## Build

```sh
make SCHEDULER=STCF qemu-nox
```

Baseline round-robin mode:

```sh
make SCHEDULER=RR qemu-nox
```

## Main Files

- `proc.c`, `proc.h`: scheduler state and process lifecycle changes
- `sysproc.c`: scheduler-related syscalls
- `syscall.c`, `syscall.h`, `user.h`, `usys.S`: syscall wiring
- `workload1.c`, `workload2.c`: small scheduler experiments
