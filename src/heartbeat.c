#include "heartbeat.h"
#include "types.h"
#include "work_stealing.h"

#define CACHE_LINE 128

// ============================= THREAD STATE
typedef struct {
    jobref *head, *tail;
} promotable_jobs;

typedef struct {
    char pad_front[CACHE_LINE];
    promotable_jobs pj;
    size_t beat_count;
    char pad_back[CACHE_LINE];
} thread_state;

static _Thread_local thread_state ts;

// ============================= HEARTBEAT
static u64 beat_interval;

bool beat() {
    if (++ts.beat_count >= beat_interval) {
        ts.beat_count = 0;
        return 1;
    }
    return 0;
}

// ============================= PROMOTABLE JOB LIST

/*
 * wires j as the head of the jobs list
 */
void enque(jobref *j) {
    j->next = ts.pj.head;
    j->prev = NULL;
    if (ts.pj.head == NULL) {
        ts.pj.tail = j;
    } else {
        ts.pj.head->prev = j;
    }
    ts.pj.head = j;
}

/*
 * returns the oldest promotable job
 */
jobref *pop_hb() {
    jobref *j = ts.pj.tail;
    if (!j) {
        return NULL;
    }
    ts.pj.tail = j->prev;
    if (ts.pj.tail) {
        ts.pj.tail->next = NULL;
    } else {
        ts.pj.head = NULL;
    }
    return j;
}

// ============================= FORK
/*
 * get oldest promotable job, push to the work stealing deque
 */
void promote() {
    jobref *jr = pop_hb();
    if (jr) {
        push_ws(jr->j);
    }
}

void hb_fork(jobref *j) {
    j->j->done = 0;
    enque(j);
    if (beat()) {
        promote();
    }
}

// ============================= JOIN
void hb_join(jobref *jr) {
    if (ts.pj.head == jr) {
        job *j = ts.pj.head->j;
        ts.pj.head = ts.pj.head->next;

        (j->fn)(j->arg);
        j->done = 1;
        return;
    }
    job *j = jr->j;
    while (!j->done) {
        ws_do_work();
    }
}

// ============================= INIT/SHUTDOWN
void hb_init(size_t nt, u64 interval) {
    beat_interval = interval;
    ws_init(nt);
}

void hb_shutdown() { ws_shutdown(); }
