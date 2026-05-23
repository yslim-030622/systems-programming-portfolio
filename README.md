# Systems Programming Portfolio

This repository collects four C systems projects that exercise the pieces of Unix-like systems that are usually hidden behind libraries and frameworks: process creation, scheduling, synchronization, and filesystems.

The projects were cleaned for public portfolio use. They intentionally exclude course handouts, generated binaries, disk images, and local test output. Each folder includes a short project-specific README with the implementation focus and build notes.

## Projects

### WFS: Userspace Filesystem with FUSE

`wfs-fuse-filesystem/`

A small block-based filesystem backed by a disk image and mounted through FUSE. The implementation manages the on-disk superblock, inode/data bitmaps, inode table, directory entries, direct and indirect block pointers, file reads/writes, directory operations, timestamps, filesystem statistics, and a `user.color` extended attribute.

This is the strongest standalone portfolio project in the repo because it touches real filesystem mechanics end to end instead of only wrapping library calls.

### Preemptive STCF Scheduler for xv6

`xv6-stcf-scheduler/`

A modified xv6 kernel that adds a shortest-time-to-completion-first scheduler and user-facing scheduling syscalls. The implementation tracks remaining runtime, accounts for elapsed ticks across context switches, inherits scheduling metadata on `fork`, and uses a deterministic tie-breaker when multiple runnable processes have the same remaining time.

### Unix Shell in C

`unix-shell/`

A compact shell implementation with interactive and batch modes, command parsing, built-ins, alias expansion, command history, external command execution via `fork`/`execv`/`waitpid`, and multi-stage pipelines wired with `pipe` and `dup2`.

### Concurrency Primitives

`concurrency-primitives/`

Threading utilities built on `pthread` and POSIX semaphores. The project includes a writer-priority reader-writer lock and a bounded buffer implemented as a circular queue for producer/consumer workloads.

## Repository Layout

```text
systems-programming-portfolio/
  unix-shell/
  xv6-stcf-scheduler/
  concurrency-primitives/
  wfs-fuse-filesystem/
```

## Build Notes

These projects target a Linux development environment with GCC. The FUSE filesystem requires libfuse development headers, and the xv6 project expects the usual xv6/QEMU toolchain.

```sh
cd unix-shell && make
cd ../concurrency-primitives && make
cd ../wfs-fuse-filesystem && make
```

The xv6 scheduler can be built from its folder with the xv6 Makefile. For example:

```sh
cd xv6-stcf-scheduler
make SCHEDULER=STCF qemu-nox
```

## Verification

Before this cleanup, the selected implementations had local test runs passing in their original development layout:

- WFS FUSE filesystem: 25/25 visible tests returned `0`
- Concurrency primitives: 20/20 visible tests returned `0`
- xv6 STCF scheduler: local validation passed the provided checks in the original development layout
- Unix shell: local validation passed the provided checks in the original development layout

The original external test harness is not included here; this repository is meant to present the implementation work clearly rather than redistribute private scaffolding.
