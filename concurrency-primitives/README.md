# Concurrency Primitives

## 한국어

`pthread`와 POSIX semaphore로 만든 작은 synchronization 라이브러리입니다. thread들이 shared state에 안전하게 접근하도록 reader-writer lock과 bounded buffer를 구현했습니다.

### 구현 내용

- writer-priority reader-writer lock
- producer-consumer workload용 bounded buffer
- mutex 기반 shared state 보호
- semaphore 기반 blocking/wakeup
- synchronization object 초기화와 정리

### 빌드

```sh
make
./conference_sim
```

## English

Small synchronization utilities written in C with pthreads and POSIX semaphores.

### What It Includes

- writer-priority reader-writer lock
- bounded buffer for producer-consumer workloads
- mutex-protected shared state
- semaphore-based blocking and wakeup
- initialization and cleanup paths for synchronization objects

### Build

```sh
make
./conference_sim
```
