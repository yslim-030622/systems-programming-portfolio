#define _XOPEN_SOURCE 700
#include <unistd.h>
#include "sync_utils.h"
#include <sys/time.h>
#include <string.h>

int usleep(unsigned int usec);
uint64_t now_ms(void) {
  struct timeval tv; gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

pthread_t spawn(thread_fn fn, void *arg, const char *name) {
  (void)name; /* name useful for extended logging */
  pthread_t t;
  if (pthread_create(&t, NULL, fn, arg)) DIE("pthread_create");
  return t;
}
void join(pthread_t t) { if (pthread_join(t, NULL)) DIE("pthread_join"); }

void jitter_us(int min_us, int max_us) {
  int span = (max_us > min_us) ? (max_us - min_us) : 1;
  int d = min_us + (rand() % span);
  usleep(d);
}

/* ------- Reader-Writer Lock: initialization and cleanup ------- */
int rw_init(rwlock_t *rw) {
  /*
   * Set up everything for the reader-writer lock.
   * - Start with no readers, no waiting writers, and no active writer.
   * - Initialize the mutex that protects these shared fields.
   * - Initialize the writer semaphore to 0 so writers will block
   *   until someone explicitly wakes them up.
   */

  if (pthread_mutex_init(&rw->m, NULL) != 0) {
    // If we can't even create the mutex, just report failure.
    return -1;
  }

  if (sem_init(&rw->wlock, 0, 0) != 0) {
    // If semaphore init fails, clean up the mutex we already made.
    pthread_mutex_destroy(&rw->m);
    return -1;
  }

  // No readers or writers at the beginning.
  rw->readers = 0;
  rw->writers_waiting = 0;
  rw->writer_active = false;

  return 0;
}

void rw_destroy(rwlock_t *rw) {
  /*
   * Tear down the reader-writer lock.
   * We assume no one is using it anymore when this is called.
   */

  pthread_mutex_destroy(&rw->m);
  sem_destroy(&rw->wlock);
}
/* RW lock functions are implemented in readers_writers.c */

/* ------- Food Tray Helper Functions ------- */
food_tray_t* create_food_tray(int tray_id, const char *food_name, int cook_id) {
  food_tray_t *tray = malloc(sizeof(food_tray_t));
  if (!tray) DIE("malloc food_tray");
  
  tray->tray_id = tray_id;
  tray->food_name = strdup(food_name);
  if (!tray->food_name) DIE("strdup food_name");
  tray->prepared_by = cook_id;
  
  return tray;
}

void free_food_tray(food_tray_t *tray) {
  if (tray) {
    free(tray->food_name);
    free(tray);
  }
}

/* ------- Bounded Buffer: initialization and operations ------- */
int bb_init(bb_t *q, int capacity) {
  /* 
   * Set up a bounded buffer with the given capacity.
   * - Allocate an array of tray pointers (initially all NULL).
   * - Initialize head and tail for a circular queue.
   * - empty semaphore starts at 'capacity' (all slots are empty).
   * - full semaphore starts at 0 (no trays yet).
   * - A mutex protects head, tail, and the buffer array itself.
   */

  if (capacity <= 0) {
    // A buffer that can't hold anything is not useful.
    return -1;
  }

  q->buf = calloc(capacity, sizeof(food_tray_t *));
  if (!q->buf) {
    // Allocation failed; nothing to clean up yet besides returning error.
    return -1;
  }

  q->cap  = capacity;
  q->head = 0;
  q->tail = 0;

  if (pthread_mutex_init(&q->m, NULL) != 0) {
    // Mutex failed; free the buffer and report error.
    free(q->buf);
    q->buf = NULL;
    return -1;
  }

  // empty = number of free slots; full = number of used slots.
  if (sem_init(&q->empty, 0, capacity) != 0) {
    pthread_mutex_destroy(&q->m);
    free(q->buf);
    q->buf = NULL;
    return -1;
  }

  if (sem_init(&q->full, 0, 0) != 0) {
    sem_destroy(&q->empty);
    pthread_mutex_destroy(&q->m);
    free(q->buf);
    q->buf = NULL;
    return -1;
  }

  return 0;
}

void bb_destroy(bb_t *q) {
  /*
   * Tear down the bounded buffer.
   * We assume all producers/consumers have stopped using it.
   * The trays themselves should have been freed by the consumers;
   * here we only free the queue structure (array + sync primitives).
   */

  pthread_mutex_destroy(&q->m);
  sem_destroy(&q->empty);
  sem_destroy(&q->full);

  free(q->buf);
  q->buf = NULL;
  q->cap = 0;
  q->head = 0;
  q->tail = 0;
}

void bb_put(bb_t *q, food_tray_t *tray) {
  /*
   * Producer side: put one tray into the queue.
   * Steps:
   *  1) Wait until there is at least one empty slot.
   *  2) Lock the mutex so we can safely update tail and the buffer.
   *  3) Store the tray pointer at the tail position.
   *  4) Move tail forward (wrap around with modulo for circular queue).
   *  5) Unlock the mutex.
   *  6) Signal that there is one more full slot available.
   */

  // Block here if the buffer is full.
  sem_wait(&q->empty);

  pthread_mutex_lock(&q->m);

  q->buf[q->tail] = tray;
  q->tail = (q->tail + 1) % q->cap;

  pthread_mutex_unlock(&q->m);

  // Tell consumers that there is a new item to take.
  sem_post(&q->full);
}

food_tray_t* bb_take(bb_t *q) {
  /*
   * Consumer side: take one tray out of the queue.
   * Steps:
   *  1) Wait until there is at least one full slot.
   *  2) Lock the mutex so we can safely update head and the buffer.
   *  3) Read the tray pointer from the head position.
   *  4) Move head forward (wrap around with modulo).
   *  5) Unlock the mutex.
   *  6) Signal that there is now one more empty slot.
   *  7) Return the tray pointer to the caller (who will consume/free it).
   */

  // Block here if the buffer is empty.
  sem_wait(&q->full);

  pthread_mutex_lock(&q->m);

  food_tray_t *tray = q->buf[q->head];
  q->head = (q->head + 1) % q->cap;

  pthread_mutex_unlock(&q->m);

  // Tell producers that there is space for one more tray.
  sem_post(&q->empty);

  return tray;
}