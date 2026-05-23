# Concurrency Primitives

`pthread`와 POSIX semaphore로 만든 작은 synchronization 라이브러리입니다. thread들이 shared state에 안전하게 접근하도록 reader-writer lock과 bounded buffer를 구현했습니다.

Small synchronization utilities written in C with pthreads and POSIX semaphores.

## What It Includes

- writer-priority reader-writer lock
- bounded buffer for producer-consumer workloads
- mutex-protected shared state
- semaphore-based blocking and wakeup
- initialization and cleanup paths for synchronization objects

## Build

```sh
make
./conference_sim
```

## Files

- `include/sync_utils.h`: shared types and declarations
- `src/sync_utils.c`: setup, teardown, and bounded-buffer logic
- `src/readers_writers.c`: reader-writer lock logic
- `src/bounded_buffer.c`, `src/main.c`: demo support
