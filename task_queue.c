/*
 * task_queue.c
 * ------------
 * Implementation of a priority-sorted task queue using a singly-linked list.
 *
 * Insertion: O(n) – traverse to find the correct position by priority.
 * Dequeue:   O(1) – always remove the head (highest priority).
 *
 * Callers are responsible for holding the scheduler mutex around all calls.
 */

#include "task_queue.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Human-readable task status strings, indexed by TaskStatus enum. */
const char *TASK_STATUS_STR[] = {
    [TASK_WAITING]   = "WAITING  ",
    [TASK_RUNNING]   = "RUNNING  ",
    [TASK_COMPLETED] = "COMPLETED"
};

/* ------------------------------------------------------------------ */
/* tq_init                                                              */
/* ------------------------------------------------------------------ */
void tq_init(TaskQueue *q)
{
    q->head    = NULL;
    q->size    = 0;
    q->next_id = 1;   /* IDs start at 1; 0 is reserved as "invalid". */
}

/* ------------------------------------------------------------------ */
/* tq_enqueue                                                           */
/* ------------------------------------------------------------------ */
uint64_t tq_enqueue(TaskQueue *q, void (*func)(void *), void *arg, int priority)
{
    Task *t = (Task *)malloc(sizeof(Task));
    if (!t) return 0;

    t->id       = q->next_id++;
    t->priority = priority;
    t->status   = TASK_WAITING;
    t->func     = func;
    t->arg      = arg;
    t->exec_ms  = 0.0;
    t->next     = NULL;

    clock_gettime(CLOCK_MONOTONIC, &t->enqueue_time);
    memset(&t->start_time, 0, sizeof(t->start_time));
    memset(&t->end_time,   0, sizeof(t->end_time));

    /*
     * Insert into the sorted linked list.
     * We walk until we find a node with LOWER priority than t,
     * and splice t in before it.
     */
    Task **cur = &q->head;
    while (*cur && (*cur)->priority >= priority) {
        cur = &(*cur)->next;
    }
    t->next = *cur;
    *cur    = t;

    q->size++;
    return t->id;
}

/* ------------------------------------------------------------------ */
/* tq_dequeue                                                           */
/* ------------------------------------------------------------------ */
Task *tq_dequeue(TaskQueue *q)
{
    if (!q->head) return NULL;

    Task *t   = q->head;
    q->head   = t->next;
    t->next   = NULL;
    q->size--;
    return t;
}

/* ------------------------------------------------------------------ */
/* tq_size                                                              */
/* ------------------------------------------------------------------ */
int tq_size(const TaskQueue *q)
{
    return q->size;
}

/* ------------------------------------------------------------------ */
/* tq_destroy                                                           */
/* ------------------------------------------------------------------ */
void tq_destroy(TaskQueue *q)
{
    Task *cur = q->head;
    while (cur) {
        Task *next = cur->next;
        free(cur);
        cur = next;
    }
    q->head = NULL;
    q->size = 0;
}
