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
// in forkjoin.c

// ============================= FORK
void fj_fork(job *j);

// ============================= JOIN
void fj_join(job *j);

// ============================= WORKER
// in forkjoin.c

// ============================= INIT/SHUTDOWN
void fj_init(size_t nthreads);
void fj_shutdown();
