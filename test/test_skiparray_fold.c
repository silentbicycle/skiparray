#include "test_skiparray.h"

static void
sub_key_from_actual(void *key, void *value, void *udata) {
    uintptr_t *actual = udata;
    assert(actual != NULL);
    (void)value;
    (*actual) -= (uintptr_t)key;
}

TEST sub_forward_and_reverse(size_t limit) {
    struct skiparray *sa = test_skiparray_sequential_build(limit);

    /* This uses subtraction because the result will differ when
     * iterating left-to-right and right-to-left. */

    {
        uintptr_t expected = 0;
        for (uintptr_t i = 0; i < limit; i++) {
            expected -= i;    /* note: rollover is fine here */
        }
        uintptr_t acc = 0;

        struct skiparray_fold_state *fs = NULL;
        ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
            skiparray_fold_init(SKIPARRAY_FOLD_LEFT, sa,
                sub_key_from_actual, (void *)&acc, &fs), "%d");

        while (skiparray_fold_next(fs) != SKIPARRAY_FOLD_NEXT_DONE) {
            ;
        }
        ASSERT_EQ_FMT(expected, acc, "%zu");
    }

    {
        assert(limit > 0);
        uintptr_t expected = 0;
        for (uintptr_t i = limit - 1; true; i--) {
            expected -= i;
            if (i == 0) { break; }
        }
        uintptr_t acc = 0;

        struct skiparray_fold_state *fs = NULL;
        ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
            skiparray_fold_init(SKIPARRAY_FOLD_RIGHT, sa,
                sub_key_from_actual, (void *)&acc, &fs), "%d");

        while (skiparray_fold_next(fs) != SKIPARRAY_FOLD_NEXT_DONE) {
            ;
        }
        ASSERT_EQ_FMT(expected, acc, "%zu");
    }

    skiparray_free(sa, NULL, NULL);
    PASS();
}

TEST sub_forward_and_reverse_halt_partway(size_t limit) {
    struct skiparray *sa = test_skiparray_sequential_build(limit);

    /* same as the last test, but stop over iterating over only
     * half of the skiparray:
     * - left: first half
     * - right: last half */
    const size_t steps = limit/2;

    {
        uintptr_t expected = 0;
        for (uintptr_t i = 0; i < steps; i++) {
            expected -= i;
        }
        uintptr_t acc = 0;

        struct skiparray_fold_state *fs = NULL;
        ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
            skiparray_fold_init(SKIPARRAY_FOLD_LEFT, sa,
                sub_key_from_actual, (void *)&acc, &fs), "%d");

        uintptr_t steps_i = 0;
        while (skiparray_fold_next(fs) != SKIPARRAY_FOLD_NEXT_DONE) {
            steps_i++;
            if (steps_i == steps) { break; }
        }
        skiparray_fold_halt(fs);
        ASSERT_EQ_FMT(expected, acc, "%zu");
    }

    {
        assert(limit > 0);
        uintptr_t expected = 0;
        for (uintptr_t i = limit - 1, steps_i = 0; steps_i < steps; i--, steps_i++) {
            expected -= i;
        }
        uintptr_t acc = 0;

        struct skiparray_fold_state *fs = NULL;
        ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
            skiparray_fold_init(SKIPARRAY_FOLD_RIGHT, sa,
                sub_key_from_actual, (void *)&acc, &fs), "%d");

        uintptr_t steps_i = 0;
        while (skiparray_fold_next(fs) != SKIPARRAY_FOLD_NEXT_DONE) {
            steps_i++;
            if (steps_i == steps) { break; }
        }
        skiparray_fold_halt(fs);
        ASSERT_EQ_FMT(expected, acc, "%zu");
    }

    skiparray_free(sa, NULL, NULL);
    PASS();
}

struct multi_env {
    bool ok;
    struct skiparray_builder *b;
};

static void
append_cb(void *key, void *value, void *udata) {
    struct multi_env *env = udata;
    if (env->ok) {
        if (SKIPARRAY_BUILDER_APPEND_OK !=
            skiparray_builder_append(env->b, key, value)) {
            env->ok = false;
        }
    }
}

static uint8_t
merge_cb(uint8_t count, const void **keys, void **values,
    void **merged_value, void *udata) {
    (void)keys;
    (void)values;
    (void)udata;
    assert(count > 0);
    /* always choose the largest value for which key % value is 0 */
    uintptr_t key = (uintptr_t)keys[0];
    uintptr_t out_value = 0;
    for (size_t i = 0; i < count; i++) {
        const uintptr_t v = (uintptr_t)values[i];
        assert(key == (uintptr_t)keys[i]);
        if ((key % v) == 0) {
            if (v > out_value) { out_value = v; }
        }
    }

    *merged_value = (void *)out_value;
    return 0;                   /* all keys are equal */
}

TEST fold_multi_and_check_merge(size_t limit) {
    /* Take N skiplists of 0..limit multiplied by muls[I] and
     * use a multi-fold to zip their values together. */
    const uintptr_t muls[] = { 1, 3, 5 };
#define MUL_CT (sizeof(muls)/sizeof(muls[0]))
    /* this could overflow, but it's not likely with a realistic limit */
    if (muls[MUL_CT - 1] * limit <= limit) { SKIPm("overflow"); }

    struct skiparray_config cfg = {
        .cmp = test_skiparray_cmp_intptr_t
    };

    struct skiparray_builder *builders[MUL_CT] = { NULL };
    for (size_t m_i = 0; m_i < MUL_CT; m_i++) {
        ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_OK,
            skiparray_builder_new(&cfg, false, &builders[m_i]), "%d");
    }

    /* Append ascending keys and multiplied values */
    for (size_t i = 0; i < limit; i++) {
        for (size_t m_i = 0; m_i < MUL_CT; m_i++) {
            uintptr_t key = muls[m_i] * i;
            uintptr_t value = muls[m_i];
            ASSERT_EQ_FMT(SKIPARRAY_BUILDER_APPEND_OK,
                skiparray_builder_append(builders[m_i],
                    (void *)key, (void *)value), "%d");
        }
    }

    /* Finish the builders */
    struct skiparray *sas[MUL_CT];
    for (size_t m_i = 0; m_i < MUL_CT; m_i++) {
        skiparray_builder_finish(&builders[m_i], &sas[m_i]);
        ASSERT(sas[m_i] != NULL);
    }

    struct multi_env env = { .ok = true };

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_OK,
        skiparray_builder_new(&cfg, false, &env.b), "%d");

    /* Use a multi-fold to merge them, passing in a new builder */
    struct skiparray_fold_state *fs = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
        skiparray_fold_multi_init(SKIPARRAY_FOLD_LEFT,
            MUL_CT, sas, append_cb, merge_cb, (void *)&env,
            &fs),
        "%d");

    ASSERT(env.ok);

    /* Step the fold until done */
    do {
    } while (skiparray_fold_next(fs) != SKIPARRAY_FOLD_NEXT_DONE);

    /* Finish the result builder */
    struct skiparray *res = NULL;
    skiparray_builder_finish(&env.b, &res);

    /* Free the merged skiparrays */
    for (size_t m_i = 0; m_i < MUL_CT; m_i++) {
        skiparray_free(sas[m_i], NULL, NULL);
    }

    {
        struct skiparray_iter *iter = NULL;
        ASSERT_EQ_FMT(SKIPARRAY_ITER_NEW_OK,
            skiparray_iter_new(res, &iter), "%d");

        /* Iterate over the merged skiparray, checking that the value
         * is set to the highest value for which key % V is 0. */
        do {
            uintptr_t key;
            uintptr_t value;
            skiparray_iter_get(iter, (void *)&key, (void *)&value);

            for (int m_i = MUL_CT - 1; m_i >= 0; m_i--) {
                if ((key % muls[m_i]) == 0) {
                    ASSERT_EQ_FMT(muls[m_i], value, "%"PRIuPTR);
                    break;
                }
            }
        } while (skiparray_iter_next(iter) != SKIPARRAY_ITER_STEP_END);

        skiparray_iter_free(iter);
    }

    skiparray_free(res, NULL, NULL);
    PASS();
}

static void
sum_values(void *key, void *value, void *udata) {
    uintptr_t *actual = udata;
    assert(actual != NULL);
    (void)key;
    (*actual) += (uintptr_t)value;
}

TEST onepass_sum(size_t limit) {
    struct skiparray *sa = test_skiparray_sequential_build(limit);

    size_t exp = 0;
    for (size_t i = 0; i < limit; i++) { exp += i; }

    size_t actual = 0;
    ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
        skiparray_fold(SKIPARRAY_FOLD_LEFT,
            sa, sum_values, &actual), "%d");
    ASSERT_EQ_FMT(exp, actual, "%zu");

    skiparray_free(sa, NULL, NULL);
    PASS();
}

TEST iter_empty(void) {
    struct skiparray *sa = test_skiparray_sequential_build(0);

    size_t exp = 0;
    size_t actual = 0;
    ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
        skiparray_fold(SKIPARRAY_FOLD_LEFT,
            sa, sum_values, &actual), "%d");
    ASSERT_EQ_FMT(exp, actual, "%zu");

    skiparray_free(sa, NULL, NULL);
    PASS();
}

SUITE(fold) {
    for (size_t limit = 10; limit <= 1000000; limit *= 10) {
        char buf[64];
#define SET_SUFFIX()                                                    \
        snprintf(buf, sizeof(buf), "%zu", limit);                       \
        greatest_set_test_suffix(buf)

        SET_SUFFIX();
        RUN_TESTp(sub_forward_and_reverse, limit);
        SET_SUFFIX();
        RUN_TESTp(sub_forward_and_reverse_halt_partway, limit);
        SET_SUFFIX();
        RUN_TESTp(fold_multi_and_check_merge, limit);
        SET_SUFFIX();
        RUN_TESTp(onepass_sum, limit);
    }

    RUN_TEST(iter_empty);
}
