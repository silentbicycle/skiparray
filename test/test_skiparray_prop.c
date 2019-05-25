#include "test_skiparray.h"

#define LOG(...)                                                       \
    do {                                                               \
        if (m->env->verbosity > 0) {                                   \
            printf(__VA_ARGS__);                                       \
        }                                                              \
    } while(0)

#define LOG_FAIL(...)                                                  \
    do {                                                               \
        if (m->env->verbosity > 0) {                                   \
            printf(__VA_ARGS__);                                       \
        }                                                              \
        return false;                                                  \
    } while(0)

static enum theft_trial_res
prop_preserve_invariants(struct theft *t, void *arg1);
static bool evaluate(struct op *op, struct model *m);
static bool eval_get(struct op *op, struct model *m);
static bool eval_set(struct op *op, struct model *m);
static bool eval_pop_first(struct op *op, struct model *m);
static bool eval_pop_last(struct op *op, struct model *m);
static bool eval_forget(struct op *op, struct model *m);
static bool eval_member(struct op *op, struct model *m);
static bool eval_count(struct op *op, struct model *m);
static bool eval_first(struct op *op, struct model *m);
static bool eval_last(struct op *op, struct model *m);

static bool validate(struct model *m);

/* Bump up verbosity and re-run once on failure. */
static enum theft_hook_trial_post_res
trial_post_cb(const struct theft_hook_trial_post_info *info,
        void *hook_env) {
    struct test_env *env = (struct test_env *)hook_env;
    assert(env->tag == 'T');

    /* run failures once more with logging increased */
    if (info->result == THEFT_TRIAL_FAIL) {
        env->verbosity = 1;
        return THEFT_HOOK_TRIAL_POST_REPEAT_ONCE;
    }

    theft_print_trial_result(&env->print_env, info);

    return THEFT_HOOK_TRIAL_POST_CONTINUE;
}

TEST preserve_invariants(size_t limit, theft_seed seed) {
    size_t trials = 1;
    if (seed == 0) {
        seed = theft_seed_of_time();
        trials = 100;
    }

    struct test_env env = {
        .tag = 'T',
        .limit = limit,
    };

    char name[64];
    snprintf(name, sizeof(name), "%s(%zd)", __func__, limit);

    struct theft_run_config config = {
        .name = name,
        .prop1 = prop_preserve_invariants,
        .type_info = { &type_info_skiparray_operations },
        .seed = seed,
        .trials = trials,

        .hooks = {
            .trial_pre = theft_hook_first_fail_halt,
            .trial_post = trial_post_cb,
            .env = &env,
        },
    };

    if (!getenv("NOFORK")) {
        config.fork.enable = true;
    }

    ASSERT_ENUM_EQ(THEFT_RUN_PASS, theft_run(&config), theft_run_res_str);
    PASS();
}

static enum theft_trial_res
prop_preserve_invariants(struct theft *t, void *arg1) {
    struct scenario *scen = arg1;
    struct test_env *env = theft_hook_get_env(t);

    const size_t m_alloc_size = sizeof(struct model)
      + scen->count * sizeof(struct pair);
    struct model *m = malloc(m_alloc_size);
    if (m == NULL) { return THEFT_TRIAL_ERROR; }
    memset(m, 0x00, m_alloc_size);
    m->tag = 'M';
    m->env = env;

    struct skiparray_config sa_config = {
        .cmp = test_skiparray_cmp_intptr_t,
        .seed = scen->seed,
        .node_size = scen->node_size,
    };
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    if (SKIPARRAY_NEW_OK != nres) {
        free(m);
        fprintf(stderr, "skiparray_new %d\n", nres);
        return THEFT_TRIAL_ERROR;
    }
    m->sa = sa;

    for (size_t i = 0; i < scen->count; i++) {
        struct op *op = &scen->ops[i];
        LOG("== evaluate: %zd\n", i);
        if (!evaluate(op, m)) { goto fail; }

        if (!validate(m)) { goto fail; }
    }

    skiparray_free(sa);
    free(m);
    return THEFT_TRIAL_PASS;

fail:
    free(m);
    return THEFT_TRIAL_FAIL;
}

static bool evaluate(struct op *op, struct model *m) {
    switch (op->t) {
    case OP_GET:
        if (!eval_get(op, m)) { return false; }
        break;
    case OP_SET:
        if (!eval_set(op, m)) { return false; }
        break;
    case OP_FORGET:
        if (!eval_forget(op, m)) { return false; }
        break;
    case OP_POP_FIRST:
        if (!eval_pop_first(op, m)) { return false; }
        break;
    case OP_POP_LAST:
        if (!eval_pop_last(op, m)) { return false; }
        break;
    case OP_MEMBER:
        if (!eval_member(op, m)) { return false; }
        break;
    case OP_COUNT:
        if (!eval_count(op, m)) { return false; }
        break;
    case OP_FIRST:
        if (!eval_first(op, m)) { return false; }
        break;
    case OP_LAST:
        if (!eval_last(op, m)) { return false; }
        break;

    default:
    case OP_TYPE_COUNT:
        assert(false);
    }

    if (!test_skiparray_invariants(m->sa, m->env->verbosity)) {
        return false;
    }

    return true;
}

static bool check_if_known(struct model *m, intptr_t key, size_t *found_i) {
    skiparray_cmp_fun *cmp = test_skiparray_cmp_intptr_t;
    /* check if known in model */
    for (size_t i = 0; i < m->pairs_used; i++) {
        if (0 == cmp(m->pairs[i].key, (void *)key, NULL)) {
            *found_i = i;
            return true;
        }
    }
    return false;
}

static bool eval_get(struct op *op, struct model *m) {
    bool found = false;
    size_t found_i = 0;

    found = check_if_known(m, op->u.get.key, &found_i);

    void *v = 0;
    bool res = skiparray_get(m->sa, (void *)op->u.get.key, &v);
    if (found) {
        if (!res) {
            LOG_FAIL("GET: lost binding\n");
        }
        if (m->pairs[found_i].value != v) {
            LOG_FAIL("GET: wrong key -- exp %p, got %p\n",
                    m->pairs[found_i].value, v);
        }
    } else {
        if (res) {
            LOG_FAIL("GET: found unexpected binding; %p -> %p\n",
                (void *)op->u.get.key, v);
        }
    }
    return true;
}

static bool eval_set(struct op *op, struct model *m) {
    size_t found_i = 0;
    bool found = check_if_known(m, op->u.get.key, &found_i);

    struct skiparray_pair pair;
    enum skiparray_set_res res = skiparray_set_with_pair(m->sa,
        (void *)op->u.set.key, (void *)op->u.set.value,
        true, &pair);

    if (found) {
        if (res != SKIPARRAY_SET_REPLACED) {
            LOG_FAIL("SET: expected res REPLACED (%d), got %d\n",
                SKIPARRAY_SET_REPLACED, res);
        }
        if (pair.value != m->pairs[found_i].value) {
            LOG_FAIL("SET: bad old value, expected %p, got %p\n",
                m->pairs[found_i].value, pair.value);
        }

        /* update model */
        m->pairs[found_i].value = (void *)op->u.set.value;
    } else {
        if (res != SKIPARRAY_SET_BOUND) {
            LOG_FAIL("SET: expected res BOUND (%d), got %d\n",
                SKIPARRAY_SET_BOUND, res);
        }
        m->pairs[m->pairs_used].key = (void *)op->u.set.key;
        m->pairs[m->pairs_used].value = (void *)op->u.set.value;
        m->pairs_used++;      /* update model */
    }

    void *nvalue = 0;
    if (!skiparray_get(m->sa, (void *)op->u.set.key, &nvalue)) {
        LOG_FAIL("SET: bound value not found: %p\n",
            (void *)op->u.set.key);
    }
    if (nvalue != (void *)op->u.set.value) {
        LOG_FAIL("SET: get after read incorrect value: %p\n", nvalue);
    }

    return true;
}

static bool eval_forget(struct op *op, struct model *m) {
    size_t found_i = 0;
    bool found = check_if_known(m, op->u.get.key, &found_i);

    struct skiparray_pair pair;
    enum skiparray_forget_res res = skiparray_forget(m->sa,
        (void *)op->u.forget.key, &pair);

    if (found) {
        if (res != SKIPARRAY_FORGET_OK) {
            LOG_FAIL("FORGET: did not forget present value: %d\n", res);
        }

        if (pair.key != m->pairs[found_i].key) {
            LOG_FAIL("FORGET: removed unexpected key\n");
        }
        if (pair.value != m->pairs[found_i].value) {
            LOG_FAIL("FORGET: removed unexpected value\n");
        }

        /* remove from model */
        if (m->pairs_used > 1 && (found_i != m->pairs_used - 1)) {
            m->pairs[found_i].key = m->pairs[m->pairs_used - 1].key;
            m->pairs[found_i].value = m->pairs[m->pairs_used - 1].value;
        }
        m->pairs_used--;
    } else {
        if (res != SKIPARRAY_FORGET_NOT_FOUND) {
            LOG_FAIL("FORGET: instead of NOT FOUND, got %d\n", res);
        }
    }

    void *nvalue = 0;
    if (skiparray_get(m->sa, (void *)op->u.set.key, &nvalue)) {
        return false;
    }

    return true;
}

static bool eval_pop_first(struct op *op, struct model *m) {
    (void)op;
    void *key = 0;
    void *value = 0;
    enum skiparray_pop_res res = skiparray_pop_first(m->sa, &key, &value);
    if (m->pairs_used == 0) {
        if (res != SKIPARRAY_POP_EMPTY) {
            LOG_FAIL("POP_FIRST: expected EMPTY\n");
        }
        return true;
    } else {
        void *min_key = m->pairs[0].key;
        void *exp_value = m->pairs[0].value;
        size_t match_i = 0;
        for (size_t i = 1; i < m->pairs_used; i++) {
            if (m->pairs[i].key < min_key) {
                min_key = m->pairs[i].key;
                exp_value = m->pairs[i].value;
                match_i = i;
            }
        }
        if (key != min_key) {
            LOG_FAIL("POP_FIRST: not min key (exp %p, got %p)\n",
                min_key, key);
        }
        if (value != exp_value) {
            LOG_FAIL("POP_FIRST: not min key's value (%p), got %p\n",
                (void *)exp_value, (void *)value);
        }

        if (match_i < m->pairs_used - 1) {
            m->pairs[match_i].key = m->pairs[m->pairs_used - 1].key;
            m->pairs[match_i].value = m->pairs[m->pairs_used - 1].value;
        }
        m->pairs_used--;
        return true;
    }
}

static bool eval_pop_last(struct op *op, struct model *m) {
    (void)op;
    void *key = 0;
    void *value = 0;
    enum skiparray_pop_res res = skiparray_pop_last(m->sa, &key, &value);
    if (m->pairs_used == 0) {
        if (res != SKIPARRAY_POP_EMPTY) {
            LOG_FAIL("POP_LAST: expected EMPTY\n");
        }
        return true;
    } else {
        void *max_key = m->pairs[0].key;
        void *exp_value = m->pairs[0].value;
        size_t match_i = 0;
        for (size_t i = 1; i < m->pairs_used; i++) {
            if (m->pairs[i].key > max_key) {
                max_key = m->pairs[i].key;
                exp_value = m->pairs[i].value;
                match_i = i;
            }
        }
        if (key != max_key) {
            LOG_FAIL("POP_LAST: not max key (exp %p, got %p)\n",
                max_key, key);
        }
        if (value != exp_value) {
            LOG_FAIL("POP_LAST: not max key's value (%p), got %p\n",
                (void *)exp_value, (void *)value);
        }

        if (match_i < m->pairs_used - 1) {
            m->pairs[match_i].key = m->pairs[m->pairs_used - 1].key;
            m->pairs[match_i].value = m->pairs[m->pairs_used - 1].value;
        }
        m->pairs_used--;
        return true;
    }
}

static bool eval_member(struct op *op, struct model *m) {
    size_t found_i = 0;
    bool found = check_if_known(m, op->u.member.key, &found_i);

    bool member = skiparray_member(m->sa, (void *)op->u.member.key);
    if (member != found) {
        LOG_FAIL("MEMBER: expected %d, got %d\n", found, member);
    }
    return true;
}

static bool eval_count(struct op *op, struct model *m) {
    (void)op;
    size_t count = skiparray_count(m->sa);
    if (count != m->pairs_used) {
        LOG_FAIL("COUNT: expected %zd, got %zd\n", m->pairs_used, count);
        return false;
    }
    return true;
}

static bool eval_first(struct op *op, struct model *m) {
    (void)op;
    void *key = 0;
    void *value = 0;
    enum skiparray_first_res res = skiparray_first(m->sa, &key, &value);
    if (m->pairs_used == 0) {
        if (res != SKIPARRAY_FIRST_EMPTY) {
            LOG_FAIL("FIRST: expected EMPTY\n");
        }
        return true;
    } else {
        void *min_key = m->pairs[0].key;
        void *exp_value = m->pairs[0].value;
        for (size_t i = 1; i < m->pairs_used; i++) {
            if (m->pairs[i].key < min_key) {
                min_key = m->pairs[i].key;
                exp_value = m->pairs[i].value;
            }
        }
        if (key != min_key) {
            LOG_FAIL("FIRST: not min key (exp %p, got %p)\n",
                min_key, key);
        }
        if (value != exp_value) {
            LOG_FAIL("FIRST: not min key's value (%p), got %p\n",
                (void *)exp_value, (void *)value);
        }
        return true;
    }
}

static bool eval_last(struct op *op, struct model *m) {
    (void)op;
    void *key = 0;
    void *value = 0;
    enum skiparray_last_res res = skiparray_last(m->sa, &key, &value);
    if (m->pairs_used == 0) {
        if (res != SKIPARRAY_LAST_EMPTY) {
            LOG_FAIL("LAST: expected EMPTY\n");
        }
        return true;
    } else {
        void *max_key = m->pairs[0].key;
        void *exp_value = m->pairs[0].value;
        for (size_t i = 1; i < m->pairs_used; i++) {
            if (m->pairs[i].key > max_key) {
                max_key = m->pairs[i].key;
                exp_value = m->pairs[i].value;
            }
        }
        if (key != max_key) {
            LOG_FAIL("LAST: not max key (exp %p, got %p)\n",
                max_key, key);
        }
        if (value != exp_value) {
            LOG_FAIL("LAST: not max key's value (%p), got %p\n",
                (void *)exp_value, (void *)value);
        }
        return true;
    }
}

static bool validate(struct model *m) {
    for (size_t i = 0; i < m->pairs_used; i++) {
        void *v = 0;
        if (!skiparray_get(m->sa, m->pairs[i].key, &v)) {
            if (m->env->verbosity > 0) {
                printf("VALIDATE: lost binding for %p\n", m->pairs[i].key);
            }
            return false;
        }

        if (v != m->pairs[i].value) {
            if (m->env->verbosity > 0) {
                printf("VALIDATE: wrong binding for %p -- "
                    "expected %p, got %p\n",
                    m->pairs[i].key, m->pairs[i].value, v);
            }
            return false;
        }
    }

    if (!test_skiparray_invariants(m->sa, m->env->verbosity)) {
        return false;
    }

    return true;
}

TEST regression(void) {
#define INIT(SEED, SIZE)                                                \
    struct skiparray_config sa_config = {                               \
        .cmp = test_skiparray_cmp_intptr_t,                             \
        .seed = SEED,                                                   \
        .node_size = SIZE,                                              \
    };                                                                  \
    struct skiparray *sa = NULL;                                        \
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);       \
    ASSERT_EQ(SKIPARRAY_NEW_OK, nres)

#define GET(K, EXP)                                                     \
    do {                                                                \
        void *value = (void *)0;                                        \
        ASSERT(skiparray_get(sa, (void *)K, &value));                   \
        ASSERT_EQ_FMT((void *)EXP, value, "%p");                        \
    } while(0)                                                          \

#define SET(K, V)                                                       \
    do {                                                                \
        enum skiparray_set_res res = skiparray_set(sa,                  \
            (void *)K, (void *)V);                                      \
        if (res != SKIPARRAY_SET_REPLACED) {                            \
            ASSERT_EQ_FMT(SKIPARRAY_SET_BOUND, res, "%d");              \
        }                                                               \
    } while(0)

#define FORGET(K)                                                       \
    do {                                                                \
        enum skiparray_forget_res res = skiparray_forget(sa, K, NULL);  \
        ASSERT_EQ_FMT(SKIPARRAY_FORGET_OK, res, "%d");                  \
    } while(0)

#define POP_FIRST(EXP_KEY, EXP_VALUE)                                   \
    do {                                                                \
        void *key = 0;                                                  \
        void *value = 0;                                                \
        enum skiparray_pop_res res =                                    \
          skiparray_pop_first(sa, &key, &value);                        \
        ASSERT_EQ_FMT(SKIPARRAY_POP_OK, res, "%d");                     \
        ASSERT_EQ_FMT((void *)EXP_KEY, key, "%p");                      \
        ASSERT_EQ_FMT((void *)EXP_VALUE, value, "%p");                  \
    } while(0)

#define POP_LAST(EXP_KEY, EXP_VALUE)                                    \
    do {                                                                \
        void *key = 0;                                                  \
        void *value = 0;                                                \
        enum skiparray_pop_res res =                                    \
          skiparray_pop_last(sa, &key, &value);                         \
        ASSERT_EQ_FMT(SKIPARRAY_POP_OK, res, "%d");                     \
        ASSERT_EQ_FMT((void *)EXP_KEY, key, "%p");                      \
        ASSERT_EQ_FMT((void *)EXP_VALUE, value, "%p");                  \
    } while(0)

#define CHECK() ASSERT(test_skiparray_invariants(sa, greatest_get_verbosity()));

    // 0x81358e447b66b10fLLU;
/*  -- Counter-Example: preserve_invariants(100000) */
/*     Trial 0, Seed 0x0484b9b6c567f9f0 */
/*     Argument 0: */
/* #skiparray_operations{0x2137d20, count 7, seed 5, node_size 2} */
/* == 3: SET 5647 => 0 */
/* == 5: SET 18954 => 3 */
/* == 6: GET 14063 */

    // 0xc75a631f7c3da256
    INIT(0, 3);

    SET(0, 0);
    SET(7, 0);
    SET(8, 0);
    CHECK();
    SET(3, 0);
    CHECK();
    GET(0, 0);
    GET(7, 0);
    GET(8, 0);
    GET(3, 0);

    skiparray_free(sa);
    PASS();
}

SUITE(prop) {
    RUN_TESTp(preserve_invariants, 10000000, 0x1de22a0cf5232d98LLU);

    for (size_t i = 10; i <= 10000000; i *= 100) {
        if (greatest_get_verbosity() > 0) {
            printf("## preserve_invariants %zd\n", i);
        }
        RUN_TESTp(preserve_invariants, i, 0);
    }

    RUN_TEST(regression);
}
