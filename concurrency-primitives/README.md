# Concurrency Primitives

이 프로젝트는 C에서 직접 구현한 synchronization utilities입니다. `pthread_mutex_t`와 POSIX semaphore를 사용해 writer-priority reader-writer lock과 bounded buffer를 만들었습니다.

This project implements two classic synchronization building blocks in C: a writer-priority reader-writer lock and a bounded buffer for producer-consumer workloads.

## Why I Built It

동시성 코드는 작은 상태 전환 하나가 race condition이나 deadlock으로 이어질 수 있습니다. 이 프로젝트는 high-level runtime에 맡기지 않고, 어떤 thread가 언제 들어가고 언제 block/wakeup 되어야 하는지 직접 제어하는 데 초점을 맞췄습니다.

The goal is to show explicit synchronization design: shared state protection, blocking behavior, wakeups, and cleanup.

## What It Does

- Implements `rwlock_t`, a writer-priority reader-writer lock
- Prevents new readers from entering while writers are waiting
- Tracks active readers, waiting writers, and active writer state
- Implements `bb_t`, a fixed-capacity circular buffer
- Uses `empty` and `full` semaphores for producer-consumer coordination
- Protects circular buffer `head` and `tail` updates with a mutex
- Provides initialization and teardown paths for mutexes, semaphores, and heap storage

## Implementation Notes

The reader-writer lock gives priority to writers: once a writer is waiting, new readers wait instead of extending reader ownership indefinitely. When the last reader exits, it wakes one waiting writer.

The bounded buffer uses a circular array. Producers wait on `empty`, consumers wait on `full`, and a mutex protects the actual queue update.

## Build

```sh
make
./conference_sim
```

## Files

- `include/sync_utils.h`: shared data structures and declarations
- `src/sync_utils.c`: initialization, destruction, and bounded-buffer operations
- `src/readers_writers.c`: reader-writer lock behavior
- `src/bounded_buffer.c`: scenario support code
- `src/main.c`: demo entry point
