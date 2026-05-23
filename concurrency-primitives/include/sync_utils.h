#ifndef SYNC_UTILS_H
#define SYNC_UTILS_H

#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DIE(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)

/* Logging (timestamped) */
uint64_t now_ms(void);
#define LOG(fmt, ...) \
  do { fprintf(stdout, "[%10llu ms] " fmt "\n", (unsigned long long)now_ms(), ##__VA_ARGS__); fflush(stdout); } while (0)

/* Thread helpers */
typedef void* (*thread_fn)(void*);
pthread_t spawn(thread_fn fn, void *arg, const char *name);
void join(pthread_t t);

/* Random jitter for schedule perturbation */
void jitter_us(int min_us, int max_us);

/* ---------- Reader-Writer Lock for Conference Schedule ---------- */

/* Reader-writer lock implemented in readers_writers.c. */
typedef struct {
  /* Mutex to protect all shared state below (counters and flags) */
  pthread_mutex_t m;

  /* Binary semaphore used to hand off write access among writers */
  sem_t wlock;

  /* Number of readers currently holding the lock */
  int readers;

  /* Number of writers that are waiting to acquire the lock */
  int writers_waiting;

  /* True when some writer currently owns the lock */
  bool writer_active;
} rwlock_t;

int  rw_init(rwlock_t *rw);
void rw_destroy(rwlock_t *rw);
void rw_rlock(rwlock_t *rw);
void rw_runlock(rwlock_t *rw);
void rw_wlock(rwlock_t *rw);
void rw_wunlock(rwlock_t *rw);

/* ---------- Bounded Buffer for Producer-Consumer (Snacks) ---------- */

/* Food tray structure */
typedef struct {
  int tray_id;           // Unique tray identifier
  char *food_name;       // Name of food on tray (dynamically allocated)
  int prepared_by;       // Cook who prepared it
} food_tray_t;

/* Bounded buffer implemented in sync_utils.c. */
typedef struct {
  /* Dynamic array of pointers to food trays (the actual buffer) */
  food_tray_t **buf;

  /* Capacity of the buffer (how many tray pointers we can hold) */
  int cap;

  /* Head index: position where the next consumer will take from */
  int head;

  /* Tail index: position where the next producer will put into */
  int tail;

  /* Counts how many empty slots remain (blocks producers when 0) */
  sem_t empty;

  /* Counts how many full slots exist (blocks consumers when 0) */
  sem_t full;

  /* Mutex to protect head/tail and the buffer array itself */
  pthread_mutex_t m;
} bb_t;

int  bb_init(bb_t *q, int capacity);
void bb_destroy(bb_t *q);
void bb_put(bb_t *q, food_tray_t *tray);   /* blocks if full */
food_tray_t* bb_take(bb_t *q);             /* blocks if empty, returns tray to consume */

/* Helper functions for food trays */
food_tray_t* create_food_tray(int tray_id, const char *food_name, int cook_id);
void free_food_tray(food_tray_t *tray);

#endif
