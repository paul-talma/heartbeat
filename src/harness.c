#include "harness.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

enum status { RUN_OK, RUN_WRONG, RUN_SKIP, RUN_FAIL };

struct result {
    enum status status;
    double median;
    double min;
    u64 got;
};

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static char *path_join(const char *dir, const char *file) {
    size_t n = strlen(dir) + strlen(file) + 2;
    char *p = xmalloc(n);
    snprintf(p, n, "%s/%s", dir, file);
    return p;
}

static char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash)
        return xstrdup(".");
    size_t n = (size_t)(slash - path);
    char *d = xmalloc(n + 1);
    memcpy(d, path, n);
    d[n] = 0;
    return d;
}

static int split(char *line, char **toks, int max) {
    int n = 0;
    char *p = line;
    while (n < max) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        toks[n++] = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p)
            *p++ = 0;
    }
    return n;
}

int case_init(struct tcase *c,
              const char *name,
              int argc,
              char **argv,
              const char *basedir) {
    memset(c, 0, sizeof *c);
    c->argv = xmalloc((size_t)(argc + 1) * sizeof *c->argv);
    size_t speclen = 1;
    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "expect=", 7)) {
            char *end;
            errno = 0;
            c->expect = strtoull(argv[i] + 7, &end, 10);
            if (errno || end == argv[i] + 7 || *end) {
                free(c->argv);
                return -1;
            }
            c->has_expect = 1;
            continue;
        }
        c->argv[c->argc++] = xstrdup(argv[i]);
        speclen += strlen(argv[i]) + 1;
    }
    c->argv[c->argc] = NULL;

    c->spec = xmalloc(speclen);
    c->spec[0] = 0;
    for (int i = 0; i < c->argc; i++) {
        if (i)
            strcat(c->spec, " ");
        strcat(c->spec, c->argv[i]);
    }
    c->name = xstrdup(name);

    if (basedir && c->argc == 2 && !strcmp(c->argv[0], "file") &&
        c->argv[1][0] != '/') {
        char *full = path_join(basedir, c->argv[1]);
        free(c->argv[1]);
        c->argv[1] = full;
    }
    return 0;
}

void case_free(struct tcase *c) {
    for (int i = 0; i < c->argc; i++)
        free(c->argv[i]);
    free(c->argv);
    free(c->spec);
    free(c->name);
}

void cases_free(struct tcase *cs, int n) {
    for (int i = 0; i < n; i++)
        case_free(&cs[i]);
    free(cs);
}

int cases_load(const char *path, struct tcase **out, int *n) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    char *basedir = dir_of(path);
    int cap = 16, count = 0, lineno = 0, rc = 0;
    struct tcase *cs = xmalloc((size_t)cap * sizeof *cs);
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash)
            *hash = 0;
        char *toks[64];
        int nt = split(line, toks, 64);
        if (nt == 0)
            continue;
        if (nt < 2) {
            fprintf(stderr,
                    "%s:%d: expected NAME KIND [ARGS...] [expect=SUM]\n", path,
                    lineno);
            rc = -1;
            break;
        }
        if (count == cap) {
            cap *= 2;
            cs = xrealloc(cs, (size_t)cap * sizeof *cs);
        }
        if (case_init(&cs[count], toks[0], nt - 1, toks + 1, basedir)) {
            fprintf(stderr, "%s:%d: bad expect= value\n", path, lineno);
            rc = -1;
            break;
        }
        count++;
    }
    fclose(f);
    free(basedir);
    if (rc) {
        cases_free(cs, count);
        return -1;
    }
    *out = cs;
    *n = count;
    return 0;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static void run_kernel(const struct kernel *k,
                       const struct tree *t,
                       const struct hconfig *cfg,
                       double *times,
                       struct result *r) {
    memset(r, 0, sizeof *r);
    if (k->max_depth && t->depth > k->max_depth) {
        r->status = RUN_SKIP;
        return;
    }
    if (k->init && k->init(&cfg->k)) {
        r->status = RUN_FAIL;
        return;
    }
    r->status = RUN_OK;
    u64 got = k->run(t->root, &cfg->k);
    for (int i = 0; got == t->sum && i < cfg->reps; i++) {
        double t0 = now();
        got = k->run(t->root, &cfg->k);
        times[i] = now() - t0;
    }
    if (k->fini)
        k->fini();
    if (got != t->sum) {
        r->status = RUN_WRONG;
        r->got = got;
        return;
    }
    qsort(times, (size_t)cfg->reps, sizeof *times, cmp_double);
    int m = cfg->reps / 2;
    r->median = cfg->reps % 2 ? times[m] : (times[m - 1] + times[m]) / 2;
    r->min = times[0];
}

static void print_row(const struct kernel *k,
                      const struct tree *t,
                      const struct result *r,
                      const struct result *base) {
    printf("  %-12s ", k->name);
    switch (r->status) {
    case RUN_SKIP:
        printf("%10s %10s %8s %8s  skipped: depth %llu > %llu\n", "-", "-", "-",
               "-", (unsigned long long)t->depth,
               (unsigned long long)k->max_depth);
        return;
    case RUN_FAIL:
        printf("%10s %10s %8s %8s  init failed\n", "-", "-", "-", "-");
        return;
    case RUN_WRONG:
        printf("%10s %10s %8s %8s  WRONG: got %llu\n", "-", "-", "-", "-",
               (unsigned long long)r->got);
        return;
    case RUN_OK:
        break;
    }
    printf("%10.3f %10.3f ", r->median * 1e3, r->min * 1e3);
    if (t->nodes)
        printf("%8.2f ", r->median * 1e9 / (double)t->nodes);
    else
        printf("%8s ", "-");
    if (base->status == RUN_OK && r->median > 0)
        printf("%7.2fx", base->median / r->median);
    else
        printf("%8s", "-");
    printf("  ok\n");
}

static void print_summary(const struct tcase *cs,
                          int ncases,
                          const struct kernel **ks,
                          int nks,
                          const struct result *res) {
    int namew = 4;
    for (int i = 0; i < ncases; i++) {
        int l = (int)strlen(cs[i].name);
        if (l > namew)
            namew = l;
    }
    char basehdr[64];
    snprintf(basehdr, sizeof basehdr, "%s ms", ks[0]->name);
    int basew = (int)strlen(basehdr) > 10 ? (int)strlen(basehdr) : 10;

    printf("\n== summary: %s in ms, others as speedup over %s\n", ks[0]->name,
           ks[0]->name);
    printf("%-*s  %*s", namew, "case", basew, basehdr);
    for (int j = 1; j < nks; j++) {
        int w = (int)strlen(ks[j]->name) > 8 ? (int)strlen(ks[j]->name) : 8;
        printf("  %*s", w, ks[j]->name);
    }
    printf("\n");

    static const char *labels[] = {"", "WRONG", "n/a", "FAIL"};
    for (int i = 0; i < ncases; i++) {
        const struct result *base = &res[i * nks];
        printf("%-*s  ", namew, cs[i].name);
        if (base->status == RUN_OK)
            printf("%*.3f", basew, base->median * 1e3);
        else
            printf("%*s", basew, labels[base->status]);
        for (int j = 1; j < nks; j++) {
            const struct result *r = &res[i * nks + j];
            int w = (int)strlen(ks[j]->name) > 8 ? (int)strlen(ks[j]->name) : 8;
            char cell[32];
            if (r->status != RUN_OK)
                snprintf(cell, sizeof cell, "%s", labels[r->status]);
            else if (base->status == RUN_OK && r->median > 0)
                snprintf(cell, sizeof cell, "%.2fx", base->median / r->median);
            else
                snprintf(cell, sizeof cell, "ok");
            printf("  %*s", w, cell);
        }
        printf("\n");
    }
}

int harness_run(const struct tcase *cs,
                int ncases,
                const struct kernel **ks,
                int nks,
                const struct hconfig *cfg) {
    struct result *res = xmalloc((size_t)(ncases * nks) * sizeof *res);
    double *times = xmalloc((size_t)cfg->reps * sizeof *times);
    int bad = 0;

    printf("threads %zu, heartbeat %llu, reps %d (+1 warmup)\n",
           cfg->k.nthreads, (unsigned long long)cfg->k.heartbeat, cfg->reps);

    for (int i = 0; i < ncases; i++) {
        const struct tcase *c = &cs[i];
        struct result *row = &res[i * nks];
        struct tree t;
        char err[512];

        if (strcmp(c->name, c->spec))
            printf("\n== %s: %s\n", c->name, c->spec);
        else
            printf("\n== %s\n", c->spec);
        fflush(stdout);
        double t0 = now();
        if (tree_build(&t, c->argc, c->argv, err, sizeof err)) {
            printf("  build failed: %s\n", err);
            for (int j = 0; j < nks; j++)
                row[j] = (struct result){.status = RUN_FAIL};
            bad = 1;
            continue;
        }
        double built = now() - t0;
        tree_stats(&t);
        printf("  nodes %llu, depth %llu, sum %llu, built in %.0f ms\n",
               (unsigned long long)t.nodes, (unsigned long long)t.depth,
               (unsigned long long)t.sum, built * 1e3);
        if (c->has_expect && c->expect != t.sum) {
            printf("  EXPECTED sum %llu\n", (unsigned long long)c->expect);
            bad = 1;
        }
        printf("  %-12s %10s %10s %8s %8s  %s\n", "kernel", "median ms",
               "min ms", "ns/node", "speedup", "check");
        for (int j = 0; j < nks; j++) {
            run_kernel(ks[j], &t, cfg, times, &row[j]);
            if (row[j].status == RUN_WRONG || row[j].status == RUN_FAIL)
                bad = 1;
            print_row(ks[j], &t, &row[j], &row[0]);
            fflush(stdout);
        }
        tree_free(&t);
    }

    print_summary(cs, ncases, ks, nks, res);
    free(times);
    free(res);
    return bad ? -1 : 0;
}
