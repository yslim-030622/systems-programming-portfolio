#define _XOPEN_SOURCE 700
#include <unistd.h>
#include "readers_writers.h"
#include "sync_utils.h"

int usleep(unsigned int usec);
extern int rw_init(rwlock_t *rw);   /* in sync_utils.c: sets m,wlock, counters */
extern void rw_destroy(rwlock_t *rw);

static rwlock_t board;
static int schedule_version = 0;
static int violation_count = 0;
static int readers_in_cs = 0;
static int writers_in_cs = 0;
static pthread_mutex_t instrumentation_mutex = PTHREAD_MUTEX_INITIALIZER;

int get_final_schedule_version(void) {
  return schedule_version;
}
int get_violation_count(void) {
  return violation_count;
}

/*
 * Reader lock (writer-priority).
 *
 * Rules:
 *  - Multiple readers can read at the same time.
 *  - But if a writer is waiting or currently active, new readers must wait.
 *  - We spin with a small sleep instead of blocking on a semaphore,
 *    because the only semaphore is reserved for writers (wlock).
 */
void rw_rlock(rwlock_t *rw) {
    while (1) {
        pthread_mutex_lock(&rw->m);

        /* If no writer is active and no writer is waiting, we can enter. */
        if (!rw->writer_active && rw->writers_waiting == 0) {
            rw->readers++;  // this reader is now inside the critical section
            pthread_mutex_unlock(&rw->m);
            break;
        }

        /*
         * A writer is either waiting or already active.
         * Give writers priority: back off and try again later.
         */
        pthread_mutex_unlock(&rw->m);

        /* Small sleep to avoid burning CPU in a tight spin loop. */
        usleep(100);  // 0.1 ms
    }
}

/*
 * Reader unlock.
 *
 * Rules:
 *  - Decrement the reader count.
 *  - If this was the last reader and writers are waiting,
 *    wake exactly one writer via the writer semaphore.
 */
void rw_runlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->m);

    rw->readers--;

    /* If no more readers and at least one writer is waiting, wake a writer. */
    if (rw->readers == 0 && rw->writers_waiting > 0) {
        /* Last reader hands off the lock to a waiting writer. */
        sem_post(&rw->wlock);
    }

    pthread_mutex_unlock(&rw->m);
}

/*
 * Writer lock (writer-priority).
 *
 * Rules:
 *  - Only one writer can be active at a time.
 *  - Writers must wait until:
 *      * no readers are inside, and
 *      * no other writer is active.
 *  - writers_waiting > 0 is used to block *new* readers (priority).
 *  - Writers sleep on wlock until a reader/writer wakes them up.
 */
void rw_wlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->m);

    /* Announce that this writer is waiting so new readers will stay out. */
    rw->writers_waiting++;

    /* Wait until there are no readers and no active writer. */
    while (rw->writer_active || rw->readers > 0) {
        pthread_mutex_unlock(&rw->m);

        /*
         * Go to sleep on the writer semaphore.
         * We will be woken up by:
         *  - the last reader (rw_runlock), or
         *  - a writer that just finished (rw_wunlock).
         */
        sem_wait(&rw->wlock);

        /* Re-check conditions under the mutex after waking up. */
        pthread_mutex_lock(&rw->m);
    }

    /* At this point: no readers and no active writer. We can take over. */
    rw->writers_waiting--;
    rw->writer_active = true;

    pthread_mutex_unlock(&rw->m);
}

/*
 * Writer unlock.
 *
 * Rules:
 *  - Mark that no writer is active.
 *  - If other writers are waiting, wake exactly one of them.
 *    (Readers will only proceed when there are no waiting writers.)
 */
void rw_wunlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->m);

    rw->writer_active = false;

    if (rw->writers_waiting > 0) {
        /*
         * Give priority to writers: wake one waiting writer.
         * Readers will see writers_waiting > 0 and stay out.
         */
        sem_post(&rw->wlock);
    }

    pthread_mutex_unlock(&rw->m);
}

static void* reader(void* arg) {
  long id = (long)arg;
  for (int k=0;k<5;k++) {
    jitter_us(500, 4000);
    rw_rlock(&board);
    pthread_mutex_lock(&instrumentation_mutex);
    readers_in_cs++;
    if (writers_in_cs > 0) violation_count++;
    pthread_mutex_unlock(&instrumentation_mutex);
    int v = schedule_version;
    LOG("Attendee#%ld reads schedule v%d", id, v);
    jitter_us(200, 800);
    pthread_mutex_lock(&instrumentation_mutex);
    readers_in_cs--;
    pthread_mutex_unlock(&instrumentation_mutex);
    rw_runlock(&board);
  }
  return NULL;
}

static void* writer(void* arg) {
  long id = (long)arg;
  for (int k=0;k<3;k++) {
    jitter_us(2000, 6000);
    rw_wlock(&board);
    pthread_mutex_lock(&instrumentation_mutex);
    writers_in_cs++;
    if (writers_in_cs > 1 || readers_in_cs > 0) violation_count++;
    pthread_mutex_unlock(&instrumentation_mutex);
    schedule_version++;
    LOG("Organizer#%ld updates schedule to v%d", id, schedule_version);
    jitter_us(200, 800);
    pthread_mutex_lock(&instrumentation_mutex);
    writers_in_cs--;
    pthread_mutex_unlock(&instrumentation_mutex);
    rw_wunlock(&board);
  }
  return NULL;
}

int schedule_run(void) {
  rw_init(&board);
  pthread_t readers[8], writers[2];
  for (long i=0;i<8;i++) readers[i] = spawn(reader, (void*)i, "reader");
  for (long i=0;i<2;i++) writers[i] = spawn(writer, (void*)i, "writer");
  for (int i=0;i<8;i++) join(readers[i]);
  for (int i=0;i<2;i++) join(writers[i]);
  rw_destroy(&board);
  LOG("Schedule (readers-writers) complete.");
  return 0;
}