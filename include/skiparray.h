/*
 * Copyright (c) 2019 Scott Vokes <vokes.s@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef SKIPARRAY_H
#define SKIPARRAY_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* Version 0.1.1 */
#define SKIPARRAY_VERSION_MAJOR 0
#define SKIPARRAY_VERSION_MINOR 1
#define SKIPARRAY_VERSION_PATCH 1

/* Default level limit as thes skiparray grows. */
#define SKIPARRAY_DEF_MAX_LEVEL 16

/* Max value allowed for max_level option. */
#define SKIPARRAY_MAX_MAX_LEVEL 32

/* By default, individual nodes have at most this many pairs. Nodes are
 * always at least half-full, except for the very last node. */
#define SKIPARRAY_DEF_NODE_SIZE 1024

/* Opaque handle for a skiparray, an unrolled skiplist. */
struct skiparray;

/* Memory management function:
 *
 * - If P is NULL, allocate and return a word-aligned pointer with at
 *   least NSIZE bytes available.
 * - If P is non-NULL and nsize is 0, free it, and return NULL.
 * - Never called with non-NULL P and nsize > 0 (the realloc case).
 * */
typedef void *skiparray_memory_fun(void *p, size_t nsize, void *udata);

/* Compare keys KA and KB:
 * | when KA is < KB -> < 0;
 * | when KA is > KB -> > 0;
 * | when KA is = KB -> 0.
 *
 * The result of comparing any particular two keys must not change over
 * the lifetime of the skiparray (e.g. do not change a collation setting
 * stored in udata). */
typedef int skiparray_cmp_fun(const void *ka,
    const void *kb, void *udata);

/* Return the level for a new skiparray node (0 <= X < max_level).
 * This should be calculated based on PRNG_STATE_IN (or similar state
 * in UDATA), and update *PRNG_STATE_OUT or UDATA to a new random
 * number generator state.
 *
 * There should be approximately half as many nodes on each level over
 * 0 as the level below it. */
typedef int skiparray_level_fun(uint64_t prng_state_in,
    uint64_t *prng_state_out, void *udata);

/* Configuration for the skiparray.
 * All fields are optional except cmp. */
struct skiparray_config {
    /* How many key/value pairs should be stored in each node?
     * Must be >= 2, or 0 for the default. */
    uint16_t node_size;
    /* At most how many express levels should the skiparray have?
     * A max_level of 0 will use the default. */
    uint8_t max_level;
    uint64_t seed;
    /* If this flag is set, then no memory will be allocated for values,
     * and value parameters will be ignored. If only the keys are being
     * used (as an ordered set), then this will cut memory usage in
     * half, and make operations faster by reducing cache misses. */
    bool ignore_values;

    skiparray_cmp_fun *cmp;       /* required */
    skiparray_memory_fun *memory; /* optional */
    skiparray_level_fun *level;   /* optional */
    void *udata;                  /* callback data, opaque to library */
};

/* Allocate a new skiparray. */
enum skiparray_new_res {
    SKIPARRAY_NEW_OK,
    SKIPARRAY_NEW_ERROR_NULL = -1,
    SKIPARRAY_NEW_ERROR_CONFIG = -2,
    SKIPARRAY_NEW_ERROR_MEMORY = -3,
};
enum skiparray_new_res
skiparray_new(const struct skiparray_config *config,
    struct skiparray **sa);

/* Free a skiparray. If CB is non-NULL, then it will be called with
 * every key, value pair and udata. Any iterators associated with this
 * skiparray will be freed, and pointers to them will become stale. */
typedef void skiparray_free_fun(void *key,
    void *value, void *udata);
void skiparray_free(struct skiparray *sa,
    skiparray_free_fun *cb, void *udata);

/* Get the value associated with a key.
 * Returns whether the value was found. */
bool
skiparray_get(const struct skiparray *sa,
    const void *key, void **value);

struct skiparray_pair {
    void *key;
    void *value;
};

/* Same as skiparray_get, but also get the key actually
 * used in the binding as well as the value. */
bool
skiparray_get_pair(const struct skiparray *sa,
    const void *key, struct skiparray_pair *pair);

/* Set/update a binding in the skiparray, possibly replacing
 * an existing binding. Note that once a key is in the skiparray,
 * it should not be modified in any way that influences comparison
 * order. The key is only not const so that it can be freed later.
 *
 * This function (and any others below that would modify the skiparray)
 * will return ERROR_LOCKED if any iterators are active.
 *
 * To get info about a binding being replaced, use
 * skiparray_set_with_pair. This function is just a wrapper for it,
 * with REPLACE_PREVIOUS_KEY of true and PAIR set to NULL. */
enum skiparray_set_res {
    SKIPARRAY_SET_BOUND,
    SKIPARRAY_SET_REPLACED,
    SKIPARRAY_SET_ERROR_NULL = -1,
    SKIPARRAY_SET_ERROR_MEMORY = -2,
    SKIPARRAY_SET_ERROR_LOCKED = -3,
};
enum skiparray_set_res
skiparray_set(struct skiparray *sa, void *key, void *value);

/* Set/update a binding in the skiparray. If PREVIOUS_BINDING is
 * non-NULL, its fields will be set to the previous binding, if any.
 *
 * If replacing an existing binding, REPLACE_KEY determines whether it
 * will continue using the current key (false) or change it to the new
 * key (true). When there are multiple instances of a key that are not
 * pointer-equal, but equal according to the comparison callback, it
 * will usually be necessary to free one of them to avoid memory
 * leaks. */
enum skiparray_set_res
skiparray_set_with_pair(struct skiparray *sa, void *key, void *value,
    bool replace_key, struct skiparray_pair *previous_binding);

/* Remove a binding from the skiparray. If PAIR is non-NULL, its key and
 * value fields will be set to the forgotten binding. */
enum skiparray_forget_res {
    SKIPARRAY_FORGET_OK,
    SKIPARRAY_FORGET_NOT_FOUND,
    SKIPARRAY_FORGET_ERROR_NULL = -1,
    SKIPARRAY_FORGET_ERROR_MEMORY = -2,
    SKIPARRAY_FORGET_ERROR_LOCKED = -3,
};
enum skiparray_forget_res
skiparray_forget(struct skiparray *sa, const void *key,
    struct skiparray_pair *forgotten);

/* Does KEY have an associated binding? */
bool
skiparray_member(const struct skiparray *sa,
    const void *key);

/* How many bindings are there? */
size_t
skiparray_count(const struct skiparray *sa);

/* Get the first binding. */
enum skiparray_first_res {
    SKIPARRAY_FIRST_OK,
    SKIPARRAY_FIRST_EMPTY,
};
enum skiparray_first_res
skiparray_first(const struct skiparray *sa,
    void **key, void **value);

/* Get the last binding. */
enum skiparray_last_res {
    SKIPARRAY_LAST_OK,
    SKIPARRAY_LAST_EMPTY,
};
enum skiparray_last_res
skiparray_last(const struct skiparray *sa,
    void **key, void **value);

enum skiparray_pop_res {
    SKIPARRAY_POP_OK,
    SKIPARRAY_POP_EMPTY,
    SKIPARRAY_POP_ERROR_MEMORY = -1,
    SKIPARRAY_POP_ERROR_LOCKED = -2,
};

/* Get and remove the first binding. */
enum skiparray_pop_res
skiparray_pop_first(struct skiparray *sa,
    void **key, void **value);

/* Get and remove the last binding. */
enum skiparray_pop_res
skiparray_pop_last(struct skiparray *sa,
    void **key, void **value);

/* Opaque handle to a skiparray iterator. */
struct skiparray_iter;

/* Allocate a new iterator handle. This will store a pointer to
 * the skiparray, and the skiparray tracks its active iterator(s).
 *
 * The skiparray cannot be modified while there are any active
 * iterators. Operations such as set will just return ERROR_LOCKED. */
enum skiparray_iter_new_res {
    SKIPARRAY_ITER_NEW_OK,
    SKIPARRAY_ITER_NEW_EMPTY,
    SKIPARRAY_ITER_NEW_ERROR_MEMORY = -1,
};
enum skiparray_iter_new_res
skiparray_iter_new(struct skiparray *sa,
    struct skiparray_iter **res);

/* Free an iterator. If there are no more iterators associated with a
 * skiparray, it will become unlocked and can again be modified.
 * Iterators do not need to be freed in any particular order. */
void
skiparray_iter_free(struct skiparray_iter *iter);

enum skiparray_iter_seek_endpoint {
    SKIPARRAY_ITER_SEEK_FIRST,
    SKIPARRAY_ITER_SEEK_LAST,
};
void
skiparray_iter_seek_endpoint(struct skiparray_iter *iter,
    enum skiparray_iter_seek_endpoint end);

/* Seek to the first binding >= the given key.
 * The iterator position is not updated on error. */
enum skiparray_iter_seek_res {
    SKIPARRAY_ITER_SEEK_FOUND,     /* now at binding with key */
    SKIPARRAY_ITER_SEEK_NOT_FOUND, /* now at first binding with > key */
    SKIPARRAY_ITER_SEEK_ERROR_BEFORE_FIRST, /* position not updated */
    SKIPARRAY_ITER_SEEK_ERROR_AFTER_LAST,   /* position not updated */
};
enum skiparray_iter_seek_res
skiparray_iter_seek(struct skiparray_iter *iter,
    const void *key);

/* Seek to the next binding; returns END if at the last pair. */
enum skiparray_iter_step_res {
    SKIPARRAY_ITER_STEP_OK,
    SKIPARRAY_ITER_STEP_END,
};
enum skiparray_iter_step_res
skiparray_iter_next(struct skiparray_iter *iter);

/* Seek to the previous binding; returns END if at the first pair. */
enum skiparray_iter_step_res
skiparray_iter_prev(struct skiparray_iter *iter);

/* Get the key and/or value at the current iterator position. */
void
skiparray_iter_get(struct skiparray_iter *iter,
    void **key, void **value);

#endif
