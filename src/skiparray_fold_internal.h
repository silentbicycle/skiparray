#ifndef SKIPARRAY_FOLD_INTERNAL_H
#define SKIPARRAY_FOLD_INTERNAL_H

#include "skiparray_internal_types.h"

/* #define SKIPARRAY_LOG_FOLD */

enum pair_state {
    PS_NONE,                    /* entry is not currently in use */
    PS_AVAILABLE_LT,            /* entry is < next, or last */
    PS_AVAILABLE_EQ,            /* entry is = next */
};

struct skiparray_fold_state {
    enum skiparray_fold_type type;
    bool use_values;

    struct {
        skiparray_fold_fun *fold;
        void *fold_udata;

        skiparray_cmp_fun *cmp;
        skiparray_free_fun *free;
        skiparray_fold_merge_fun *merge;
        skiparray_memory_fun *mem;
        void *sa_udata;
    } cbs;

    /* Array of iters[] IDs for available pairs, where their keys are in
     * >= order. iters[id].state indicates which keys are equal. */
    struct {
        uint8_t available;          /* IDs available */
        uint8_t offset;             /* offset into IDs, as keys are used */
        uint8_t *current;
    } ids;

    uint8_t iter_count;
    uint8_t iter_live;
    struct iter_state {
        enum pair_state state;       /* state of pair's key */
        struct skiparray_pair pair;
        struct skiparray_iter *iter; /* NULL -> done */
    } iters[];
};

static void
step_active_iterators(struct skiparray_fold_state *fs);

static void
insert_pair(struct skiparray_fold_state *fs, size_t iter_i);

static void
call_with_next(struct skiparray_fold_state *fs, size_t count);

#endif
