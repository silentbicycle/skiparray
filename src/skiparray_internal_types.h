#ifndef SKIPARRAY_INTERNAL_TYPES_H
#define SKIPARRAY_INTERNAL_TYPES_H

#include "skiparray.h"

#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

struct skiparray {
    const uint16_t node_size;
    const uint8_t max_level;
    uint8_t height;
    bool use_values;
    uint64_t prng_state;

    skiparray_memory_fun * const mem;
    skiparray_cmp_fun * const cmp;
    skiparray_level_fun * const level;
    void *udata;

    struct skiparray_iter *iter;

    /* Node chains for each level, 0 to max_level, inclusive.
     * Every node is on level 0; a level-1 node will also
     * be linked to nodes[1], etc. */
    struct node *nodes[];
};

struct skiparray_builder {
    struct skiparray *sa;
    struct node *last;
    bool check_ascending;
    bool has_prev_key;
    void *prev_key;

    struct node *trail[];
};

struct node {
    /* How many levels is this node on? >= 1. */
    const uint8_t height;
    uint16_t offset;
    uint16_t count;
    void **keys;
    void **values;
    
    struct node *back;          /* back on level 0 */

    /* Forward pointers. A level 0 node will have 0,
     * at level 0. A level 1 node will have 2,
     * at levels 0 (where all are linked) and 1. Etc. */
    struct node *fwd[];
};

struct skiparray_iter {
    struct skiparray *sa;
    struct skiparray_iter *prev;
    struct skiparray_iter *next;
    struct node *n;
    uint16_t index;
};

struct search_env {
    const struct skiparray *sa;
    const void *key;

    struct node *n;
    uint16_t index;
};

#endif
