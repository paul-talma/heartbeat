#pragma once

#include "work_stealing.h"

// ============================= FORK
void fj_fork(job *j);

// ============================= JOIN
void fj_join(job *j);

// ============================= INIT/SHUTDOWN
void fj_init(size_t nthreads);
void fj_shutdown();
