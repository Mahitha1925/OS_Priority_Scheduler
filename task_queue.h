#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

/*
 * task_queue.h
 * -----------
 * Defines the priority task queue structures and operations.
 * The queue is implemented as a singly-linked list sorted in
 * descending priority order (highest priority dequeued first).
 *
 * All queue operations are thread-safe when the caller holds
 * the associated mutex (defined in thread_pool.h / thread_pool.c).
 */

#include <time.h>
#include <stdint.h>

/* Task lifecycle states */
typedef enum {
    TASK_WAITING   = 0,
    TASK_RUNNING   = 1,
    TASK_COMPLETED = 2
} TaskStatus;

extern const char *TASK_STATUS_STR[];

/* Task descriptor */
typedef struct Task {
    uint64_t        id;
    int             priority;
    TaskStatus      status;
    void          (*func)(void *);
    void           *arg;
    struct timespec enqueue_time;
    struct timespec start_time;
    struct timespec end_time;
    double          exec_ms;
    struct Task    *next;
} Task;

/* Priority queue (linked list, sorted descending by priority) */
typedef struct {
    Task    *head;
    int      size;
    uint64_t next_id;
} TaskQueue;

void     tq_init(TaskQueue *q);
uint64_t tq_enqueue(TaskQueue *q, void (*func)(void *), void *arg, int priority);
Task    *tq_dequeue(TaskQueue *q);
int      tq_size(const TaskQueue *q);
void     tq_destroy(TaskQueue *q);

#endif /* TASK_QUEUE_H */
