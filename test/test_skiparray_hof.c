#include "test_skiparray.h"

struct parity_env {
    char tag;
    int parity;
    bool ok;
};

static bool
keep_cb(const void *key, const void *value, void *udata) {
    struct parity_env *env = udata;
    uintptr_t k = (uintptr_t)key;
    (void)value;
    assert(env->tag == 'E');
    return (k & env->parity);
}

static void
matches_parity(void *key, void *value, void *udata) {
    struct parity_env *env = udata;
    (void)value;
    assert(env->tag == 'E');
    uintptr_t k = (uintptr_t)key;
    if ((k & 1) != env->parity) { env->ok = false; }
}

TEST filter_odds_or_evens(int parity) {
    struct skiparray *sa = test_skiparray_sequential_build(10);
    ASSERT(sa != NULL);

    struct parity_env env = {
        .tag = 'E',
        .parity = parity,
        .ok = true,
    };

    struct skiparray *filtered = skiparray_filter(sa,
        keep_cb, (void *)&env);
    ASSERT(filtered != NULL);

    ASSERT_EQ_FMT(SKIPARRAY_FOLD_OK,
        skiparray_fold(SKIPARRAY_FOLD_LEFT, filtered,
            matches_parity, (void *)&env), "%d");
    ASSERT(env.ok);

    skiparray_free(sa, NULL, NULL);
    skiparray_free(filtered, NULL, NULL);
    PASS();
}

/* other misc higher-order functions */
SUITE(hof) {
    RUN_TESTp(filter_odds_or_evens, 0);
    RUN_TESTp(filter_odds_or_evens, 1);
}
