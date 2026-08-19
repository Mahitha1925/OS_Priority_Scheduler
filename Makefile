# =============================================================================
# Makefile – Multithreaded Priority Task Scheduler
# =============================================================================
#
# Targets:
#   make          – Build the scheduler binary
#   make clean    – Remove build artefacts
#   make rebuild  – Clean then build
#
# Flags:
#   -Wall -Wextra     – Enable comprehensive compiler warnings
#   -pthread          – Enable POSIX threads (links libpthread, sets macros)
#   -lncurses         – Link against ncurses for the terminal UI
# =============================================================================

CC      := gcc
TARGET  := scheduler
SRCS    := main.c thread_pool.c task_queue.c logger.c ui.c
OBJS    := $(SRCS:.c=.o)

CFLAGS  := -Wall -Wextra -std=c11 -g -O2 -I. \
           -D_POSIX_C_SOURCE=200809L \
           -D_DEFAULT_SOURCE
LDFLAGS := -pthread -lncurses -ltinfo

.PHONY: all clean rebuild

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build successful → ./$(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) scheduler.log
	@echo "Cleaned."

rebuild: clean all
