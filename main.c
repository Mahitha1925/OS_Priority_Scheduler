/*
 * main.c
 * ------
 * Entry point for the Multithreaded Priority Task Scheduler.
 *
 * Startup sequence
 * ================
 * 1. Initialise the file logger.
 * 2. Initialise the ncurses UI layer.
 * 3. Initialise the scheduler (creates worker thread pool).
 * 4. Pre-populate the queue with a handful of demo tasks.
 * 5. Start the UI thread (handles drawing + keyboard input).
 * 6. Main thread waits for the UI thread to signal quit.
 * 7. Graceful shutdown: stop scheduler → join UI thread → destroy logger.
 *
 * Threading model
 * ===============
 *   Main thread      – orchestration only (waits on UI thread)
 *   Worker threads   – execute Task functions from the queue
 *   UI thread        – ncurses rendering + keyboard handling
 *
 * Only the UI thread ever calls ncurses functions.
 */

#include "logger.h"
#include "thread_pool.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* Demo task – simulates a unit of work                                 */
/* ------------------------------------------------------------------ */
typedef struct {
    int work_ms;
    int id;
} DemoArg;

static void demo_task(void *varg)
{
    DemoArg *a = (DemoArg *)varg;
    log_message("[Demo] Starting demo task #%d  (%d ms)", a->id, a->work_ms);

    struct timespec ts = {
        .tv_sec  = a->work_ms / 1000,
        .tv_nsec = (a->work_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    log_message("[Demo] Finished demo task #%d", a->id);
    free(a);
}

/** Convenience wrapper to add a demo task without duplicating malloc logic. */
static void add_demo_task(Scheduler *s, int id, int priority, int work_ms)
{
    DemoArg *arg = (DemoArg *)malloc(sizeof(DemoArg));
    if (!arg) return;
    arg->id      = id;
    arg->work_ms = work_ms;
    scheduler_add_task(s, demo_task, arg, priority);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */
int main(void)
{
    srand((unsigned int)time(NULL));

    /* Step 1 – Initialise file logger (must precede ncurses). */
    log_init();
    log_message("[Main] Scheduler starting up");

    /* Step 2 – Initialise ncurses. */
    ui_init();

    /* Step 3 – Create the scheduler with 4 worker threads. */
    Scheduler sched;
    if (scheduler_init(&sched, 4) != 0) {
        ui_destroy();
        log_message("[Main] Failed to initialise scheduler");
        log_destroy();
        return EXIT_FAILURE;
    }

    /* Step 4 – Pre-populate the queue with demo tasks. */
    add_demo_task(&sched, 1, 5,  800);
    add_demo_task(&sched, 2, 9, 1200);
    add_demo_task(&sched, 3, 3, 2000);
    add_demo_task(&sched, 4, 7,  600);
    add_demo_task(&sched, 5, 1, 1500);
    add_demo_task(&sched, 6, 8,  900);
    add_demo_task(&sched, 7, 6, 1100);
    add_demo_task(&sched, 8, 2,  700);

    log_message("[Main] Queued 8 demo tasks");

    /* Step 5 – Start the UI thread. */
    volatile int quit_flag = 0;
    UIContext ui_ctx = {
        .scheduler = &sched,
        .quit_flag = &quit_flag
    };

    pthread_t ui_thread;
    if (pthread_create(&ui_thread, NULL, ui_thread_func, &ui_ctx) != 0) {
        scheduler_shutdown(&sched);
        ui_destroy();
        log_message("[Main] Failed to start UI thread");
        log_destroy();
        return EXIT_FAILURE;
    }

    /* Step 6 – Wait for the UI thread to set quit_flag and exit. */
    pthread_join(ui_thread, NULL);
    log_message("[Main] UI thread joined, initiating shutdown");

    /* Step 7 – Graceful shutdown. */
    scheduler_shutdown(&sched);
    ui_destroy();

    log_message("[Main] Exiting cleanly");
    log_destroy();

    return EXIT_SUCCESS;
}
