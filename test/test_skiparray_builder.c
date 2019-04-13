#include "test_skiparray.h"

static struct skiparray_config config = {
    .cmp = test_skiparray_cmp_intptr_t,
    .node_size = 3,
};

TEST reject_missing_parameters(void) {
    struct skiparray_config bad = {
        .node_size = 2,
    };

    struct skiparray_builder *b = NULL;

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_ERROR_MISUSE,
        skiparray_builder_new(NULL, false, &b), "%d");

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_ERROR_MISUSE,
        skiparray_builder_new(&bad, false, NULL), "%d");

    bad.node_size = 1;
    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_ERROR_MISUSE,
        skiparray_builder_new(&bad, false, &b), "%d");

    PASS();
}

TEST reject_descending_key(void) {
    struct skiparray_builder *b = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_OK,
        skiparray_builder_new(&config, false, &b), "%d");

    uintptr_t k1 = 1;
    uintptr_t k0 = 0;

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_APPEND_OK,
        skiparray_builder_append(b, (void *)k1, NULL), "%d");

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_APPEND_ERROR_MISUSE,
        skiparray_builder_append(b, (void *)k0, NULL), "%d");

    skiparray_builder_free(b, NULL, NULL);
    PASS();
}

TEST reject_equal_key(void) {
    struct skiparray_builder *b = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_OK,
        skiparray_builder_new(&config, false, &b), "%d");

    const uintptr_t k1 = 1;

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_APPEND_OK,
        skiparray_builder_append(b, (void *)k1, NULL), "%d");

    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_APPEND_ERROR_MISUSE,
        skiparray_builder_append(b, (void *)k1, NULL), "%d");

    skiparray_builder_free(b, NULL, NULL);
    PASS();
}

TEST build_ascending(size_t limit) {
    const int verbosity = greatest_get_verbosity();
    struct skiparray_builder *b = NULL;
    ASSERT_EQ_FMT(SKIPARRAY_BUILDER_NEW_OK,
        skiparray_builder_new(&config, false, &b), "%d");

    for (size_t i = 0; i < limit; i++) {
        const uintptr_t k = (uintptr_t)i;
        const uintptr_t v = 2*k + 1;

        ASSERT_EQ_FMT(SKIPARRAY_BUILDER_APPEND_OK,
            skiparray_builder_append(b, (void *)k, (void *)v), "%d");
    }

    struct skiparray *sa = NULL;
    skiparray_builder_finish(&b, &sa);
    ASSERT(sa != NULL);
    ASSERT(test_skiparray_invariants(sa, verbosity - 1));

    for (size_t i = 0; i < limit; i++) {
        const uintptr_t k = (uintptr_t)i;
        const uintptr_t exp = 2*k + 1;
        uintptr_t v;

        ASSERT(skiparray_get(sa, (void *)k, (void **)&v));
        ASSERT_EQ_FMT(exp, v, "%"PRIuPTR);
    }

    skiparray_free(sa, NULL, NULL);
    PASS();
}

SUITE(builder) {
    RUN_TEST(reject_missing_parameters);
    RUN_TEST(reject_descending_key);
    RUN_TEST(reject_equal_key);

    for (size_t i = 10; i <= 100000; i *= 10) {
        if (greatest_get_verbosity() > 0) {
            fprintf(GREATEST_STDOUT, "== %s: tests with i = %zu\n", __func__, i);
        }

        char buf[8];
        if (sizeof(buf) < (size_t)snprintf(buf, sizeof(buf), "%zu", i)) { assert(false); }

        greatest_set_test_suffix(buf);
        RUN_TESTp(build_ascending, i);
    }
}
