#include "test_skiparray.h"

static enum theft_alloc_res
op_alloc(struct theft *t, void *penv, void **output) {
    (void)penv;
    struct scenario *res = NULL;

    struct test_env *env = theft_hook_get_env(t);
    assert(env->tag == 'T');

    const size_t max_count = theft_random_choice(t, (uint64_t)env->limit);

    size_t alloc_size = sizeof(*res) + max_count * sizeof(struct op);
    res = malloc(alloc_size);
    memset(res, 0x00, alloc_size);
    if (res == NULL) {
        return THEFT_ALLOC_ERROR;
    }

    res->seed = theft_random_bits(t, 16);
    res->node_size = 2 + theft_random_choice(t, 64);

    size_t count = 0;
    for (size_t i = 0; i < max_count; i++) {
        struct op *op = &res->ops[count];
        if (0 == theft_random_bits(t, 1)) {
            continue;           /* shrink away */
        }

        op->t = (enum op_type)theft_random_choice(t, OP_TYPE_COUNT);
        assert(op->t < OP_TYPE_COUNT);

        switch (op->t) {
        case OP_GET:
            op->u.get.key = theft_random_choice(t, env->limit);
            break;
        case OP_SET:
            op->u.set.key = theft_random_choice(t, env->limit);
            op->u.set.value = theft_random_bits(t, 8);
            break;
        case OP_FORGET:
            op->u.forget.key = theft_random_choice(t, env->limit);
            break;
        case OP_POP_FIRST:
        case OP_POP_LAST:
            break;

        case OP_MEMBER:
            op->u.member.key = theft_random_choice(t, env->limit);
            break;
        case OP_COUNT:
        case OP_FIRST:
        case OP_LAST:
            break;

        default:
        case OP_TYPE_COUNT:
            assert(false);
        }
        count++;
    }
    res->count = count;

    *output = res;
    return THEFT_ALLOC_OK;
}

static void
op_print(FILE *f, const void *instance, void *env) {
    (void)env;
    const struct scenario *scen = (const struct scenario *)instance;
    printf("#skiparray_operations{%p, count %zd, seed %" PRIu32
        ", node_size %" PRIu16 "}\n",
        (void *)scen, scen->count, scen->seed, scen->node_size);
    for (size_t i = 0; i < scen->count; i++) {
        const struct op *op = &scen->ops[i];
        switch (op->t) {
        case OP_GET:
            fprintf(f, "== %zd: GET %" PRIdPTR "\n",
                i, op->u.get.key);
            break;
        case OP_SET:
            fprintf(f, "== %zd: SET %" PRIdPTR " => %" PRIdPTR "\n",
                i, op->u.set.key, op->u.set.value);
            break;
        case OP_FORGET:
            fprintf(f, "== %zd: FORGET %" PRIdPTR "\n",
                i, op->u.forget.key);
            break;
        case OP_MEMBER:
            fprintf(f, "== %zd: MEMBER %" PRIdPTR "\n",
                i, op->u.member.key);
            break;
        case OP_COUNT:
            fprintf(f, "== %zd: COUNT\n", i);
            break;
        case OP_POP_FIRST:
            fprintf(f, "== %zd: POP_FIRST\n", i);
            break;
        case OP_POP_LAST:
            fprintf(f, "== %zd: POP_LAST\n", i);
            break;
        case OP_FIRST:
            fprintf(f, "== %zd: FIRST\n", i);
            break;
        case OP_LAST:
            fprintf(f, "== %zd: LAST\n", i);
            break;

        default:
            assert(false);
        }
    }
}

const struct theft_type_info type_info_skiparray_operations = {
    .alloc = op_alloc,
    .free = theft_generic_free_cb,
    .print = op_print,

    .autoshrink_config = {
        .enable = true,
    },
};
