#include "test_skiparray.h"

static struct skiparray *init_with_pairs(size_t limit) {
    const int verbosity = greatest_get_verbosity();
    struct skiparray_config sa_config = {
        .cmp = test_skiparray_cmp_intptr_t,
        .node_size = 5,
    };
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    if (nres != SKIPARRAY_NEW_OK) { return NULL; }

    if (sa != NULL) {
        for (size_t i = 0; i < limit; i++) {
            void *x = (void *)i;
            if (verbosity > 2) {
                fprintf(GREATEST_STDOUT, "==== %s: set %p -> %p\n",
                    __func__, (void *)x, (void *)x);
            }
            if (skiparray_set(sa, x, x) != SKIPARRAY_SET_BOUND) {
                skiparray_free(sa);
                return NULL;
            }

            if (!test_skiparray_invariants(sa, verbosity - 1)) {
                return NULL;
            }
        }
    }

    return sa;
}

TEST set_and_forget_lowest(size_t limit) {
    struct skiparray *sa = init_with_pairs(limit);
    ASSERT(sa);

    const int verbosity = greatest_get_verbosity();
    if (verbosity > 0) {
        fprintf(GREATEST_STDOUT, "==== %s(%zd)\n", __func__, limit);
    }
    ASSERT(test_skiparray_invariants(sa, verbosity - 1));

    for (size_t i = 0; i < limit; i++) {
        if (verbosity > 1) { fprintf(GREATEST_STDOUT, "-- forgetting %zu\n", i); }

        struct skiparray_pair pair;
        enum skiparray_forget_res res = skiparray_forget(sa,
            (void *)i, &pair);
        ASSERT_EQ_FMT(SKIPARRAY_FORGET_OK, res, "%d");
        ASSERT_EQ_FMT(i, (size_t)pair.value, "%zd");
        ASSERT_EQ_FMT(i, (size_t)pair.key, "%zd");

        ASSERT(test_skiparray_invariants(sa, verbosity - 1));
    }

    skiparray_free(sa);
    PASS();
}

TEST set_and_forget_highest(size_t limit) {
    struct skiparray *sa = init_with_pairs(limit);
    ASSERT(sa);

    const int verbosity = greatest_get_verbosity();
    if (verbosity > 0) {
        fprintf(GREATEST_STDOUT, "==== %s(%zd)\n", __func__, limit);
    }
    ASSERT(test_skiparray_invariants(sa, verbosity > 1));

    for (intptr_t i = limit - 1; i >= 0; i--) {
        struct skiparray_pair pair;
        enum skiparray_forget_res res = skiparray_forget(sa,
            (void *)i, &pair);
        ASSERT_EQ_FMT(SKIPARRAY_FORGET_OK, res, "%d");
        ASSERT_EQ_FMT((uintptr_t)i, (uintptr_t)pair.value, "%"PRIuPTR);

        ASSERT(test_skiparray_invariants(sa, verbosity > 1));
        if (i == 0) { break; }
    }

    skiparray_free(sa);
    PASS();
}

TEST set_and_forget_interleaved(size_t limit) {
    const int verbosity = greatest_get_verbosity();
    struct skiparray_config sa_config = {
        .cmp = test_skiparray_cmp_intptr_t,
        .node_size = 5,
    };
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    ASSERT_EQ_FMT(SKIPARRAY_NEW_OK, nres, "%d");
    ASSERT(sa != NULL);

    for (size_t i = 0; i < limit; i++) {
        void *x = (void *)i;
        if (verbosity > 2) {
            fprintf(GREATEST_STDOUT, "==== %s: set %p -> %p\n",
                __func__, (void *)x, (void *)x);
        }
        if (skiparray_set(sa, x, x) != SKIPARRAY_SET_BOUND) {
            skiparray_free(sa);
            FAILm("set failure");
        }

        struct skiparray_pair pair;
        enum skiparray_forget_res res = skiparray_forget(sa,
            (void *)i, &pair);
        ASSERT_EQ_FMT(SKIPARRAY_FORGET_OK, res, "%d");
        ASSERT_EQ_FMT((uintptr_t)i, (uintptr_t)pair.value, "%"PRIuPTR);

        ASSERT(test_skiparray_invariants(sa, verbosity - 1));
    }

    skiparray_free(sa);
    PASS();
}

TEST set_and_pop_first(size_t limit) {
    struct skiparray *sa = init_with_pairs(limit);
    ASSERT(sa);

    const int verbosity = greatest_get_verbosity();
    if (verbosity > 0) {
        fprintf(GREATEST_STDOUT, "==== %s(%zd)\n", __func__, limit);
    }
    ASSERT(test_skiparray_invariants(sa, verbosity > 1));

    for (size_t i = 0; i < limit; i++) {
        intptr_t k = 0;
        intptr_t v = 0;

        enum skiparray_pop_res res = skiparray_pop_first(sa,
            (void *)&k, (void *)&v);
        ASSERT_EQ_FMT(SKIPARRAY_POP_OK, res, "%d");
        ASSERT_EQ_FMT(i, (size_t)k, "%zd");

        ASSERT(test_skiparray_invariants(sa, verbosity > 1));
    }

    skiparray_free(sa);
    PASS();
}

TEST set_and_pop_last(size_t limit) {
    struct skiparray *sa = init_with_pairs(limit);
    ASSERT(sa);

    const int verbosity = greatest_get_verbosity();
    if (verbosity > 0) {
        fprintf(GREATEST_STDOUT, "==== %s(%zd)\n", __func__, limit);
    }
    ASSERT(test_skiparray_invariants(sa, verbosity > 1));

    for (size_t i = 0; i < limit; i++) {
        intptr_t k = 0;
        intptr_t v = 0;

        enum skiparray_pop_res res = skiparray_pop_last(sa,
            (void *)&k, (void *)&v);
        ASSERT_EQ_FMT(SKIPARRAY_POP_OK, res, "%d");
        ASSERT_EQ_FMT(limit - i - 1, (size_t)k, "%zd");

        ASSERT(test_skiparray_invariants(sa, verbosity > 1));
    }

    skiparray_free(sa);
    PASS();
}

bool skiparray_bsearch(void *key, const void **keys,
    size_t key_count, skiparray_cmp_fun *cmp, void *udata,
    uint16_t *index);

TEST binary_search(void) {
    #define MAX_SIZE 16
    intptr_t keys[MAX_SIZE + 1];
    int verbosity = greatest_get_verbosity();
    for (uint16_t size = 1; size <= MAX_SIZE; size++) {
        for (int present = 1; present >= 0; present--) {
            for (uintptr_t needle = 0; needle < size; needle++) {
                for (size_t i = 0; i < size; i++) {
                    keys[i] = i;
                    if (!present && i >= needle) { keys[i] += 1; }
                    if (verbosity > 0) {
                        printf(" == %zd: %"PRIdPTR "\n", i, keys[i]);
                    }
                }

                uint16_t index = (uint16_t)-1;
                bool found = skiparray_bsearch((void *)needle,
                    (void *)keys, size,
                    test_skiparray_cmp_intptr_t, NULL,
                    &index);
                if (verbosity > 0) {
                    printf("size %u, needle %"PRIuPTR", present %d ==> found %d, index %u\n\n",
                        size, needle, present, found, index);
                }
                ASSERT_EQ(present, found);
                ASSERT_EQ_FMT(needle, (uintptr_t)index, "%"PRIuPTR);
            }
        }
    }
    PASS();
}

TEST iteration_locks_collection(bool free_newest_first) {
    struct skiparray_config sa_config = {
        .cmp = test_skiparray_cmp_intptr_t,
    };
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    ASSERT_EQ_FMT(SKIPARRAY_NEW_OK, nres, "%d");
    ASSERT(sa != NULL);

    void *x = (void *)23;
    ASSERT_EQ_FMT(SKIPARRAY_SET_BOUND,
        skiparray_set(sa, x, x), "%d");

    struct skiparray_iter *iter = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_ITER_NEW_OK,
        skiparray_iter_new(sa, &iter), "%d");

    /* Allocating an iterator locks the collection. */

    void *k;
    void *v;

    ASSERT_EQ_FMT(SKIPARRAY_SET_ERROR_LOCKED,
        skiparray_set(sa, x, x), "%d");
    ASSERT_EQ_FMT(SKIPARRAY_FORGET_ERROR_LOCKED,
        skiparray_forget(sa, x, NULL), "%d");

    ASSERT_EQ_FMT(SKIPARRAY_POP_ERROR_LOCKED,
        skiparray_pop_first(sa, &k, &v), "%d");
    ASSERT_EQ_FMT(SKIPARRAY_POP_ERROR_LOCKED,
        skiparray_pop_last(sa, &k, &v), "%d");

    /* Allocate another iterator, then verify that it's still locked. */
    struct skiparray_iter *iter2 = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_ITER_NEW_OK,
        skiparray_iter_new(sa, &iter2), "%d");

    /* Free one, according to the arg, and verify that it's still locked */
    if (free_newest_first) {
        skiparray_iter_free(iter2);
    } else {
        skiparray_iter_free(iter);
    }

    ASSERT_EQ_FMT(SKIPARRAY_SET_ERROR_LOCKED,
        skiparray_set(sa, x, x), "%d");
    ASSERT_EQ_FMT(SKIPARRAY_FORGET_ERROR_LOCKED,
        skiparray_forget(sa, x, NULL), "%d");

    ASSERT_EQ_FMT(SKIPARRAY_POP_ERROR_LOCKED,
        skiparray_pop_first(sa, &k, &v), "%d");
    ASSERT_EQ_FMT(SKIPARRAY_POP_ERROR_LOCKED,
        skiparray_pop_last(sa, &k, &v), "%d");

    /* After the last iterator is freed, the collection should unlock. */
    if (free_newest_first) {
        skiparray_iter_free(iter);
    } else {
        skiparray_iter_free(iter2);
    }

    k = (void *)12345;
    enum skiparray_set_res sres = skiparray_set(sa, k, x);
    ASSERT_EQ_FMT(SKIPARRAY_SET_BOUND, sres, "%d");

    ASSERT_EQ_FMT(SKIPARRAY_FORGET_OK,
        skiparray_forget(sa, k, NULL), "%d");

    /* add it back, so it can then be popped off */
    sres = skiparray_set(sa, k, x);
    ASSERT_EQ_FMT(SKIPARRAY_SET_BOUND, sres, "%d");

    enum skiparray_pop_res pres = skiparray_pop_first(sa, &k, &v);
    ASSERT_EQ_FMT(SKIPARRAY_POP_OK, pres, "%d");
    ASSERT_EQ_FMT((uintptr_t)23, (uintptr_t)k, "%zu");
    ASSERT_EQ_FMT((uintptr_t)23, (uintptr_t)v, "%zu");

    pres = skiparray_pop_last(sa, &k, &v);
    ASSERT_EQ_FMT(SKIPARRAY_POP_OK, pres, "%d");
    ASSERT_EQ_FMT((uintptr_t)12345, (uintptr_t)k, "%zu");
    ASSERT_EQ_FMT((uintptr_t)23, (uintptr_t)v, "%zu");

    skiparray_free(sa);
    PASS();
}

TEST iteration(void) {
    int verbosity = greatest_get_verbosity();
    struct skiparray_config sa_config = {
        .cmp = test_skiparray_cmp_intptr_t,
        .node_size = 5,
    };
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    ASSERT_EQ_FMT(SKIPARRAY_NEW_OK, nres, "%d");
    ASSERT(sa != NULL);

    /* set bindings from 100 to 9900 */
    for (size_t i = 1; i < 100; i++) {
        if (verbosity > 0) {
            fprintf(GREATEST_STDOUT, "%s: binding %zu (0x%"PRIxPTR") -> %zu (0x%"PRIxPTR")\n",
                __func__, 100 * i, (uintptr_t)(100 * i),
                (100 * i) + 1, (uintptr_t)((100 * i) + 1));
        }
        void *x = (void *)(100 * i);
        ASSERT_EQ_FMT(SKIPARRAY_SET_BOUND,
            skiparray_set(sa, x, (void *)((uintptr_t)x + 1)), "%d");
    };

    struct skiparray_iter *iter = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_ITER_NEW_OK,
        skiparray_iter_new(sa, &iter), "%d");

    /* allocate more iterators, to test that they get cleaned up properly */
    for (size_t i = 0; i < 10; i++) {
        struct skiparray_iter *extra_iter = NULL;
        ASSERT_EQ_FMT(SKIPARRAY_ITER_NEW_OK,
            skiparray_iter_new(sa, &extra_iter), "%d");
        ASSERT(extra_iter != NULL);
    }

#define GET_AND_CHECK(EXP_KEY, EXP_VALUE)                               \
    do {                                                                \
        void *k;                                                \
        void *v;                                              \
        skiparray_iter_get(iter, &k, &v);                               \
        ASSERT_EQ_FMT((uintptr_t)EXP_KEY, (uintptr_t)k, "%"PRIuPTR);    \
        ASSERT_EQ_FMT((uintptr_t)EXP_VALUE, (uintptr_t)v, "%"PRIuPTR);  \
    } while (0)

    skiparray_iter_seek_endpoint(iter, SKIPARRAY_ITER_SEEK_LAST);
    GET_AND_CHECK(9900, 9901);

    skiparray_iter_seek_endpoint(iter, SKIPARRAY_ITER_SEEK_FIRST);
    GET_AND_CHECK(100, 101);

    /* seek to a present value */
    enum skiparray_iter_seek_res sres =
      skiparray_iter_seek(iter, (void *)5000);
    ASSERT_EQ_FMT(SKIPARRAY_ITER_SEEK_FOUND, sres, "%d");
    GET_AND_CHECK(5000, 5001);

    enum skiparray_iter_step_res step_res;
    step_res = skiparray_iter_next(iter);
    ASSERT_EQ_FMT(SKIPARRAY_ITER_STEP_OK, step_res, "%d");
    GET_AND_CHECK(5100, 5101);

    step_res = skiparray_iter_next(iter);
    ASSERT_EQ_FMT(SKIPARRAY_ITER_STEP_OK, step_res, "%d");
    GET_AND_CHECK(5200, 5201);

    step_res = skiparray_iter_prev(iter);
    ASSERT_EQ_FMT(SKIPARRAY_ITER_STEP_OK, step_res, "%d");
    GET_AND_CHECK(5100, 5101);

    ASSERT(test_skiparray_invariants(sa, verbosity));

    /* seek to a nonexistent value */
    sres = skiparray_iter_seek(iter, (void *)1234);
    ASSERT_EQ_FMT(SKIPARRAY_ITER_SEEK_NOT_FOUND, sres, "%d");
    GET_AND_CHECK(1300, 1301);

    /* try seeking to all entries and check the next */
    for (size_t i = 0; i < 10000; i++) {
        sres = skiparray_iter_seek(iter, (void *)i);
        const bool present = (i % 100) == 0;
        if (i < 100) {
            ASSERT_EQ_FMT(SKIPARRAY_ITER_SEEK_ERROR_BEFORE_FIRST, sres, "%d");
        } else if (i > 9900) {
            ASSERT_EQ_FMT(SKIPARRAY_ITER_SEEK_ERROR_AFTER_LAST, sres, "%d");
        } else {
            ASSERT_EQ_FMT(present
                ? SKIPARRAY_ITER_SEEK_FOUND
                : SKIPARRAY_ITER_SEEK_NOT_FOUND,
                sres, "%d");
            void *exp_k = (void *)(i - (i % 100) + (present ? 0 : 100));
            GET_AND_CHECK(exp_k, ((uintptr_t)exp_k + 1));
        }
    }

    /* freeing the skiparray should also free any pending iterators */
    skiparray_free(sa);
    PASS();
}

SUITE(basic) {
    RUN_TEST(binary_search);
    RUN_TESTp(iteration_locks_collection, false);
    RUN_TESTp(iteration_locks_collection, true);
    RUN_TEST(iteration);

    for (size_t i = 10; i <= 10000; i *= 10) {
        if (greatest_get_verbosity() > 0) {
            fprintf(GREATEST_STDOUT, "== %s: tests with i = %zu\n", __func__, i);
        }

        char buf[8];
        if (sizeof(buf) < (size_t)snprintf(buf, sizeof(buf), "%zu", i)) { assert(false); }

        greatest_set_test_suffix(buf);
        RUN_TESTp(set_and_forget_lowest, i);

        greatest_set_test_suffix(buf);
        RUN_TESTp(set_and_forget_interleaved, i);

        greatest_set_test_suffix(buf);
        RUN_TESTp(set_and_forget_highest, i);

        greatest_set_test_suffix(buf);
        RUN_TESTp(set_and_pop_first, i);

        greatest_set_test_suffix(buf);
        RUN_TESTp(set_and_pop_last, i);
    }
}
