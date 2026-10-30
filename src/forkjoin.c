#include "forkjoin.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define DEQUE_SIZE (1 << 16)
#define NTHREADS 64

static void take_and_run();

// ============================= DEQUE
typedef struct {
    job *jobs[DEQUE_SIZE];
    size_t head, tail; // head points to first job, tail to one past last
    pthread_mutex_t mut;
} deque;

static deque *deques[NTHREADS];

static deque d = {
    .head = 0,
    .tail = 0,
    .mut = PTHREAD_MUTEX_INITIALIZER,
};

static void compact(deque *d) {
    size_t id = 0;
    for (size_t i = d->head; i < d->tail; ++i)
        if (d->jobs[i])
            d->jobs[id++] = d->jobs[i];
    d->head = 0;
    d->tail = id;
}

static void push(deque *d, job *j) {
    pthread_mutex_lock(&d->mut);
    if (d->tail == DEQUE_SIZE) {
        compact(d);
        if (d->tail == DEQUE_SIZE) {
            printf("Too many jobs on deque\n");
            exit(1);
        }
    }
    d->jobs[d->tail++] = j;
    pthread_mutex_unlock(&d->mut);
}

// static job *pop(deque *d) {
//     pthread_mutex_lock(&d->mut);
//     if (d->head == d->tail) {
//         pthread_mutex_unlock(&d->mut);
//         return NULL;
//     }
//     job *j = d->jobs[--d->tail];
//     pthread_mutex_unlock(&d->mut);
//     return j;
// }

static void reset(deque *d) {
    d->head = 0;
    d->tail = 0;
}

static job *take(deque *d) {
    pthread_mutex_lock(&d->mut);
    while (d->head < d->tail && !d->jobs[d->head])
        d->head++;
    if (d->head == d->tail) {
        pthread_mutex_unlock(&d->mut);
        return NULL;
    }
    job *j = d->jobs[d->head++];
    if (d->head == d->tail)
        reset(d);
    pthread_mutex_unlock(&d->mut);
    return j;
}

static void clean(deque *d) {
    while (d->head < d->tail && !d->jobs[d->head])
        d->head++;
    while (d->head < d->tail && !d->jobs[d->tail - 1])
        d->tail--;
}

static i32 find(job *j) {
    for (size_t i = d.tail; d.head < i; --i) {
        if (d.jobs[i - 1] == j) {
            return i - 1;
        }
    }
    return -1;
}

// ============================= FORK
void fj_fork(job *j) {
    j->done = 0;
    push(&d, j);
}

// ============================= JOIN
void fj_join(job *j) {
    pthread_mutex_lock(&d.mut);
    i32 id = find(j);
    if (0 <= id) {
        d.jobs[id] = NULL;
        clean(&d);
        pthread_mutex_unlock(&d.mut);
        (j->fn)(j->arg);
        j->done = 1;
    } else {
        pthread_mutex_unlock(&d.mut);
        while (!j->done) {
            take_and_run();
        }
    }
}

// ============================= WORKER
static _Atomic i32 stop;

typedef struct {
    size_t id;
    size_t nt;
    deque *d;
} worker_arg;

static void try_steal(size_t id, size_t nt) {
    for (size_t i = 0; i < nt; ++i) {
        // if i != id
        // try taking from deque i
    }
}

static void take_and_run(deque *d, size_t id, size_t nt) {
    job *j = take(d);
    if (j) {
        (j->fn)(j->arg);
        j->done = 1;
    } else {
        try_steal(id, nt);
    }
}

void *worker(void *arg) {
    worker_arg *a = arg;
    while (!stop) {
        take_and_run(a->d, a->id, a->nt);
    }
    return NULL;
}

// ============================= INIT/SHUTDOWN
static pthread_t threads[NTHREADS];
static size_t nthreads;
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
    for (size_t i = 1; i < nt; ++i) {
        deques[i] = &(deque){
            .head = 0,
            .tail = 0,
            .mut = PTHREAD_MUTEX_INITIALIZER,
        };
        worker_arg a = {
            .id = i,
            .nt = nt,
            .d = deques[i],
        };
        pthread_create(&threads[i], &attr, worker, &a);
    }
    pthread_attr_destroy(&attr);
}

void fj_shutdown() {
    stop = 1;
    for (size_t i = 1; i < nthreads; ++i) {
        pthread_join(threads[i], NULL);
    }
}
