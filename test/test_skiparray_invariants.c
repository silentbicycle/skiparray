#include "test_skiparray.h"
#include "skiparray_internal_types.h"

#include <string.h>
#include <stdio.h>

#define LOG_FILE stdout
#define LOG(LVL, ...)                                                  \
    do {                                                               \
        if (LVL <= verbosity) {                                        \
            fprintf(LOG_FILE, __VA_ARGS__);                            \
        }                                                              \
    } while(0)

#define CHECK(ASSERT, ...)                                             \
    do {                                                               \
        if (!(ASSERT)) {                                               \
            LOG(1, "FAILURE: " __VA_ARGS__);                           \
            return false;                                              \
        }                                                              \
    } while(0)

int test_skiparray_cmp_intptr_t(const void *ka,
    const void *kb, void *udata) {
    (void)udata;
    intptr_t a = (intptr_t)ka;
    intptr_t b = (intptr_t)kb;
    return (a < b ? -1 : a > b ? 1 : 0);
}

bool test_skiparray_invariants(struct skiparray *sa, int verbosity) {
    size_t counts[sa->max_level + 1];
    memset(counts, 0, (1 + sa->max_level) * sizeof(size_t));

    size_t counts_linked[sa->max_level + 1];
    memset(counts_linked, 0, (1 + sa->max_level) * sizeof(size_t));

    LOG(1, "==== Checking invariants\n");

    /* There must always be at least one node on level 0. */
    struct node *cur = sa->nodes[0];
    CHECK(cur != NULL, "No node on level 0\n");

    /* For each progressively taller node encountered, check that it is
     * the first node linked in sa->nodes[] up to its height. */
    uint8_t checked_head_links_up_to = 0;

    size_t actual_pairs = 0;

    /* For every node linked on level 0:
     *
     * - All nodes except the last must have at least node_size/2 keys.
     * - No node can overflow its key buffer.
     * - The last key in a node must be less than the first key in the
     *   next node, if there is one.
     * - Keys within the node are in ascending order. */
    struct node *prev = NULL;
    while (cur) {
        assert(cur);
        struct node *next = cur->fwd[0];
        LOG(2, "-- checking level 0: %p, height %u, %" PRIu16 " pairs, offset %" PRIu16
            " (prev %p, next: %p)\n",
            (void *)cur, cur->height, cur->count, cur->offset,
            (void *)prev, (void *)next);

        actual_pairs += cur->count;

        CHECK(cur->height <= sa->max_level, "node height exceeds max level: %u vs. %u",
            cur->height, sa->max_level);

        counts[cur->height - 1]++;

        for (size_t i = 1; i < cur->height; i++) {
            LOG(2, "    -- fwd[%zu]: %p\n", i, (void *)cur->fwd[i]);
        }

        for (size_t i = 0; i < cur->count; i++) {
            LOG(3, "%zd: %p => %p\n",
                i, (void *)cur->keys[cur->offset + i],
                (void *)cur->values[cur->offset + i]);
        }

        if (cur->height > checked_head_links_up_to) {
            for (size_t i = checked_head_links_up_to + 1; i < cur->height; i++) {
                CHECK(sa->nodes[i] == cur,
                    "Level %d node %p is not the first node linked on level %zd, instead %p is\n",
                    cur->height, (void *)cur, i, (void *)sa->nodes[i]);
            }
            checked_head_links_up_to = cur->height;
        }

        if (prev) {
            CHECK(cur->back == prev,
                "Back pointer mismatch on %p: prev %p, cur->back %p\n",
                (void *)cur, (void *)prev, (void *)cur->back);
            if (cur->count > 0) {
                CHECK(sa->cmp(prev->keys[prev->offset + prev->count - 1],
                        cur->keys[cur->offset], sa->udata) < 0,
                    "Last key in prev node must be less than first key in cur node, prev %p, cur %p\n",
                    (void *)prev, (void *)cur);
            }
        } else {
            CHECK(cur->back == NULL, "First node must have NULL backpointer\n");
        }

        if (next == NULL) {     /* last node */
            if (cur != sa->nodes[0]) {
                CHECK(cur->count > 0, "Only root node can be empty\n");
            }
        } else {                /* not last node */
            CHECK(cur->count >= sa->node_size / 2,
                "Node must be at least half full\n");
        }

        CHECK(cur->count <= sa->node_size, "Cannot have excess keys\n");
        CHECK(cur->offset + cur->count <= sa->node_size,
            "Must not overflow key buffer\n");

        for (size_t i = 1; i < cur->count; i++) {
            CHECK(sa->cmp(cur->keys[cur->offset + i - 1],
                    cur->keys[cur->offset + i],
                    sa->udata) < 0,
                "Node keys must be in ascending order\n");
        }

        prev = cur;
        cur = next;
        counts_linked[0]++;
    }

    /* For each level above zero:
     *
     * - The number of nodes linked must be <= the number of nodes on
     *   the level immediately below it.
     * - The last key in a node must be less than the first key in
     *   the next node on that level. */
    for (size_t li = 1; li < sa->height; li++) {
        cur = sa->nodes[li];
        while (cur) {
            struct node *next = cur->fwd[li];
            LOG(3, "-- counting level %zd: %p, level %u, %" PRIu16
                " pairs, offset %" PRIu16 " (next: %p)\n",
                li, (void *)cur, cur->height, cur->count, cur->offset, (void *)next);
            CHECK(next != cur, "Cycle detected\n");

            if (next != NULL) {
                CHECK(next->height >= li,
                    "Node with height %u should not be linked on level %zd\n",
                    next->height, li);

                CHECK(sa->cmp(cur->keys[cur->offset + cur->count - 1],
                        next->keys[next->offset], sa->udata) < 0,
                    "Last key in node must be less than first key in next node\n");
            }

            cur = next;
            counts_linked[li]++;
        }
    }

    for (size_t li = 1; li < sa->height; li++) {
        LOG(1, "-- level %zd: %zd nodes linked (level %zd: %zd nodes)\n",
            li, counts_linked[li], li, counts[li]);
        size_t level_gte = 0;
        for (size_t i = li; i <= sa->height; i++) { level_gte += counts[i]; }

        CHECK(counts_linked[li] == level_gte,
            "Count mismatch: %zd nodes with level >= %zd, but only %zd linked on level %zd\n",
            level_gte, li, counts_linked[li], li);
    }

    for (size_t li = 1; li <= sa->height; li++) {
        CHECK(counts_linked[li - 1] >= counts_linked[li],
            "Less nodes on level than level above it: %zd vs. %zd\n",
            counts_linked[li - 1], counts_linked[li]);
    }

    size_t count_pairs = skiparray_count(sa);
    CHECK(count_pairs == actual_pairs,
        "pairs don't match: expected %zu, got %zu\n", actual_pairs, count_pairs);

    struct skiparray_iter *iter = NULL;

    if (count_pairs == 0) {
        CHECK(SKIPARRAY_ITER_NEW_EMPTY == skiparray_iter_new(sa, &iter), "iter_new");
    } else {
        CHECK(SKIPARRAY_ITER_NEW_OK == skiparray_iter_new(sa, &iter), "iter_new");
        /* This should be optional -- a new iterator should start at the beginning. */
        skiparray_iter_seek_endpoint(iter, SKIPARRAY_ITER_SEEK_FIRST);

        size_t count_forward = 0;
        void *prev_key = NULL;
        for (;;) {
            void *key;
            void *value;
            skiparray_iter_get(iter, &key, &value);
            count_forward++;

            LOG(3, "%s: count_forward %zu, key %p, prev_key %p\n",
                __func__, count_forward, (void *)key, (void *)prev_key);
            if (count_forward > 1) {
                CHECK(test_skiparray_cmp_intptr_t(prev_key, key, NULL) < 0,
                    "iteration order must be ascending, failed with keys %p and %p\n",
                    (void *)prev_key, (void *)key);
            }

            enum skiparray_iter_step_res step_res = skiparray_iter_next(iter);
            if (step_res == SKIPARRAY_ITER_STEP_END) { break; }
            CHECK(step_res == SKIPARRAY_ITER_STEP_OK, "iteration stepped");
            prev_key = key;
        }
        CHECK(count_forward == count_pairs,
            "forward iteration count mismatch, exp %zu, got %zu\n",
            count_pairs, count_forward);

        skiparray_iter_seek_endpoint(iter, SKIPARRAY_ITER_SEEK_LAST);

        size_t count_backward = 0;
        for (;;) {
            void *key;
            void *value;
            skiparray_iter_get(iter, &key, &value);
            count_backward++;

            if (count_backward > 1) {
                CHECK(test_skiparray_cmp_intptr_t(prev_key, key, NULL) > 0,
                    "reverse iteration order must be descending, failed with keys %p and %p\n",
                    (void *)prev_key, (void *)key);
            }

            enum skiparray_iter_step_res step_res = skiparray_iter_prev(iter);
            if (step_res == SKIPARRAY_ITER_STEP_END) { break; }
            CHECK(step_res == SKIPARRAY_ITER_STEP_OK, "iteration stepped");
            prev_key = key;
        }
        CHECK(count_forward == count_pairs,
            "forward iteration count mismatch, exp %zu, got %zu\n",
            count_pairs, count_forward);

        skiparray_iter_free(iter);
    }

    LOG(1, "==== PASSED\n");
    return true;
}
