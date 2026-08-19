/*
 * thread_pool.c
 * -------------
 * Implementation of the thread pool and scheduler core.
 *
 * Synchronisation design
 * ======================
 * A single mutex (s->mutex) protects ALL shared state:
 *   - s->queue        (task linked-list)
 *   - s->history[]    (completed task ring-buffer)
 *   - s->running_count / s->completed_count
 *   - s->shutdown
 *
 * Workers block on s->work_cond when the queue is empty.
 * The condition is signalled by:
 *   - scheduler_add_task()  → pthread_cond_signal()  (one task available)
 *   - scheduler_shutdown()  → pthread_cond_broadcast() (wake all to exit)
 *
 * The pattern avoids busy-waiting entirely.
 */

#include "thread_pool.h"
#include "logger.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/** Compute elapsed milliseconds between two CLOCK_MONOTONIC readings. */
static double elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    return (double)(end->tv_sec  - start->tv_sec)  * 1000.0 +
           (double)(end->tv_nsec - start->tv_nsec) / 1e6;
}

/** Copy a completed Task into the ring buffer history (caller holds mutex). */
static void record_history(Scheduler *s, const Task *t)
{
    s->history[s->history_head] = *t;
    s->history[s->history_head].next = NULL; /* Detach linked-list pointer */
    s->history_head = (s->history_head + 1) % MAX_HISTORY;
    if (s->history_count < MAX_HISTORY)
        s->history_count++;
}

/* ------------------------------------------------------------------ */
/* Worker thread entry point                                            */
/* ------------------------------------------------------------------ */
static void *worker_thread(void *arg)
{
    Scheduler *s = (Scheduler *)arg;

    for (;;) {
        /* ---- LOCK ---- */
        pthread_mutex_lock(&s->mutex);

        /*
         * Wait while the queue is empty AND we are not shutting down.
         * pthread_cond_wait atomically releases the mutex and suspends
         * the thread until woken; it re-acquires the mutex before returning.
         *
         * We loop (while, not if) to handle spurious wakeups.
         */
        while (tq_size(&s->queue) == 0 && !s->shutdown) {
            pthread_cond_wait(&s->work_cond, &s->mutex);
        }

        /* On shutdown with an empty queue, this worker exits cleanly. */
        if (s->shutdown && tq_size(&s->queue) == 0) {
            pthread_mutex_unlock(&s->mutex);
            break;
        }

        /* Dequeue the highest-priority task. */
        Task *t = tq_dequeue(&s->queue);
        if (!t) {
            /* Defensive: should not happen, but be safe. */
            pthread_mutex_unlock(&s->mutex);
            continue;
        }

        /* Mark as running and record start time. */
        t->status = TASK_RUNNING;
        clock_gettime(CLOCK_MONOTONIC, &t->start_time);
        s->running_count++;

        /* ---- UNLOCK before executing (non-blocking) ---- */
        pthread_mutex_unlock(&s->mutex);

        log_message("[Worker] Executing task #%lu  priority=%d",
                    (unsigned long)t->id, t->priority);

        /* Execute the work function outside the lock. */
        if (t->func) t->func(t->arg);

        /* ---- LOCK again to update completion state ---- */
        pthread_mutex_lock(&s->mutex);

        clock_gettime(CLOCK_MONOTONIC, &t->end_time);
        t->exec_ms  = elapsed_ms(&t->start_time, &t->end_time);
        t->status   = TASK_COMPLETED;
        s->running_count--;
        s->completed_count++;

        log_message("[Worker] Completed task #%lu  exec=%.2f ms",
                    (unsigned long)t->id, t->exec_ms);

        record_history(s, t);
        free(t);          /* Release the heap-allocated Task struct. */

        pthread_mutex_unlock(&s->mutex);
        /* ---- UNLOCK ---- */
    }

    log_message("[Worker] Thread exiting (tid=%lu)",
                (unsigned long)pthread_self());
    return NULL;
}

/* ------------------------------------------------------------------ */
/* scheduler_init                                                       */
/* ------------------------------------------------------------------ */
int scheduler_init(Scheduler *s, int num_threads)
{
    if (num_threads < 1 || num_threads > MAX_THREADS) return -1;

    memset(s, 0, sizeof(*s));
    s->num_threads = num_threads;

    tq_init(&s->queue);

    /* Initialise the mutex with default (non-recursive) attributes. */
    if (pthread_mutex_init(&s->mutex, NULL) != 0) return -2;

    /* Initialise the condition variable with default attributes. */
    if (pthread_cond_init(&s->work_cond, NULL) != 0) {
        pthread_mutex_destroy(&s->mutex);
        return -3;
    }

    /* Spawn worker threads. They immediately block on work_cond. */
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&s->workers[i], NULL, worker_thread, s) != 0) {
            /* Partial initialisation: set shutdown and join created threads. */
            s->shutdown    = 1;
            s->num_threads = i; /* Only i threads were created. */
            pthread_cond_broadcast(&s->work_cond);
            for (int j = 0; j < i; j++) pthread_join(s->workers[j], NULL);
            pthread_cond_destroy(&s->work_cond);
            pthread_mutex_destroy(&s->mutex);
            return -4;
        }
    }

    log_message("[Scheduler] Initialised with %d worker thread(s)", num_threads);
    return 0;
}

/* ------------------------------------------------------------------ */
/* scheduler_add_task                                                   */
/* ------------------------------------------------------------------ */
uint64_t scheduler_add_task(Scheduler *s, void (*func)(void *), void *arg, int priority)
{
    pthread_mutex_lock(&s->mutex);

    if (s->shutdown) {
        /* Reject new tasks once shutdown has been initiated. */
        pthread_mutex_unlock(&s->mutex);
        return 0;
    }

    uint64_t id = tq_enqueue(&s->queue, func, arg, priority);

    /*
     * Signal ONE sleeping worker that a task is available.
     * Using signal (not broadcast) is correct here: only one worker
     * should wake to claim the single newly-added task.
     */
    pthread_cond_signal(&s->work_cond);

    pthread_mutex_unlock(&s->mutex);

    log_message("[Scheduler] Added task #%lu  priority=%d", (unsigned long)id, priority);
    return id;
}

/* ------------------------------------------------------------------ */
/* scheduler_shutdown                                                   */
/* ------------------------------------------------------------------ */
void scheduler_shutdown(Scheduler *s)
{
    log_message("[Scheduler] Shutdown initiated");

    pthread_mutex_lock(&s->mutex);
    s->shutdown = 1;

    /*
     * Broadcast wakes ALL sleeping workers.
     * Each worker will re-evaluate the loop condition and, finding
     * the queue empty AND shutdown set, will exit cleanly.
     * Workers currently executing a task will check shutdown after
     * they finish their current task.
     */
    pthread_cond_broadcast(&s->work_cond);
    pthread_mutex_unlock(&s->mutex);

    /* Join every worker thread before releasing resources. */
    for (int i = 0; i < s->num_threads; i++) {
        pthread_join(s->workers[i], NULL);
    }

    /* Release remaining WAITING tasks (should be empty after drain). */
    pthread_mutex_lock(&s->mutex);
    tq_destroy(&s->queue);
    pthread_mutex_unlock(&s->mutex);

    pthread_cond_destroy(&s->work_cond);
    pthread_mutex_destroy(&s->mutex);

    log_message("[Scheduler] Shutdown complete. Completed=%d", s->completed_count);
}

/* ------------------------------------------------------------------ */
/* scheduler_snapshot                                                   */
/* ------------------------------------------------------------------ */
void scheduler_snapshot(Scheduler *s, SchedulerSnapshot *snap)
{
    pthread_mutex_lock(&s->mutex);

    snap->num_threads = s->num_threads;
    snap->pending     = tq_size(&s->queue);
    snap->running     = s->running_count;
    snap->completed   = s->completed_count;
    snap->task_count  = 0;

    /* Copy WAITING tasks from the queue (in priority order). */
    Task *cur = s->queue.head;
    while (cur && snap->task_count < (int)(sizeof(snap->tasks)/sizeof(snap->tasks[0]))) {
        snap->tasks[snap->task_count++] = *cur;
    }

    /* Copy completed task history (oldest first). */
    int start = (s->history_count < MAX_HISTORY)
                ? 0
                : s->history_head;          /* oldest slot when buffer is full */

    for (int i = 0; i < s->history_count &&
         snap->task_count < (int)(sizeof(snap->tasks)/sizeof(snap->tasks[0])); i++) {
        int idx = (start + i) % MAX_HISTORY;
        snap->tasks[snap->task_count++] = s->history[idx];
    }

    pthread_mutex_unlock(&s->mutex);
}
