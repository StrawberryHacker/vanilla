/// Copyright (C) StrawberryHacker

#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "types.h"
#include "list.h"
#include "dlist.h"

#define SYSTICK_RVR 300000
#define THREAD_MAX_NAME_LEN 32

enum sched_class {
    REAL_TIME,
    APPLICATION,
    BACKGROUND,
    IDLE
};

/// Info structure used for initializing new threads
struct thread_info {
    char name[THREAD_MAX_NAME_LEN];

    // Requested stack size in words
    u32 stack_size;

    // Function pointer to the thread 
    void (*thread)(void*);

    // Optional thread argument
    void* arg;

    enum sched_class class;
};

struct rq {
    struct dlist app_rq;
    struct dlist background_rq;
    struct dlist rt_rq;
    
    struct thread* idle;

    struct dlist sleep_q;
    struct dlist blocked_q;

    struct dlist threads;
};

/// Main thread control block
struct thread {
    // The first element in the `tcb` has to be the stack pointer
    u32* stack_pointer;
    u32* stack_base;

    // Runqueue list node
    struct dlist_node rq_node;
    struct dlist_node thread_node;

    // 
    const struct scheduling_class* class;

    // Name of the thread
    char name[THREAD_MAX_NAME_LEN];
    u8 name_len;

    // If the thread is sleeping this value will hold the tick it should wake 
    // on
    u64 tick_to_wake;

    // These are for calulating runtime statistics
    u64 runtime_curr;
    u64 runtime_new;
};

/// Each scheduling class will have its own set of functions defined in this
/// struct. 
struct scheduling_class {
    // Link to the next scheduling class
    const struct scheduling_class* next;

    struct thread* (*pick_thread)(struct rq* rq);
    void           (*enqueue)(struct thread* thread, struct rq* rq);
    void           (*dequeue)(struct thread* thread, struct rq* rq);
};

/// These are the different scheduling classes in the kernel
extern const struct scheduling_class rt_class;
extern const struct scheduling_class app_class;
extern const struct scheduling_class background_class;
extern const struct scheduling_class idle_class;

/// Sets up the interrupts and starts the scheduler
void scheduler_start(void);

void scheduler_enqueue_delay(struct thread* thread);

void reschedule(void);

#endif