#ifndef SKIPARRAY_INTERNAL_H
#define SKIPARRAY_INTERNAL_H

#include "skiparray_internal_types.h"

#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>

#define LOG_LEVEL 0
#define LOG_FILE stdout
#define LOG(LVL, ...)                                                  \
    do {                                                               \
        if (LVL <= LOG_LEVEL) {                                        \
            fprintf(LOG_FILE, __VA_ARGS__);                            \
        }                                                              \
    } while(0)

static struct node *
node_alloc(uint8_t height, uint16_t node_size,
    skiparray_memory_fun *mem, void *udata);

static void node_free(const struct skiparray *sa, struct node *n);

enum search_res {
    SEARCH_FOUND,
    SEARCH_NOT_FOUND,
    SEARCH_EMPTY,
};
static enum search_res
search(struct search_env *env);

static void
prepare_node_for_insert(struct skiparray *sa,
    struct node *n, uint16_t index);

static bool
split_node(struct skiparray *sa,
    struct node *n, struct node **res);

static void
shift_or_merge(struct skiparray *sa, struct node *n);

static void
unlink_node(struct skiparray *sa, struct node *n);

static bool
search_within_node(const struct skiparray *sa,
    const void *key, const struct node *n, uint16_t *index);

static void
shift_pairs(struct node *n,
    uint16_t to_pos, uint16_t from_pos, uint16_t count);

static void
move_pairs(struct node *to, struct node *from,
    uint16_t to_pos, uint16_t from_pos, uint16_t count);

static void
*def_memory_fun(void *p, size_t nsize, void *udata);

static int
def_level_fun(uint64_t prng_state_in,
    uint64_t *prng_state_out, void *udata);

static void
dump_raw_bindings(const char *tag,
    const struct skiparray *sa, const struct node *n);

#endif
