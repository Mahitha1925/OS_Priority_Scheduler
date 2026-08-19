#ifndef THREAD_POOL_H
#define THREAD_POOL_H

/*
 * thread_pool.h
 * -------------
 * Public interface for the multithreaded priority task scheduler.
 *
 * Architecture
 * ============
 * A fixed-size pool of POSIX threads (workers) share a single
 * mutex-protected priority queue.  The producer-consumer pattern is
 * implemented with a condition variable:
 *
 *   Producers (UI / caller): lock mutex → enqueue task → signal cond → unlock
 *   Consumers (workers):     lock mutex → wait on cond while queue empty →
 *                            dequeue task → unlock → execute task → repeat
 *
 * Shutdown: set shutdown flag → broadcast to all workers → join every thread.
 */

#include <pthread.h>
#include <stdint.h>
#include "task_queue.h"

/* Maximum number of completed task records kept for display. */
#define MAX_HISTORY  64

/* Maximum number of worker threads. */
#define MAX_THREADS  16

/* ------------------------------------------------------------------ */
/* Scheduler state                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    /* Thread pool */
    pthread_t   workers[MAX_THREADS];
    int         num_threads;

    /* Synchronisation primitives */
    pthread_mutex_t  mutex;     /* Guards: queue, history, counters, shutdown */
    pthread_cond_t   work_cond; /* Signalled when a task is available or shutdown */

    /* Task queue (WAITING tasks) */
    TaskQueue   queue;

    /* Completed-task ring buffer (for display) */
    Task        history[MAX_HISTORY];
    int         history_head;   /* Next write position (ring buffer) */
    int         history_count;  /* Number of valid entries            */

    /* Live counters */
    int         running_count;
    int         completed_count;

    /* Lifecycle flag – set to 1 to initiate graceful shutdown */
    volatile int shutdown;
} Scheduler;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/**
 * Initialise the scheduler and start num_threads worker threads.
 * Returns 0 on success, non-zero on failure.
 */
int  scheduler_init(Scheduler *s, int num_threads);

/**
 * Add a task to the priority queue.
 * func     – work function executed by a worker thread.
 * arg      – opaque argument forwarded to func.
 * priority – higher integer = higher priority.
 * Returns the assigned task ID, or 0 on failure / shutdown.
 */
uint64_t scheduler_add_task(Scheduler *s, void (*func)(void *), void *arg, int priority);

/**
 * Begin graceful shutdown:
 *   1. Set shutdown flag.
 *   2. Broadcast to wake sleeping workers.
 *   3. Join every worker thread.
 *   4. Release all resources.
 */
void scheduler_shutdown(Scheduler *s);

/* ------------------------------------------------------------------ */
/* Read-only snapshot (used by the UI – must hold mutex)                */
/* ------------------------------------------------------------------ */
typedef struct {
    int      num_threads;
    int      pending;
    int      running;
    int      completed;
    /* Snapshot of up to MAX_HISTORY completed tasks + waiting tasks   */
    Task     tasks[MAX_HISTORY + MAX_THREADS * 4]; /* ample space     */
    int      task_count;
} SchedulerSnapshot;

/**
 * Fill *snap with a consistent snapshot of scheduler state.
 * This call acquires and releases the scheduler mutex internally.
 */
void scheduler_snapshot(Scheduler *s, SchedulerSnapshot *snap);

#endif /* THREAD_POOL_H */
