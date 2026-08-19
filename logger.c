/*
 * logger.c
 * --------
 * Thread-safe file logger.
 *
 * All public functions are safe to call from multiple threads simultaneously.
 * A dedicated mutex (log_mutex) serialises write operations so that log lines
 * from concurrent threads are never interleaved.
 *
 * During ncurses UI operation, writing to stdout/stderr would corrupt the
 * display.  All diagnostic output therefore goes to "scheduler.log".
 */

#include "logger.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#define LOG_FILE "scheduler.log"

static FILE         *log_fp    = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ */
/* log_init                                                             */
/* ------------------------------------------------------------------ */
void log_init(void)
{
    pthread_mutex_lock(&log_mutex);
    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        /* Fall back to stderr if the file cannot be opened. */
        log_fp = stderr;
    }
    pthread_mutex_unlock(&log_mutex);
}

/* ------------------------------------------------------------------ */
/* log_destroy                                                          */
/* ------------------------------------------------------------------ */
void log_destroy(void)
{
    pthread_mutex_lock(&log_mutex);
    if (log_fp && log_fp != stderr) {
        fclose(log_fp);
        log_fp = NULL;
    }
    pthread_mutex_unlock(&log_mutex);
    pthread_mutex_destroy(&log_mutex);
}

/* ------------------------------------------------------------------ */
/* log_message                                                          */
/* ------------------------------------------------------------------ */
void log_message(const char *fmt, ...)
{
    if (!fmt) return;

    /* Build timestamp string. */
    char   timebuf[32];
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm_info);

    pthread_mutex_lock(&log_mutex);

    if (log_fp) {
        fprintf(log_fp, "[%s] ", timebuf);

        va_list args;
        va_start(args, fmt);
        vfprintf(log_fp, fmt, args);
        va_end(args);

        fprintf(log_fp, "\n");
        fflush(log_fp);   /* Ensure lines appear immediately in the file. */
    }

    pthread_mutex_unlock(&log_mutex);
}
