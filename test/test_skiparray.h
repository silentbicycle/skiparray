#ifndef TEST_SKIPARRAY_H
#define TEST_SKIPARRAY_H

#define GREATEST_USE_LONGJMP 0
#include "greatest.h"
#include "theft.h"
#include "skiparray.h"

#include <inttypes.h>
#include <assert.h>

bool test_skiparray_invariants(struct skiparray *sa, int verbosity);

SUITE_EXTERN(basic);
SUITE_EXTERN(builder);
SUITE_EXTERN(fold);
SUITE_EXTERN(prop);
SUITE_EXTERN(hof);
SUITE_EXTERN(integration);

struct test_env {
    char tag;
    size_t limit;
    size_t pair_count;
    uint8_t verbosity;
    struct theft_print_trial_result_env print_env;
};

enum op_type {
    OP_GET,
    OP_SET,
    OP_FORGET,
    OP_POP_FIRST,
    OP_POP_LAST,
    OP_MEMBER,
    OP_COUNT,
    OP_FIRST,
    OP_LAST,

    /* No need to include the iterator stuff -- each operation
     * already calls test_skiparray_invariants on it, which
     * does a full iteration forwards and backwards. */

    OP_TYPE_COUNT,
};

struct op {
    enum op_type t;
    union {
        struct {
            intptr_t key;
        } get;
        struct {
            intptr_t key;
            intptr_t value;
        } set;
        struct {
            intptr_t key;
        } forget;
        struct {
            intptr_t key;
        } member;
    } u;
};

struct scenario {
    uint32_t seed;
    uint16_t node_size;
    size_t count;
    struct op ops[];
};

struct pair {
    void *key;
    void *value;
};

struct model {
    char tag;
    struct skiparray *sa;
    struct test_env *env;

    size_t pairs_used;
    struct pair pairs[];
};

extern const struct theft_type_info type_info_skiparray_operations;

int test_skiparray_cmp_intptr_t(const void *ka,
    const void *kb, void *udata);

struct skiparray *
test_skiparray_sequential_build(size_t limit);

#endif
