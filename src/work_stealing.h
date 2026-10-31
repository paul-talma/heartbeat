#pragma once

#include "types.h"
#include <pthread.h>
#include <stdatomic.h>

// ============================= JOB
typedef struct {
    void (*fn)(void *arg);
    void *arg;
    _Atomic i32 done;
} job;

// ============================= DEQUE
void ws_push(job *j);
job *ws_pop();

// ============================= WORKER
void ws_run_job(job *j);
void ws_do_work();

// ============================= INIT/SHUTDOWN
void ws_init(size_t nthreads);
void ws_shutdown();
