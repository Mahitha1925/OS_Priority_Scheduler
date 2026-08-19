/*
 * ui.c
 * ----
 * Terminal UI implementation using ncurses.
 *
 * Layout (top to bottom)
 * ======================
 *
 *  ┌─────────────────────────────────────────────────────────────────┐
 *  │           MULTITHREADED PRIORITY TASK SCHEDULER                 │
 *  ├──────────┬──────────┬──────────┬──────────┬────────────────────┤
 *  │ Threads  │ Pending  │ Running  │ Done     │ [a]=Add [q]=Quit   │
 *  ├──────────┴──────────┴──────────┴──────────┴────────────────────┤
 *  │  ID    Priority  Status       Exec(ms)                         │
 *  │  ...   ...       ...          ...                              │
 *  └─────────────────────────────────────────────────────────────────┘
 *
 * The UI thread acquires a snapshot of scheduler state under the scheduler
 * mutex, then draws without holding the mutex.  This minimises lock contention.
 *
 * ncurses is NOT thread-safe.  All ncurses calls happen exclusively in the
 * UI thread; other threads never call ncurses functions directly.
 */

#include "ui.h"
#include "logger.h"
#include "task_queue.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>

/* Colour pair IDs */
#define CP_TITLE     1
#define CP_HEADER    2
#define CP_WAITING   3
#define CP_RUNNING   4
#define CP_COMPLETED 5
#define CP_KEY       6
#define CP_BORDER    7
#define CP_STAT      8

/* Refresh interval: 500 ms expressed as 500,000 µs */
#define REFRESH_US   500000

/* ------------------------------------------------------------------ */
/* ui_init / ui_destroy                                                 */
/* ------------------------------------------------------------------ */
void ui_init(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);   /* Non-blocking getch()                    */
    curs_set(0);             /* Hide the text cursor                    */

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(CP_TITLE,     COLOR_BLACK,  COLOR_CYAN);
        init_pair(CP_HEADER,    COLOR_BLACK,  COLOR_BLUE);
        init_pair(CP_WAITING,   COLOR_YELLOW, -1);
        init_pair(CP_RUNNING,   COLOR_GREEN,  -1);
        init_pair(CP_COMPLETED, COLOR_CYAN,   -1);
        init_pair(CP_KEY,       COLOR_WHITE,  COLOR_MAGENTA);
        init_pair(CP_BORDER,    COLOR_CYAN,   -1);
        init_pair(CP_STAT,      COLOR_WHITE,  COLOR_BLUE);
    }
}

void ui_destroy(void)
{
    endwin();
}

/* ------------------------------------------------------------------ */
/* Internal drawing helpers                                             */
/* ------------------------------------------------------------------ */

/** Draw a horizontal line of 'ch' characters across the full width. */
static void draw_hline(int row, chtype ch)
{
    int cols = getmaxx(stdscr);
    move(row, 0);
    for (int i = 0; i < cols; i++) addch(ch);
}

/** Draw a centred string in the given colour pair on the given row. */
static void draw_centred(int row, int pair, const char *str)
{
    int cols = getmaxx(stdscr);
    int len  = (int)strlen(str);
    int col  = (cols - len) / 2;
    if (col < 0) col = 0;
    attron(COLOR_PAIR(pair) | A_BOLD);
    mvprintw(row, col, "%s", str);
    attroff(COLOR_PAIR(pair) | A_BOLD);
}

/** Draw the top title bar. */
static void draw_title(void)
{
    int cols = getmaxx(stdscr);
    attron(COLOR_PAIR(CP_TITLE) | A_BOLD);
    mvhline(0, 0, ' ', cols);
    draw_centred(0, CP_TITLE, "  ★  MULTITHREADED PRIORITY TASK SCHEDULER  ★  ");
    attroff(COLOR_PAIR(CP_TITLE) | A_BOLD);
}

/** Draw the statistics bar (row 2). */
static void draw_stats(const SchedulerSnapshot *snap)
{
    int cols = getmaxx(stdscr);

    attron(COLOR_PAIR(CP_STAT) | A_BOLD);
    mvhline(2, 0, ' ', cols);

    mvprintw(2, 2,  "Threads: %d", snap->num_threads);
    mvprintw(2, 18, "Pending: %d", snap->pending);
    mvprintw(2, 34, "Running: %d", snap->running);
    mvprintw(2, 50, "Done: %d",    snap->completed);

    attroff(COLOR_PAIR(CP_STAT) | A_BOLD);

    /* Key hints on the right */
    attron(COLOR_PAIR(CP_KEY) | A_BOLD);
    mvprintw(2, cols - 22, "  [a]=Add  [q]=Quit  ");
    attroff(COLOR_PAIR(CP_KEY) | A_BOLD);
}

/** Draw the column headers for the task table. */
static void draw_table_header(int row)
{
    int cols = getmaxx(stdscr);
    attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
    mvhline(row, 0, ' ', cols);
    mvprintw(row, 1, " %-8s %-10s %-12s %-12s %-30s",
             "ID", "PRIORITY", "STATUS", "EXEC(ms)", "NOTE");
    attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
}

/** Draw a single task row. */
static void draw_task_row(int row, const Task *t)
{
    int pair;
    switch (t->status) {
        case TASK_WAITING:   pair = CP_WAITING;   break;
        case TASK_RUNNING:   pair = CP_RUNNING;   break;
        case TASK_COMPLETED: pair = CP_COMPLETED; break;
        default:             pair = 0;            break;
    }

    char exec_str[16];
    if (t->status == TASK_COMPLETED)
        snprintf(exec_str, sizeof(exec_str), "%8.2f", t->exec_ms);
    else if (t->status == TASK_RUNNING)
        snprintf(exec_str, sizeof(exec_str), "  active");
    else
        snprintf(exec_str, sizeof(exec_str), "       -");

    attron(COLOR_PAIR(pair));
    mvprintw(row, 1, " %-8lu %-10d %-12s %-12s",
             (unsigned long)t->id,
             t->priority,
             TASK_STATUS_STR[t->status],
             exec_str);
    attroff(COLOR_PAIR(pair));
}

/** Draw the status-bar footer. */
static void draw_footer(int row)
{
    int cols = getmaxx(stdscr);
    time_t now = time(NULL);
    char tbuf[32];
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm_info);

    attron(COLOR_PAIR(CP_BORDER) | A_DIM);
    mvhline(row, 0, ACS_HLINE, cols);
    mvprintw(row + 1, 1, "scheduler.log | Time: %s | Press 'q' to quit", tbuf);
    attroff(COLOR_PAIR(CP_BORDER) | A_DIM);
}

/* ------------------------------------------------------------------ */
/* Main draw routine                                                    */
/* ------------------------------------------------------------------ */
static void draw_all(const SchedulerSnapshot *snap)
{
    int rows = getmaxy(stdscr);

    erase();  /* Clear without flicker (uses internal buffer) */

    /* Title bar (row 0) */
    draw_title();

    /* Separator (row 1) */
    attron(COLOR_PAIR(CP_BORDER));
    draw_hline(1, ACS_HLINE);
    attroff(COLOR_PAIR(CP_BORDER));

    /* Stats bar (row 2) */
    draw_stats(snap);

    /* Separator (row 3) */
    attron(COLOR_PAIR(CP_BORDER));
    draw_hline(3, ACS_HLINE);
    attroff(COLOR_PAIR(CP_BORDER));

    /* Table header (row 4) */
    draw_table_header(4);

    /* Task rows (rows 5 … rows-4) */
    int max_rows = rows - 7;   /* Leave room for footer */
    if (max_rows < 1) max_rows = 1;

    int displayed = 0;

    /*
     * We show RUNNING and WAITING tasks first (most important),
     * followed by recently COMPLETED tasks.
     */
    for (int pass = 0; pass < 2 && displayed < max_rows; pass++) {
        for (int i = 0; i < snap->task_count && displayed < max_rows; i++) {
            const Task *t = &snap->tasks[i];
            int show = (pass == 0)
                       ? (t->status == TASK_RUNNING || t->status == TASK_WAITING)
                       : (t->status == TASK_COMPLETED);
            if (!show) continue;
            draw_task_row(5 + displayed, t);
            displayed++;
        }
    }

    if (displayed == 0) {
        attron(A_DIM);
        mvprintw(5, 4, "(No tasks yet – press 'a' to add one)");
        attroff(A_DIM);
    }

    /* Footer */
    draw_footer(rows - 3);

    /* Flush to terminal */
    refresh();
}

/* ------------------------------------------------------------------ */
/* Sample task functions (used when user presses 'a')                  */
/* ------------------------------------------------------------------ */

typedef struct {
    int    work_ms;   /* Simulated work duration in ms */
    int    task_num;  /* For logging only              */
} SampleArg;

static void sample_task(void *varg)
{
    SampleArg *a = (SampleArg *)varg;
    log_message("[Task] sample_task #%d sleeping %d ms", a->task_num, a->work_ms);

    /* Simulate work with nanosleep (no busy-wait). */
    struct timespec ts = {
        .tv_sec  = a->work_ms / 1000,
        .tv_nsec = (a->work_ms % 1000) * 1000000L
    };
    nanosleep(&ts, NULL);

    free(a);
}

/* Counter used to label sample tasks sequentially. */
static int g_task_counter = 0;

/** Add a random-priority task to the scheduler (called on 'a' keypress). */
static void add_random_task(Scheduler *sched)
{
    SampleArg *arg = (SampleArg *)malloc(sizeof(SampleArg));
    if (!arg) return;

    arg->work_ms  = 500 + rand() % 2500;   /* 0.5 – 3.0 s of simulated work */
    arg->task_num = ++g_task_counter;

    int priority  = 1 + rand() % 10;       /* Priority 1–10 */

    uint64_t id = scheduler_add_task(sched, sample_task, arg, priority);
    log_message("[UI] User added task #%lu  priority=%d  work=%d ms",
                (unsigned long)id, priority, arg->work_ms);
}

/* ------------------------------------------------------------------ */
/* UI thread entry point                                                */
/* ------------------------------------------------------------------ */
void *ui_thread_func(void *arg)
{
    UIContext      *ctx   = (UIContext *)arg;
    Scheduler      *sched = ctx->scheduler;
    SchedulerSnapshot snap;

    log_message("[UI] Thread started");

    /*
     * Main UI loop: draw → sleep → poll input → repeat.
     *
     * We sleep in 50 ms increments so that keyboard input feels
     * responsive even though we redraw only every 500 ms.
     */
    const int TOTAL_SLEEP_US  = REFRESH_US;   /* 500 ms per redraw     */
    const int SLICE_US        = 50000;         /* 50 ms input-poll slice */
    const int SLICES_PER_DRAW = TOTAL_SLEEP_US / SLICE_US;

    int slice = 0;

    while (!(*ctx->quit_flag)) {
        /* ---- Poll keyboard ---- */
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            *ctx->quit_flag = 1;
            break;
        }
        if (ch == 'a' || ch == 'A') {
            add_random_task(sched);
        }

        /* ---- Redraw every SLICES_PER_DRAW slices ---- */
        if (slice == 0) {
            scheduler_snapshot(sched, &snap);
            draw_all(&snap);
        }

        slice = (slice + 1) % SLICES_PER_DRAW;

        /* Sleep one slice without busy-waiting. */
        usleep((unsigned int)SLICE_US);
    }

    log_message("[UI] Thread exiting");
    return NULL;
}
