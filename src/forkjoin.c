#include "forkjoin.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define DEQUE_SIZE (1 << 10)
#define MAX_THREADS 64

// ============================= DEQUE
/*
 * head points to first job, tail to one past last
 */
typedef struct {
    job *jobs[DEQUE_SIZE];
    size_t head, tail;
    pthread_mutex_t mut;
} deque;

static deque deques[MAX_THREADS];
static pthread_t threads[MAX_THREADS];
static size_t nthreads; // number of threads used
static _Thread_local deque *self;
static _Atomic i32 stop;

/*
 * add a job at the end of the deque
 * if deque is full, errors out
 */
static void push(deque *d, job *j) {
    pthread_mutex_lock(&d->mut);
    if (d->tail == DEQUE_SIZE) {
        printf("Too many jobs on deque\n");
        exit(1);
    }
    d->jobs[d->tail++] = j;
    pthread_mutex_unlock(&d->mut);
}

/*
 * clears the deque
 */
static void reset(deque *d) {
    d->head = 0;
    d->tail = 0;
}

/*
 * return job at the end of the deque, or NULL if deque is empty
 */
static job *pop(deque *d) {
    pthread_mutex_lock(&d->mut);
    if (d->head == d->tail) { // deque empty
        reset(d);
        pthread_mutex_unlock(&d->mut);
        return NULL;
    }
    job *j = d->jobs[--d->tail];
    pthread_mutex_unlock(&d->mut);
    return j;
}

/*
 * take a job from the front of the queue
 * thieves take, owners pop
 * `take` updates the deque's head if it points at an already claimed job
 */
static job *take(deque *d) {
    pthread_mutex_lock(&d->mut);
    if (d->head == d->tail) { // deque empty
        pthread_mutex_unlock(&d->mut);
        return NULL;
    }
    job *j = d->jobs[d->head++];
    if (d->head == d->tail) // reset if we took the last job
        reset(d);
    pthread_mutex_unlock(&d->mut);
    return j;
}

// ============================= FORK
/*
 * pushes a job onto the worker's deque
 */
void fj_fork(job *j) {
    j->done = 0;
    push(self, j);
}

// ============================= WORKER
/*
 * looks for a job in other workers' deques
 */
static job *try_steal() {
    for (size_t i = 0; i < nthreads; ++i) {
        deque *d = &deques[i];
        if (d != self) {
            if (d->head == d->tail)
                continue;
            return take(d);
        }
    }
    return NULL;
}

/*
 * runs and marks a job as done
 */
static void run_job(job *j) {
    (j->fn)(j->arg);
    j->done = 1;
}

/*
 * fetches a job (from own deque or steals if empty) and runs it
 * returns if no job found
 */
static void do_work() {
    job *j;
    j = pop(self);
    if (!j)
        j = try_steal();
    if (j) {
        run_job(j);
    }
}

/*
 * workers pop jobs from their queue or steal from others'
 * until told to stop
 * also initializes `self` deque
 */
void *worker(void *arg) {
    size_t worker_id = (size_t)arg;
    self = &deques[worker_id];
    while (!stop) {
        do_work();
    }
    return NULL;
}

// ============================= JOIN

/*
 * waits for job j to finish
 * if j is unclaimed, runs j
 * otherwise, runs other jobs while waiting
 */
void fj_join(job *j) {
    if (pop(self)) {
        run_job(j);
        return;
    }
    while (!j->done) {
        do_work();
    }
}

// ============================= INIT/SHUTDOWN
/*
 * initializes the fork-join runtime
 * nt: number of threads to use (usually # of machine cores)
 *
 * initializes the worker deques
 * creates a thread per worker
 */
void fj_init(size_t nt) {
    if (64 < nt) {
        printf("-p must be at most 64\n");
        exit(1);
    }
    nthreads = nt;
    stop = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 << 20);
    for (size_t i = 0; i < nt; ++i) {
        pthread_mutex_init(&deques[i].mut, NULL);
    }
    for (size_t i = 1; i < nt; ++i) {
        pthread_create(&threads[i], &attr, worker, (void *)i);
    }
    self = &deques[0];
    pthread_attr_destroy(&attr);
}

/*
 * sets stop flag for workers and joins threads
 */
void fj_shutdown() {
    stop = 1;
    for (size_t i = 1; i < nthreads; ++i) {
        pthread_join(threads[i], NULL);
    }
}
