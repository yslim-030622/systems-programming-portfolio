# Preemptive STCF Scheduler for xv6

이 프로젝트는 xv6의 기본 round-robin scheduler를 shortest-time-to-completion-first scheduler로 확장한 커널 프로젝트입니다. process가 알려주는 예상 남은 실행 시간을 바탕으로 scheduler가 다음에 실행할 process를 선택합니다.

This project modifies xv6 to support a preemptive shortest-time-to-completion-first scheduler.

## Why I Built It

scheduler는 OS 내부에서 policy와 mechanism이 만나는 지점입니다. 단순히 process list를 순회하는 코드를 바꾸는 것뿐 아니라, process lifecycle 전체에서 scheduling metadata가 언제 바뀌어야 하는지 이해해야 했습니다.

Scheduler work is useful because it forces you to trace context switches, process states, timer accounting, and syscall boundaries together.

## What It Does

- Chooses the runnable process with the smallest expected remaining time
- Accounts for elapsed runtime using xv6 ticks
- Carries scheduling metadata across `fork`
- Updates remaining time before a process yields, sleeps, exits, or switches out
- Uses higher PID as the tie-breaker when remaining time is equal
- Adds `remain(int new_time)` to update a process's expected remaining time
- Adds `exec2(int time_to_complete, char *file, char **argv)` to set runtime metadata before `exec`
- Adds `give_cpu(void)` to voluntarily yield to another runnable process once
- Keeps an optional `log_sched` hook for tracing scheduler choices

## Implementation Notes

The main scheduler state lives in `struct proc`. Each process tracks remaining time and the tick at which it was last scheduled. Before a context switch, the kernel subtracts elapsed ticks from the process's remaining time. The value is signed because coarse tick accounting can push it slightly below zero after a CPU burst.

The syscall path is wired through `sysproc.c`, `syscall.c`, `syscall.h`, `user.h`, and `usys.S`, so user programs can influence scheduler metadata without directly touching kernel structures.

## Build

This folder contains a complete xv6 tree.

```sh
make SCHEDULER=STCF qemu-nox
```

For baseline round-robin behavior:

```sh
make SCHEDULER=RR qemu-nox
```

## Key Files

- `proc.c` / `proc.h`: scheduler logic, process metadata, and lifecycle integration
- `sysproc.c`: `remain`, `exec2`, and `give_cpu`
- `syscall.c` / `syscall.h`: syscall registration
- `user.h` / `usys.S`: user-space syscall declarations and stubs
- `workload1.c` / `workload2.c`: small programs for scheduler experiments
