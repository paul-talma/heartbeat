#include "forkjoin.h"

// ============================= FORK
/*
 * pushes a job onto the worker's deque
 */
void fj_fork(job *j) {
    j->done = 0;
    push_ws(j);
}

// ============================= JOIN

/*
 * waits for job j to finish
 * if j is unclaimed, runs j
 * otherwise, runs other jobs while waiting
 */
void fj_join(job *j) {
    if (pop_ws()) {
        ws_run_job(j);
        return;
    }
    while (!j->done) {
        ws_do_work();
    }
}

// ============================= INIT/SHUTDOWN
void fj_init(size_t nt) { ws_init(nt); }

void fj_shutdown() { ws_shutdown(); }
