# Multithreaded Priority Task Scheduler


---

## Project Description

This project implements a **multithreaded priority task scheduler** in C.
Tasks with higher priority are always executed before lower-priority tasks.
A fixed-size thread pool processes tasks concurrently, and a live ncurses
dashboard shows the real-time state of the scheduler.

---

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                         main.c                             │
│  • Initialise logger, ncurses, scheduler                   │
│  • Add demo tasks                                          │
│  • Start UI thread                                         │
│  • Join UI thread → trigger shutdown                       │
└──────────────┬──────────────────────────┬──────────────────┘
               │                          │
       ┌───────▼───────┐         ┌────────▼────────┐
       │  Scheduler    │         │   UI Thread      │
       │ (thread_pool) │◄───────►│   (ui.c)         │
       │               │ snapshot│                  │
       │  mutex        │         │  ncurses drawing  │
       │  cond_var     │         │  keyboard input   │
       │  TaskQueue    │         └──────────────────┘
       └───────┬───────┘
               │  dispatch
       ┌───────▼───────────────┐
       │  Worker Threads (×4)  │
       │  • Wait on cond_var   │
       │  • Dequeue task       │
       │  • Execute func(arg)  │
       │  • Record history     │
       └───────────────────────┘
```

---

## Thread Pool Design

- **Fixed size**: 4 worker threads created at startup (`scheduler_init`).
- **Blocking wait**: Workers call `pthread_cond_wait` when the queue is empty,
  consuming zero CPU.
- **Wake-up**: Adding a task calls `pthread_cond_signal` (one worker wakes).
  Shutdown calls `pthread_cond_broadcast` (all workers wake and exit).
- **Graceful drain**: During shutdown, workers finish any currently-executing
  task and then exit cleanly when the queue empties.
- **Join**: `scheduler_shutdown` joins every worker thread before returning,
  guaranteeing no use-after-free or dangling threads.

---

## Producer-Consumer Pattern

```
Producer (UI / scheduler_add_task):
  pthread_mutex_lock(&s->mutex)
  tq_enqueue(...)           ← insert task into priority queue
  pthread_cond_signal(...)  ← wake one sleeping consumer
  pthread_mutex_unlock(&s->mutex)

Consumer (worker thread):
  pthread_mutex_lock(&s->mutex)
  while (queue_empty && !shutdown)
      pthread_cond_wait(...)    ← atomically sleep + release mutex
  task = tq_dequeue(...)
  pthread_mutex_unlock(&s->mutex)
  task->func(task->arg)         ← execute outside the lock
```

The condition variable eliminates busy-waiting completely.

---

## Priority Queue Design

- Implemented as a **sorted singly-linked list** (descending priority).
- `tq_enqueue`: O(n) insert — traverses to find the correct insertion point.
- `tq_dequeue`: O(1) — always removes the head (highest priority task).
- Each `Task` carries: ID, priority, status, function pointer, argument,
  enqueue/start/end timestamps, and computed execution time in milliseconds.

---

## UI ↔ Scheduler Interaction

The UI thread **never** holds the scheduler mutex while performing ncurses
drawing (which could be slow and cause priority inversion):

1. UI thread calls `scheduler_snapshot()` — acquires mutex briefly, copies
   all state into a local `SchedulerSnapshot` struct, releases mutex.
2. UI thread renders the snapshot without holding any lock.
3. This ensures worker threads are never blocked waiting for the UI.

---

## How to Compile

```bash
# Install ncurses development headers if needed:
#   Ubuntu/Debian: sudo apt install libncurses-dev
#   Fedora/RHEL:   sudo dnf install ncurses-devel

cd scheduler/
make
```

---

## How to Run

```bash
./scheduler
```

Press **`a`** to add a random-priority task interactively.  
Press **`q`** to quit (triggers graceful shutdown).

Log output is written to `scheduler.log` in the current directory.

---

## Example Output (Terminal Description)

```
╔══════════════════════════════════════════════════════════════════╗
║       ★  MULTITHREADED PRIORITY TASK SCHEDULER  ★               ║
╠══════════════════════════════════════════════════════════════════╣
║ Threads: 4  │ Pending: 3  │ Running: 4  │ Done: 12              ║
║                                              [a]=Add  [q]=Quit  ║
╠══════════════════════════════════════════════════════════════════╣
║  ID       PRIORITY  STATUS       EXEC(ms)                        ║
║  14       10        RUNNING       active                         ║
║  12       8         RUNNING       active                         ║
║  11       7         WAITING            -                         ║
║  13       5         WAITING            -                         ║
║  9        9         COMPLETED     823.14                         ║
║  10       6         COMPLETED    1204.87                         ║
╚══════════════════════════════════════════════════════════════════╝
scheduler.log | Time: 2025-01-15 14:32:07 | Press 'q' to quit
```

Colour coding:
- **Yellow** – WAITING tasks
- **Green**  – RUNNING tasks  
- **Cyan**   – COMPLETED tasks

---

## Concepts Demonstrated

| Concept | Implementation |
|---------|---------------|
| POSIX Threads | `pthread_create`, `pthread_join` |
| Mutex | `pthread_mutex_t` guards all shared state |
| Condition Variable | `pthread_cond_wait` / `pthread_cond_signal` / `pthread_cond_broadcast` |
| Producer-Consumer | UI adds tasks; workers consume them |
| Priority Queue | Sorted linked list; O(1) dequeue |
| Thread-safe Logging | Dedicated mutex in `logger.c` |
| Graceful Shutdown | Broadcast wake + drain + join |
| Execution Tracking | `CLOCK_MONOTONIC` timestamps |
| ncurses TUI | Real-time dashboard, colour coding, keyboard input |
| Memory Management | Every `malloc`'d task `free`'d after completion |
| Modular Design | Clean separation across 5 source modules |
