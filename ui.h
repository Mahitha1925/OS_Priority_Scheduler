#ifndef UI_H
#define UI_H

/*
 * ui.h
 * ----
 * Terminal UI interface using ncurses.
 *
 * The UI runs in a dedicated thread (ui_thread_func).  It redraws the
 * dashboard every 500 ms and also processes keyboard input.
 *
 * Keyboard controls:
 *   'a' – add a new random task to the scheduler
 *   'q' – initiate graceful shutdown
 */

#include "thread_pool.h"

/* Context passed to the UI thread. */
typedef struct {
    Scheduler *scheduler;        /* Live scheduler to interact with        */
    volatile int *quit_flag;     /* Set to 1 by UI thread to signal quit   */
} UIContext;

/**
 * Entry point for the UI thread.
 * arg must be a pointer to a UIContext struct.
 */
void *ui_thread_func(void *arg);

/**
 * Initialise ncurses and colour pairs.
 * Called once from the main thread before starting the UI thread.
 */
void ui_init(void);

/**
 * Cleanly tear down ncurses.
 * Called after the UI thread has exited.
 */
void ui_destroy(void);

#endif /* UI_H */
