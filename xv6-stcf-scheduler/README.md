# Preemptive STCF Scheduler for xv6

This project modifies xv6 to support shortest-time-to-completion-first scheduling. Instead of scanning runnable processes in round-robin order, the scheduler chooses the runnable process with the smallest expected remaining time and preempts when process metadata changes.

## What It Implements

- STCF process selection in `scheduler()`
- Runtime accounting based on xv6 ticks
- Remaining-time inheritance across `fork`
- Scheduling metadata updates on `yield`, `sleep`, `exit`, and context switches
- Deterministic tie-breaking by higher PID
- `remain(int new_time)` syscall to update the current process's expected remaining time
- `exec2(int time_to_complete, char *file, char **argv)` syscall to set runtime metadata before `exec`
- `give_cpu(void)` syscall to voluntarily yield to another runnable process once
- Optional scheduler tracing hook through `log_sched`

## Build

This folder contains a complete xv6 tree so the kernel can be built directly from here.

```sh
make SCHEDULER=STCF qemu-nox
```

For the baseline round-robin behavior:

```sh
make SCHEDULER=RR qemu-nox
```

## Key Files

- `proc.c` / `proc.h` - STCF fields, accounting, process lifecycle integration, and scheduler changes
- `sysproc.c` - `remain`, `exec2`, and `give_cpu` syscall implementations
- `syscall.c` / `syscall.h` - syscall registration
- `user.h` / `usys.S` - user-space syscall declarations and stubs
- `workload1.c` / `workload2.c` - simple user programs for scheduler experiments

## Design Notes

The scheduler keeps `t_remain` as a signed integer because coarse-grained tick accounting can push a process slightly below zero after a CPU burst. When a process is selected, the kernel records the current tick. Before switching away, it subtracts elapsed ticks from the process's remaining time. If multiple runnable processes have the same remaining time, the newer process wins through the higher-PID tie-breaker.
