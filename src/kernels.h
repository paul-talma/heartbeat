#pragma once

#include "tree.h"

struct kconfig {
    size_t nthreads;
    u64 heartbeat;
};

struct kernel {
    const char *name;
    const char *desc;
    u64 (*run)(struct node *root, const struct kconfig *cfg);
    int (*init)(const struct kconfig *cfg);
    void (*fini)(void);
    u64 max_depth;
};

extern const struct kernel kernels[];
extern const int nkernels;

const struct kernel *kernel_find(const char *name);
