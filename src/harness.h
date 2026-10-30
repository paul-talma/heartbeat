#pragma once

#include "kernels.h"

struct tcase {
    char *name;
    char *spec;
    int argc;
    char **argv;
    int has_expect;
    u64 expect;
};

struct hconfig {
    struct kconfig k;
    int reps;
};

int case_init(struct tcase *c, const char *name, int argc, char **argv, const char *basedir);
void case_free(struct tcase *c);
int cases_load(const char *path, struct tcase **out, int *n);
void cases_free(struct tcase *cs, int n);

int harness_run(const struct tcase *cs, int ncases, const struct kernel **ks, int nks,
                const struct hconfig *cfg);
