# Concurrency Primitives

This project implements two classic synchronization building blocks in C: a writer-priority reader-writer lock and a bounded buffer for producer/consumer workloads. The code uses `pthread_mutex_t` for shared state protection and POSIX semaphores for blocking/wakeup behavior.

## What It Implements

- `rwlock_t`: a reader-writer lock that prevents new readers from entering while writers are waiting
- `bb_t`: a fixed-capacity circular buffer for producer/consumer handoff
- Clean initialization and teardown for mutexes, semaphores, and heap storage
- A small simulation driver that exercises the synchronization utilities

## Build

```sh
make
./conference_sim
```

## Files

- `include/sync_utils.h` - shared data structures and function declarations
- `src/sync_utils.c` - initialization/destruction logic and bounded-buffer operations
- `src/readers_writers.c` - reader-writer lock behavior
- `src/bounded_buffer.c` - scenario support code
- `src/main.c` - demo entry point

## Design Notes

The reader-writer lock favors writers. Readers are allowed in only when there is no active writer and no writer waiting. When the last reader exits, it wakes one waiting writer. The bounded buffer uses `empty` and `full` semaphores to represent available slots and queued items, while a mutex protects the circular `head` and `tail` updates.
