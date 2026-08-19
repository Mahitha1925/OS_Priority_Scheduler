#ifndef LOGGER_H
#define LOGGER_H

/*
 * logger.h
 * --------
 * Thread-safe logging interface.
 *
 * log_message() may be called from any thread.  Internally it acquires
 * its own mutex so log lines are never interleaved.  Messages are written
 * to a rotating log file rather than stdout/stderr because stdout is
 * owned by ncurses during UI operation.
 */

/** Initialise the logger.  Opens (or creates) LOG_FILE for append. */
void log_init(void);

/** Close the log file and destroy the logger mutex. */
void log_destroy(void);

/** Write a formatted log line (thread-safe). */
void log_message(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* LOGGER_H */
