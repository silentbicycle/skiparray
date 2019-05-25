#include "skiparray_internal_types.h"

#include "assert.h"

/* Other misc. higher-order functions. */

struct filter_fold_env {
    char tag;
    struct skiparray_builder *b;
    skiparray_filter_fun *fun;
    void *udata;
    bool ok;
};

static void
filter_append(void *key, void *value, void *udata) {
    struct filter_fold_env *env = udata;
    assert(env->tag == 'F');
    if (env->fun(key, value, env->udata)) {
        if (SKIPARRAY_BUILDER_APPEND_OK !=
            skiparray_builder_append(env->b, key, value)) {
            env->ok = false;
        }
    }
}

struct skiparray *
skiparray_filter(struct skiparray *sa,
    skiparray_filter_fun *fun, void *udata) {
    assert(sa != NULL);
    assert(fun != NULL);

    struct skiparray_builder *b = NULL;
    {
        struct skiparray_config cfg = {
            .node_size = sa->node_size,
            .max_level = sa->max_level,
            .ignore_values = !sa->use_values,
            .cmp = sa->cmp,
            .memory = sa->mem,
            .level = sa->level,
            .udata = sa->udata,
        };
        if (SKIPARRAY_BUILDER_NEW_OK != skiparray_builder_new(&cfg, true, &b)) {
            return NULL;
        }
    }

    struct filter_fold_env env = {
        .tag = 'F',
        .b = b,
        .fun = fun,
        .udata = udata,
        .ok = true,
    };

    if (SKIPARRAY_FOLD_OK != skiparray_fold(SKIPARRAY_FOLD_LEFT,
            sa, filter_append, &env)) {
        skiparray_builder_free(b, NULL, NULL);
        return NULL;
    }

    if (env.ok != true) {
        skiparray_builder_free(b, NULL, NULL);
        return NULL;
    }

    struct skiparray *res = NULL;
    skiparray_builder_finish(&b, &res);
    return res;
}
