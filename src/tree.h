#pragma once

#include <stddef.h>

#include "types.h"

struct node {
    u64 val;
    struct node *left;
    struct node *right;
};

struct tree {
    struct node *root;
    struct node *pool;
    u64 nodes;
    u64 depth;
    u64 sum;
};

int tree_build(struct tree *t, int argc, char **argv, char *err, size_t errlen);
void tree_stats(struct tree *t);
void tree_free(struct tree *t);
