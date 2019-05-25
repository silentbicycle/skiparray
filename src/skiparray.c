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

#include "skiparray_internal.h"

enum skiparray_new_res
skiparray_new(const struct skiparray_config *config,
    struct skiparray **sa) {
    if (sa == NULL || config == NULL) {
        return SKIPARRAY_NEW_ERROR_NULL;
    }

    if (config->node_size == 1 || config->cmp == NULL) {
        return SKIPARRAY_NEW_ERROR_CONFIG;
    }

#define DEF(FIELD, DEF) (config->FIELD == 0 ? DEF : config->FIELD)
    uint16_t node_size = DEF(node_size, SKIPARRAY_DEF_NODE_SIZE);
    uint8_t max_level = DEF(max_level, SKIPARRAY_DEF_MAX_LEVEL);
#undef DEF
#define DEF(FIELD, DEF) (config->FIELD == NULL ? DEF : config->FIELD)
    skiparray_memory_fun *mem = DEF(memory, def_memory_fun);
    skiparray_level_fun *level = DEF(level, def_level_fun);
#undef DEF

    const size_t alloc_size = sizeof(struct skiparray) +
      max_level * sizeof(struct node *);
    struct skiparray *res = mem(NULL, alloc_size, config->udata);
    if (res == NULL) { return SKIPARRAY_NEW_ERROR_MEMORY; }
    memset(res, 0x00, alloc_size);

    uint64_t prng_state = 0;
    uint8_t root_level = level(config->seed, &prng_state, config->udata) + 1;
    if (root_level >= max_level) { root_level = max_level; }

    struct skiparray fields = {
        .node_size = node_size,
        .max_level = max_level,
        .height = root_level,
        .use_values = !config->ignore_values,
        .prng_state = prng_state,
        .mem = mem,
        .cmp = config->cmp,
        .free = config->free,
        .level = level,
        .udata = config->udata,
    };
    memcpy(res, &fields, sizeof(fields));

    struct node *root = node_alloc(root_level, node_size,
        mem, config->udata, fields.use_values);
    if (root == NULL) {
        mem(res, 0, config->udata);
        return SKIPARRAY_NEW_ERROR_MEMORY;
    }

    for (size_t i = 0; i < root_level; i++) {
        res->nodes[i] = root;
        LOG(4, "%s: res->nodes[%zu]: %p\n",
            __func__, i, (void *)res->nodes[i]);
    }
    for (size_t i = root_level; i < max_level; i++) {
        res->nodes[i] = NULL;
        LOG(4, "%s: res->nodes[%zu]: %p\n",
            __func__, i, (void *)res->nodes[i]);
    }

    *sa = res;
    LOG(2, "%s: new SA %p with height %u, max_level %u\n",
        __func__, (void *)res, root_level, res->max_level);
    return SKIPARRAY_NEW_OK;
}

void
skiparray_free(struct skiparray *sa) {
    assert(sa != NULL);
    struct node *n = sa->nodes[0];
    while (n != NULL) {
        struct node *next = n->fwd[0];
        if (sa->free != NULL) {
            for (size_t i = 0; i < n->count; i++) {
                sa->free(n->keys[n->offset + i],
                    sa->use_values ? n->values[n->offset + i] : NULL, sa->udata);
            }
        }
        node_free(sa, n);
        n = next;
    }

    /* Free any remaining iterators */
    struct skiparray_iter *iter = sa->iter;
    while (iter != NULL) {
        struct skiparray_iter *next = iter->next;
        sa->mem(iter, 0, sa->udata);
        iter = next;
    }

    sa->mem(sa, 0, sa->udata);
}

bool
skiparray_get(const struct skiparray *sa,
    const void *key, void **value) {
    struct skiparray_pair p;
    if (skiparray_get_pair(sa, key, &p)) {
        if (value != NULL) { *value = p.value; }
        return true;
    } else {
        return false;
    }
}

bool
skiparray_get_pair(const struct skiparray *sa,
    const void *key, struct skiparray_pair *pair) {
    LOG(2, "%s: key %p\n", __func__, (void *)key);
    assert(sa != NULL);
    assert(pair != NULL);

    struct search_env env = {
        .sa = sa,
        .key = key,
    };
    enum search_res sres = search(&env);
    switch (sres) {
    default:
        assert(false);
    case SEARCH_NOT_FOUND:
        return false;

    case SEARCH_FOUND:
    {
        struct node *n = env.n;
        pair->key = n->keys[n->offset + env.index];
        pair->value = sa->use_values ? n->values[n->offset + env.index] : NULL;
        return true;
    }
    }
}

static bool
has_iterators(const struct skiparray *sa) {
    return sa->iter != NULL;
}

enum skiparray_set_res
skiparray_set(struct skiparray *sa,
    void *key, void *value) {
    return skiparray_set_with_pair(sa, key, value, true, NULL);
}

enum skiparray_set_res
skiparray_set_with_pair(struct skiparray *sa, void *key, void *value,
    bool replace_key, struct skiparray_pair *previous_binding) {
    LOG(2, "%s: key %p => value %p\n",
        __func__, (void *)key, (void *)value);
    assert(sa);

    if (has_iterators(sa)) { return SKIPARRAY_SET_ERROR_LOCKED; }

    struct search_env env = {
        .sa = sa,
        .key = key,
    };

    enum search_res sres = search(&env);

    switch (sres) {
    case SEARCH_FOUND:
    {
        struct node *n = env.n;
        assert(n);
        void **k = &n->keys[n->offset + env.index];
        static void *the_NULL = NULL; /* safe placeholder for *v */
        void **v = sa->use_values
          ? &n->values[n->offset + env.index] : &the_NULL;
        if (previous_binding != NULL) {
            previous_binding->key = *k;
            previous_binding->value = *v;
        }
        if (sa->use_values) { *v = value; }

        if (replace_key) { *k = key; }

        return SKIPARRAY_SET_REPLACED;
    }

    case SEARCH_NOT_FOUND:
    case SEARCH_EMPTY:
    {
        struct node *n = env.n;
        assert(n);
        if (env.n->count == sa->node_size) {
            /* split, update node; index in env.
             * This is the only code path that changes the overall
             * skiplist structure, and can be fairly rare with large nodes. */
            struct node *new = NULL;
            if (!split_node(sa, n, &new)) {
                return SKIPARRAY_SET_ERROR_MEMORY;
            }
            assert(new->count > 0);

            /* Update back pointer */
            if (n->fwd[0] != NULL) { n->fwd[0]->back = new; }

            if (LOG_LEVEL >= 3) {
                for (size_t i = 0; i <= new->height; i++) {
                    LOG(3, "post-split: sa->nodes[%zu]: %p\n",
                        i, (void *)sa->nodes[i]);
                }
            }

            /* If the new node is taller than the node it split from,
             * then find the preceding nodes on those levels and update
             * their forward pointers. */
            struct node *prev = NULL;
            struct node *cur = NULL;
            for (size_t level = new->height - 1; level >= n->height; level--) {
                LOG(2, "%s: updating forward pointers on level %zu, cur %p\n",
                    __func__, level, (void *)cur);
                if (level > sa->height) { continue; }
                if (cur == NULL) {
                    if (sa->nodes[level] == NULL) { continue; }
                    cur = sa->nodes[level];
                }
                for (;;) {
                    assert(cur);
                    assert(cur->count > 0);
                    const int res = sa->cmp(new->keys[new->offset],
                        cur->keys[cur->offset + cur->count - 1], sa->udata);
                    LOG(2, "%s: level %zu, cur %p, cmp %d, prev %p\n",
                        __func__, level, (void *)cur, res, (void *)prev);
                    if (res < 0) { /* overshot */
                        if (prev == NULL) {
                            LOG(2, "%s: setting sa->nodes[%zu] to %p\n",
                                __func__, level, (void *)new);
                            new->fwd[level] = sa->nodes[level];
                            sa->nodes[level] = new;
                        }
                        cur = prev;
                        break;
                    } else if (res > 0) {
                        prev = cur;
                        if (cur->fwd[level] == NULL) {
                            LOG(2, "%s: setting %p->fwd[%zu] to %p\n",
                                __func__, (void *)cur, level, (void *)new);
                            cur->fwd[level] = new;
                            break;
                        } else {
                            LOG(2, "%s: advancing cur from %p to %p\n",
                                __func__, (void *)cur, (void *)cur->fwd[level]);
                            cur = cur->fwd[level];
                        }
                        assert(cur);
                    } else {
                        assert(false);
                    }
                }

                if (prev != NULL) {
                    if (prev->fwd[level] != new) {
                        LOG(2, "%s: setting new->fwd[%zu] to %p\n",
                            __func__, level, (void *)prev->fwd[level]);
                        new->fwd[level] = prev->fwd[level];
                    }
                    LOG(2, "%s: setting prev->fwd[%zu] to %p\n",
                        __func__, level, (void *)new);
                    prev->fwd[level] = new;
                    if (new->fwd[level]) {
                        assert(new->fwd[level]->height > level);
                    }
                }
            }

            /* If the new node is taller than the current SA height,
             * then increase it and update forward links. */
            while (new->height > sa->height) {
                sa->nodes[sa->height] = new;
                sa->height++;
            }

            /* Update the forward pointers on the node that split. */
            const uint8_t common_height = (n->height < new->height
                ? n->height : new->height);
            for (size_t i = 0; i < common_height; i++) {
                new->fwd[i] = n->fwd[i];
                n->fwd[i] = new;
            }

            if (env.index > n->count) { /* now inserting on new node */
                LOG(2, "split, was inserting at %" PRIu16
                    ", now inserting at %" PRIu16 " on new\n",
                    env.index, env.index - n->count);
                env.index -= n->count;
                n = new;
            }
        }

        prepare_node_for_insert(sa, n, env.index);

        assert(n->offset + env.index < sa->node_size);
        n->keys[n->offset + env.index] = key;

        if (sa->use_values) {
            n->values[n->offset + env.index] = value;
        }

        n->count++;
        LOG(2, "%s: now node %p has %" PRIu16 " pair(s)\n",
            __func__, (void *)n, n->count);
        return SKIPARRAY_SET_BOUND;
    }

    default:
        return SKIPARRAY_SET_ERROR_NULL;
    }
}

enum skiparray_forget_res
skiparray_forget(struct skiparray *sa, const void *key,
    struct skiparray_pair *forgotten) {
    LOG(2, "%s: key %p\n",
        __func__, (void *)key);

    if (has_iterators(sa)) { return SKIPARRAY_FORGET_ERROR_LOCKED; }

    struct search_env env = {
        .sa = sa,
        .key = key,
    };
    enum search_res sres = search(&env);
    switch (sres) {
    case SEARCH_NOT_FOUND:
    case SEARCH_EMPTY:
        return SKIPARRAY_FORGET_NOT_FOUND;

    case SEARCH_FOUND:
    {
        struct node *n = env.n;
        assert(n);

        LOG(2, "%s: found in node %p at index %" PRIu16 "\n",
            __func__, (void *)n, env.index);
        assert(env.index < n->count);

        if (forgotten != NULL) {
            forgotten->key = n->keys[n->offset + env.index];
            forgotten->value = sa->use_values
              ? n->values[n->offset + env.index] : NULL;
        }

        if (LOG_LEVEL >= 4) {
            dump_raw_bindings("PRE-FORGET", sa, n);
        }

        if (env.index == 0) {   /* first */
            n->offset++;
            /* Deletion shouldn't gradually shift off the end. */
            if (n->offset == sa->node_size) {
                n->offset = sa->node_size/2;
            }
            n->count--;
        } else if (env.index == n->count - 1) { /* last */
            n->count--;
        } else {                /* from middle */
            const uint16_t to_move = n->count - env.index - 1;
            shift_pairs(n, n->offset + env.index,
                n->offset + env.index + 1, to_move);
            n->count--;
        }

        LOG(2, "%s: count after deletion for %p: %" PRIu16 " (offset %" PRIu16 ")\n",
            __func__, (void *)n, n->count, n->offset);

        if (LOG_LEVEL >= 4) {
            dump_raw_bindings("POST-FORGET", sa, n);
        }

        if (n->count < sa->node_size/2) {
            /* The node is too empty: either shift over entries
             * from the following L0 node (if any), or if it's
             * also too empty, merge with it.*/
            shift_or_merge(sa, n);

            if (LOG_LEVEL >= 4) {
                dump_raw_bindings("POST-FORGET (post merge)", sa, n);
            }
        }

        return SKIPARRAY_FORGET_OK;
    }

    default:
        return SKIPARRAY_FORGET_ERROR_NULL;
    }
}

bool
skiparray_member(const struct skiparray *sa,
    const void *key) {
    return skiparray_get(sa, key, NULL);
}

size_t
skiparray_count(const struct skiparray *sa) {
    assert(sa != NULL);

    size_t res = 0;
    struct node *n = sa->nodes[0];
    while (n != NULL) {
        res += n->count;
        n = n->fwd[0];
    }

    return res;
}

enum skiparray_first_res
skiparray_first(const struct skiparray *sa,
    void **key, void **value) {
    assert(sa != NULL);

    struct node *n = sa->nodes[0];
    if (n->count == 0) {
        return SKIPARRAY_FIRST_EMPTY;
    }

    uint16_t index = n->offset;

    if (key != NULL) {
        *key = n->keys[index];
    }

    if (value != NULL && sa->use_values) {
        *value = n->values[index];
    }

    return SKIPARRAY_FIRST_OK;
}

static struct node *
last_node(const struct skiparray *sa) {
    assert(sa->height > 0);
    int level = sa->height - 1;
    struct node *n = sa->nodes[level];
    for (;;) {
        struct node *next = n->fwd[level];
        if (next != NULL) {
            n = next;
        } else {
            if (level == 0) {
                return n;
            } else {
                level--;
            }
        }
    }
}

enum skiparray_last_res
skiparray_last(const struct skiparray *sa,
    void **key, void **value) {
    assert(sa != NULL);

    struct node *n = last_node(sa);

    if (n->count == 0) {
        assert(n == sa->nodes[0]);
        return SKIPARRAY_LAST_EMPTY;
    }

    uint16_t index = n->offset + n->count - 1;

    if (key != NULL) {
        *key = n->keys[index];
    }

    if (value != NULL && sa->use_values) {
        *value = n->values[index];
    }

    return SKIPARRAY_LAST_OK;
}

enum skiparray_pop_res
skiparray_pop_first(struct skiparray *sa,
    void **key, void **value) {
    /* if first node is only half full and not last,
     * then steal from and/or combine with the next node */
    assert(sa != NULL);

    struct node *head = sa->nodes[0];
    LOG(2, "%s: head %p, count %" PRIu16"\n",
        __func__, (void *)head, head->count);

    if (has_iterators(sa)) { return SKIPARRAY_POP_ERROR_LOCKED; }

    if (head->count == 0) {
        assert(head->fwd[0] == NULL);
        return SKIPARRAY_POP_EMPTY;
    }

    if (key != NULL) { *key = head->keys[head->offset]; }
    if (value != NULL && sa->use_values) {
        *value = head->values[head->offset];
    }
    head->offset++;
    if (head->offset == sa->node_size) {
        head->offset = sa->node_size/2;
    }
    head->count--;

    /* If the head node is less than half full (and not the only node),
     * either take some pairs from the next node or merge with it. */
    struct node *next = head->fwd[0];
    const uint16_t required = sa->node_size/2;
    if (head->count < sa->node_size/2 && next != NULL) {
        if (head->count + next->count <= sa->node_size) {
            LOG(2, "%s: combining head with next (%p), which has %" PRIu16 " pairs\n",
                __func__, (void *)next, next->count);
            const uint16_t to_move = next->count;
            if (head->offset > 0) {
                /* move to front, to make room */
                shift_pairs(head, 0, head->offset, head->count);
                head->offset = 0;
            }
            move_pairs(head, next, head->count, next->offset, to_move);
            head->count += to_move;

            for (size_t i = 0; i < next->height; i++) {
                if (i < head->height) {
                    LOG(2, "%s: head->fwd[%zu] = next->fwd[%zu] = %p\n",
                        __func__, i, i, (void *)next->fwd[i]);
                    head->fwd[i] = next->fwd[i];
                } else {
                    LOG(2, "%s: sa->nodes[%zu] = next->fwd[%zu] = %p\n",
                        __func__, i, i, (void *)next->fwd[i]);
                    assert(sa->nodes[i] == next);
                    sa->nodes[i] = next->fwd[i];
                }
            }

            if (next->fwd[0] != NULL) {
                next->fwd[0]->back = head;
            }

            LOG(2, "%s: freeing next node %p\n", __func__, (void *)next);
            node_free(sa, next);

            /* handle decrease in height */
            while (sa->height > 1 && sa->nodes[sa->height - 1] == NULL) { sa->height--; }
        } else {
            const uint16_t to_move = next->count - required;
            LOG(2, "%s: moving %" PRIu16 " pairs from next (%p) to head\n",
                __func__, to_move, (void *)next);
            if (head->offset > 0) {
                /* move to front, to make room */
                shift_pairs(head, 0, head->offset, head->count);
                head->offset = 0;
            }
            move_pairs(head, next, head->count, next->offset, to_move);
            next->count -= to_move;
            next->offset += to_move;
            head->count += to_move;
        }
    }

    return SKIPARRAY_POP_OK;
}

enum skiparray_pop_res
skiparray_pop_last(struct skiparray *sa,
    void **key, void **value) {
    assert(sa != NULL);
    /* same as skiparray_last, but delete last node if empty */
    struct node *head = sa->nodes[0];
    LOG(2, "%s: head %p, count %" PRIu16"\n",
        __func__, (void *)head, head->count);

    if (has_iterators(sa)) { return SKIPARRAY_POP_ERROR_LOCKED; }

    if (head->count == 0) {
        assert(head->fwd[0] == NULL);
        return SKIPARRAY_POP_EMPTY;
    }

    int8_t level = sa->height - 1;
    struct node *cur = sa->nodes[level];
    assert(cur);
    while (level >= 0) {
        if (cur->fwd[level] == NULL) {
            /* If it's the very last node, break */
            if (cur->fwd[0] == NULL) { break; }
            level--;
        } else {
            cur = cur->fwd[level];
        }
    }
    struct node *last = cur;
    assert(last);
    assert(last->fwd[0] == NULL);
    assert(last->count > 0);
    LOG(2, "%s: last node is %p, with %" PRIu16 " pair(s)\n",
        __func__, (void *)last, last->count);

    if (key != NULL) { *key = last->keys[last->offset + last->count - 1]; }
    if (value != NULL && sa->use_values) {
        *value = last->values[last->offset + last->count - 1];
    }
    last->count--;

    if (last->count == 0) {
        if (last == sa->nodes[0]) {
            LOG(2, "%s: retaining empty first/last node\n", __func__);
        } else {
            unlink_node(sa, last);
        }
    }

    return SKIPARRAY_POP_OK;
}

enum skiparray_iter_new_res
skiparray_iter_new(struct skiparray *sa,
    struct skiparray_iter **res) {
    assert(sa != NULL);
    assert(res != NULL);

    if (sa->nodes[0]->fwd[0] == NULL && sa->nodes[0]->count == 0) {
        return SKIPARRAY_ITER_NEW_EMPTY;
    }

    struct skiparray_iter *si = sa->mem(NULL,
        sizeof(*si), sa->udata);
    if (si == NULL) {
        return SKIPARRAY_ITER_NEW_ERROR_MEMORY;
    }

    if (sa->iter != NULL) {
        sa->iter->prev = si;
    }

    *si = (struct skiparray_iter) {
        .sa = sa,
        .prev = NULL,
        .next = sa->iter,
        .n = sa->nodes[0],
        .index = 0,
    };
    sa->iter = si;
    *res = si;
    return SKIPARRAY_ITER_NEW_OK;
}

void
skiparray_iter_free(struct skiparray_iter *iter) {
    if (iter == NULL) { return; }

    struct skiparray *sa = iter->sa;
    assert(sa != NULL);

    LOG(4, "%s: freeing %p; iter->prev %p, sa->iter %p\n",
        __func__, (void *)iter, (void *)iter->prev, (void *)sa->iter);

    if (iter->prev == NULL) {
        assert(sa->iter == iter);
        sa->iter = iter->next;
        if (iter->next != NULL) {
            iter->next->prev = NULL;
        }
    } else {                    /* unlink */
        iter->prev->next = iter->next;
        if (iter->next != NULL) {
            iter->next->prev = iter->prev;
        }
    }

    sa->mem(iter, 0, sa->udata);
}

void
skiparray_iter_seek_endpoint(struct skiparray_iter *iter,
    enum skiparray_iter_seek_endpoint end) {
    assert(iter != NULL);
    switch (end) {
    case SKIPARRAY_ITER_SEEK_FIRST:
        iter->n = iter->sa->nodes[0];
        iter->index = 0;
        break;
    case SKIPARRAY_ITER_SEEK_LAST:
        iter->n = last_node(iter->sa);
        iter->index = iter->n->count - 1;
        break;

    default:
        assert(false);
    }
}

enum skiparray_iter_seek_res
skiparray_iter_seek(struct skiparray_iter *iter,
    const void *key) {
    assert(iter != NULL);

    struct search_env env = {
        .sa = iter->sa,
        .key = key,
    };
    enum search_res sres = search(&env);
    assert(env.n != NULL);

    LOG(3, "%s: sres %d, got node %p, index %u\n",
        __func__, sres, (void *)env.n, env.index);

    switch (sres) {
    case SEARCH_FOUND:
        iter->n = env.n;
        iter->index = env.index;
        return SKIPARRAY_ITER_SEEK_FOUND;

    default:
    case SEARCH_EMPTY:
        assert(false);

    case SEARCH_NOT_FOUND:
        break;                  /* continue below */
    }

    if (env.index == 0 && env.n->back == NULL) {
        return SKIPARRAY_ITER_SEEK_ERROR_BEFORE_FIRST;
    }

    if (env.index == env.n->count) {
        env.n = env.n->fwd[0];
        if (env.n == NULL) { return SKIPARRAY_ITER_SEEK_ERROR_AFTER_LAST; }
        env.index = 0;
    }

    iter->n = env.n;
    iter->index = env.index;

    return SKIPARRAY_ITER_SEEK_NOT_FOUND;
}

enum skiparray_iter_step_res
skiparray_iter_next(struct skiparray_iter *iter) {
    assert(iter != NULL);

    iter->index++;
    LOG(4, "%s: index %"PRIu16", count %"PRIu16"\n",
        __func__, iter->index, iter->n->count);

    if (iter->index == iter->n->count) {
        if (iter->n->fwd[0] == NULL) {
            return SKIPARRAY_ITER_STEP_END;
        } else {
            iter->n = iter->n->fwd[0];
            iter->index = 0;
        }
    }
    return SKIPARRAY_ITER_STEP_OK;
}

enum skiparray_iter_step_res
skiparray_iter_prev(struct skiparray_iter *iter) {
    assert(iter != NULL);

    LOG(4, "%s: index %"PRIu16", count %"PRIu16"\n",
        __func__, iter->index, iter->n->count);

    if (iter->index == 0) {
        if (iter->n->back == NULL) {
            return SKIPARRAY_ITER_STEP_END;
        } else {
            iter->n = iter->n->back;
            iter->index = iter->n->count - 1;
        }
    } else {
        iter->index--;
    }
    return SKIPARRAY_ITER_STEP_OK;
}

void
skiparray_iter_get(struct skiparray_iter *iter,
    void **key, void **value) {
    assert(iter != NULL);

    LOG(2, "%s: index %u, node %p, count %u\n",
        __func__, iter->index, (void *)iter->n, iter->n->count);

    assert(iter->index < iter->n->count);
    uint16_t n = iter->n->offset + iter->index;
    if (key != NULL) {
        *key = iter->n->keys[n];
    }

    if (value != NULL && iter->sa->use_values) {
        *value = iter->n->values[n];
    }
}

enum skiparray_builder_new_res
skiparray_builder_new(const struct skiparray_config *cfg,
    bool skip_ascending_key_check, struct skiparray_builder **builder) {
    if (builder == NULL) { return SKIPARRAY_BUILDER_NEW_ERROR_MISUSE; }

    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(cfg, &sa);
    switch (nres) {
    default:
        assert(false);
    case SKIPARRAY_NEW_ERROR_NULL:
    case SKIPARRAY_NEW_ERROR_CONFIG:
        return SKIPARRAY_BUILDER_NEW_ERROR_MISUSE;
    case SKIPARRAY_NEW_ERROR_MEMORY:
        return SKIPARRAY_BUILDER_NEW_ERROR_MEMORY;
    case SKIPARRAY_NEW_OK:
        break;                  /* continue below */
    }

    struct skiparray_builder *b = NULL;
    const size_t alloc_size = sizeof(*b)
      + sa->max_level * sizeof(b->trail[0]);
    b = sa->mem(NULL, alloc_size, sa->udata);
    if (b == NULL) {
        skiparray_free(sa);
        return SKIPARRAY_BUILDER_NEW_ERROR_MEMORY;
    }
    memset(b, 0x00, alloc_size);

    b->sa = sa;
    b->last = sa->nodes[0];
    b->last->offset = 0;

    LOG(3, "%s: initializing builder with n %p (height %u)\n",
        __func__, (void *)b->last, b->last->height);

    for (size_t i = 0; i < b->last->height; i++) {
        b->trail[i] = b->last;
        LOG(3, "    -- b->trail[%zu] <- %p\n", i, (void *)b->last);
        assert(b->trail[i] == sa->nodes[i]);
    }

    b->check_ascending = !skip_ascending_key_check;
    b->has_prev_key = false;
    *builder = b;
    return SKIPARRAY_BUILDER_NEW_OK;
}

void
skiparray_builder_free(struct skiparray_builder *b) {
    if (b == NULL) { return; }
    assert(b->sa != NULL);
    struct skiparray *sa = b->sa;
    b->sa->mem(b, 0, sa->udata);
    skiparray_free(sa);
}

enum skiparray_builder_append_res
skiparray_builder_append(struct skiparray_builder *b,
    void *key, void *value) {
    assert(b != NULL);
    assert(b->sa != NULL);
    assert(b->last != NULL);

    struct skiparray *sa = b->sa;
    struct node *last = b->last;
    assert(last->offset == 0);

    /* reject key if <= previous; must be ascending */
    if (b->has_prev_key) {
        if (sa->cmp(key, b->prev_key, sa->udata) <= 0) {
            return SKIPARRAY_BUILDER_APPEND_ERROR_MISUSE;
        }
    }

    LOG(3, "%s: last is %p (%u height, %u count), sa height is %u\n",
        __func__, (void *)last, last->height, last->count, sa->height);

    /* If the current last node is full, then allocate a new last node
     * and connect back and forward pointers according to the trail. */
    if (last->count == sa->node_size) {
        uint8_t level = sa->level(sa->prng_state,
            &sa->prng_state, sa->udata) + 1;
        if (level >= sa->max_level) { level = sa->max_level - 1; }

        struct node *new = node_alloc(level + 1, sa->node_size,
            sa->mem, sa->udata, sa->use_values);
        if (new == NULL) {
            return SKIPARRAY_BUILDER_APPEND_ERROR_MEMORY;
        }
        LOG(3, "    -- new %p, height %u\n", (void *)new, new->height);

        for (size_t i = 0; i < last->height; i++) {
            if (i >= new->height) { break; }
            LOG(3, "    -- last->fwd[%zu] -> %p\n", i, (void *)new);
            last->fwd[i] = new;
        }
        for (size_t i = 0; i < new->height; i++) {
            if (b->trail[i] == NULL) {
                LOG(3, "    -- b->trail[%zu] -> %p\n", i, (void *)new);
                b->trail[i] = new;
            } else {
                LOG(3, "    -- b->trail(%p)->fwd[%zu] -> %p\n",
                    (void *)b->trail[i], i, (void *)new);
                assert(b->trail[i]->height > i);
                b->trail[i]->fwd[i] = new;
                LOG(3, "    -- b->trail[%zu] -> %p\n", i, (void *)new);
                b->trail[i] = new;
            }
        }

        /* If the new node is taller than the current SA height,
         * then increase it and update forward links. */
        while (new->height > sa->height) {
            sa->nodes[sa->height] = new;
            sa->height++;
        }
        new->back = last;
        assert(last->fwd[0] == new);
        new->offset = 0;
        last = new;
        b->last = last;
    }

    last->keys[last->count] = key;
    if (last->values != NULL) { last->values[last->count] = value; }
    last->count++;

    if (b->check_ascending) {
        b->has_prev_key = true;
        b->prev_key = key;
    }
    return SKIPARRAY_BUILDER_APPEND_OK;
}

void
skiparray_builder_finish(struct skiparray_builder **b,
    struct skiparray **sa) {
    assert(b != NULL);
    assert(sa != NULL);

    struct skiparray_builder *builder = *b;
    *b = NULL;
    assert(builder != NULL);

    *sa = builder->sa;
    (*sa)->mem(builder, 0, (*sa)->udata);
}

static struct node *
node_alloc(uint8_t height, uint16_t node_size,
    skiparray_memory_fun *mem, void *udata, bool use_values) {
    LOG(2, "%s: height %u, size %" PRIu16 "\n", __func__, height, node_size);
    assert(height >= 1);
    assert(node_size >= 2);

    struct node *res = NULL;
    void **keys = NULL;
    void **values = NULL;

    const size_t alloc_size = sizeof(struct node) +
      height * sizeof(struct node *);
    res = mem(NULL, alloc_size, udata);
    if (res == NULL) { goto cleanup; }
    memset(res, 0x00, alloc_size);

    keys = mem(NULL, node_size * sizeof(keys[0]), udata);
    if (keys == NULL) { goto cleanup; }
    memset(keys, 0x00, node_size * sizeof(keys[0]));

    if (use_values) {
        values = mem(NULL, node_size * sizeof(values[0]), udata);
        if (values == NULL) { goto cleanup; }
        memset(values, 0x00, node_size * sizeof(values[0]));
    }

    struct node fields = {
        .height = height,
        .offset = node_size / 2,
        .count = 0,
        .keys = keys,
        .values = values,
    };
    memcpy(res, &fields, sizeof(fields));
    for (uint8_t i = 0; i < height; i++) {
        res->fwd[i] = NULL;
    }
    return res;

cleanup:
    if (res != NULL) { mem(res, 0, udata); }
    if (keys != NULL) { mem(keys, 0, udata); }
    if (values != NULL) { mem(values, 0, udata); }
    return NULL;
}

static void
node_free(const struct skiparray *sa, struct node *n) {
    if (n == NULL) { return; }
    sa->mem(n->keys, 0, sa->udata);
    if (n->values != NULL) { sa->mem(n->values, 0, sa->udata); }
    sa->mem(n, 0, sa->udata);
}

/* Search for the index <= KEY within KEYS[KEY_COUNT] (according to CMP),
 * and write it in *INDEX. Return whether an exact match was found. */
bool
skiparray_bsearch(const void *key, const void * const *keys,
    size_t key_count, skiparray_cmp_fun *cmp, void *udata,
    uint16_t *index) {

#if LOG_LEVEL >= 4
    LOG(4, "====== %s\n", __func__);
    for (size_t i = 0; i < key_count; i++) {
        LOG(4, "%zu: %p\n", i, (void *)keys[i]);
    }
#endif

    assert(key_count > 0);
    int low = 0;
    int high = key_count;

    while (low < high) {
        int cur = (low + high)/2;

        int res = cmp(key, keys[cur], udata);
        LOG(3, "%s: low %d, high %d, cur %d: res %d\n",
            __func__, low, high, cur, res);
        if (res < 0) {
            high = cur;
            continue;
        } else if (res > 0) {
            low = cur + 1;
            continue;
        } else {
            *index = cur;
            return true;
        }
    }

    *index = low;
    return false;
}

static bool
search_within_node(const struct skiparray *sa,
    const void *key, const struct node *n, uint16_t *index) {
    return skiparray_bsearch(key, (const void * const *)&n->keys[n->offset],
        n->count, sa->cmp, sa->udata, index);
}

/* Search the chains of nodes, starting at the highest level, and
 * find the node and position in which the key would fit. */
static enum search_res
search(struct search_env *env) {
    bool found = false;
    const struct skiparray *sa = env->sa;
    assert(sa->height >= 1);
    int level = sa->height - 1;
    struct node *prev = NULL;

    skiparray_cmp_fun *cmp = sa->cmp;
    void *udata = sa->udata;
    assert(cmp != NULL);

    struct node *cur = sa->nodes[level];
    LOG(2, "%s: level %d: cur %p\n", __func__, level, (void *)cur);
    assert(cur != NULL);
    if (cur->count == 0) {
        LOG(2, "%s: empty head => NOT_FOUND\n", __func__);
        env->n = cur;
        return SEARCH_NOT_FOUND;
    }

    if (LOG_LEVEL >= 3) {
        assert(level >= 0);
        for (size_t i = 0; i <= (size_t)level; i++) {
            LOG(3, "%s: sa->nodes[%zu]: %p\n",
                __func__, i, (void *)sa->nodes[i]);
        }
    }

    for (;;) {
        assert(cur != NULL);
        assert(cur->count > 0);

        /* Eliminating redundant comparisons after dropping a level
         * doesn't appear to make a significant difference time-wise. */
        const int cmp_res = cmp(env->key,
            cur->keys[cur->offset + cur->count - 1], udata);

        LOG(2, "%s: level %d, cur %p, cmp_res %d\n",
            __func__, level, (void *)cur, cmp_res);

        if (cmp_res < 0) {     /* key < this node's last key */
            /* either in this node or not at all */
            if (level == 0) {   /* find exact pos and return */
                found = search_within_node(sa, env->key, cur, &env->index);
                LOG(2, "%s: < -- on level 0, found? %d\n", __func__, found);
                /* If adding a binding to the beginning, put it in the end
                 * of the previous one if it's less full. */
                if (!found && env->index == 0) {
                    struct node *back = cur->back;
                    if (back != NULL && back->count < cur->count) {
                        LOG(2, "%s: choosing end of previous node %p rather than start of %p\n",
                            __func__, (void *)back, (void *)cur);
                        env->index = back->count;
                        cur = back;
                    }
                }
                break;
            } else {            /* descend */
                LOG(2, "%s: < -- descending\n", __func__);
                level--;
                cur = (prev ? prev->fwd[level] : sa->nodes[level]);
                assert(cur != NULL);
            }
        } else if (cmp_res > 0) { /* key > this node's last key */
            struct node *next = cur->fwd[level];
            if (next != NULL) { /* advance */
                LOG(2, "%s: > advancing to %p\n", __func__, (void *)next);
                prev = cur;
                cur = next;
                assert(cur != NULL);
            } else {            /* descend */
                LOG(2, "%s: > descending\n", __func__);
                if (level == 0) {
                    LOG(2, "%s: > setting index: %" PRIu16 "\n",
                        __func__, cur->count);
                    env->index = cur->count;
                    break;
                }

                if (prev == NULL) {
                    level--;
                    cur = sa->nodes[level];
                } else {
                    /* keep descending and looking for a forward pointer */
                    struct node *ncur = NULL;
                    do {
                        level--;
                        ncur = prev->fwd[level];
                    } while ((ncur == NULL || ncur == cur) && level > 0);
                    cur = ncur;
                }

                assert(cur != NULL);
            }
        } else {                /* exact match: last node key */
            found = true;
            env->index = cur->count - 1;
            LOG(2, "%s: == index = %" PRIu16 "\n",
                __func__, env->index);
            if (level == 0) {
                break;
            } else {
                level--;
                cur = (prev ? prev->fwd[level] : sa->nodes[level]);
                assert(cur != NULL);
            }
        }
    }

    if (found) { assert(cur != NULL); }
    env->n = cur;

    LOG(2, "%s: exiting with found %d, env->n %p, env->index %" PRIu16 "\n",
        __func__, found, (void *)env->n, env->index);
    return (found ? SEARCH_FOUND : SEARCH_NOT_FOUND);
}

static void
prepare_node_for_insert(struct skiparray *sa,
        struct node *n, uint16_t index) {
    assert(n->count < sa->node_size); /* must fit */

    LOG(2, "%s: inserting @ %" PRIu16 " on %p, node offset %" PRIu16
        ", count %" PRIu16 "\n",
        __func__, index, (void *)n, n->offset, n->count);

    dump_raw_bindings("BEFORE insert", sa, n);

    if (index == 0) {           /* shift forward or reduce offset */
        if (n->count > 0 && n->offset > 0) {
            LOG(2, "%s: reducing offset by 1\n", __func__);
            n->offset--;
        } else {                /* shift all forward */
            LOG(2, "%s: shifting all forward by 1\n", __func__);
            shift_pairs(n, n->offset + 1, n->offset, n->count);
        }
    } else if (index < n->count) { /* shift middle */
        if (n->offset > 0) {    /* prefer shifting backward */
            LOG(2, "%s: shifting pairs up to position back 1\n", __func__);
            const uint16_t to_move = index + 1;
            shift_pairs(n, n->offset - 1, n->offset, to_move);
            n->offset--;
        } else {                /* shift forward */
            LOG(2, "%s: shifting pairs after position forward 1\n", __func__);
            assert(n->offset == 0);
            const uint16_t to_move = n->count - index;;
            shift_pairs(n, index + 1, index, to_move);
        }
    } else {                    /* inserting at end */
        assert(index == n->count);
        assert(n->offset + index <= sa->node_size);
        if (n->offset + index == sa->node_size) { /* shift all back */
            LOG(2, "%s: shifting to front, changing offset to 0\n", __func__);
            assert(n->offset > 0);
            shift_pairs(n, 0, n->offset, n->count);
            n->offset = 0;
        } else {
            LOG(2, "%s: no-op \n", __func__);
        }
    }

    LOG(2, "%s: adjusted node offset %" PRIu16 "\n", __func__, n->offset);
    if (LOG_LEVEL >= 4) {
        dump_raw_bindings("AFTER insert", sa, n);
    }
}

static bool
split_node(struct skiparray *sa,
    struct node *n, struct node **res) {
    uint8_t level = sa->level(sa->prng_state,
        &sa->prng_state, sa->udata) + 1;
    if (level >= sa->max_level) { level = sa->max_level - 1; }

    struct node *new = node_alloc(level + 1, sa->node_size,
        sa->mem, sa->udata, sa->use_values);
    if (new == NULL) {
        return false;
    }

    if (LOG_LEVEL >= 4) {
        dump_raw_bindings("BEFORE split n", sa, n);
    }

    /* Half the keys and values get moved to the new node. Round down
     * and insert at the beginning, in case of sequential insertion. */
    const uint16_t to_move = n->count / 2;
    assert(to_move > 0);
    new->offset = 0;

    memcpy(&new->keys[new->offset],
        &n->keys[n->offset + n->count - to_move],
        to_move * sizeof(n->keys[0]));
    if (sa->use_values) {
        memcpy(&new->values[new->offset],
            &n->values[n->offset + n->count - to_move],
            to_move * sizeof(n->values[0]));
    }
    n->count -= to_move;
    new->count += to_move;
    new->back = n;

    if (LOG_LEVEL >= 4) {
        dump_raw_bindings("AFTER split n", sa, n);
        dump_raw_bindings("AFTER split new", sa, new);
    }

    *res = new;
    LOG(2, "%s: split node %p (height %u) to %p (height %u), with %u pairs\n",
        __func__, (void *)n, n->height, (void *)new, new->height, new->count);
    return true;
}

static void
shift_or_merge(struct skiparray *sa, struct node *n) {
    LOG(2, "%s: checking %p (prev %p, next %p)\n",
        __func__, (void *)n, (void *)n->back, (void *)n->fwd[0]);

    /* Special case: If this is the only node, do nothing -- the root
     * node is allowed to be empty. */
    if (n == sa->nodes[0] && n->fwd[0] == NULL) {
        LOG(2, "%s: special case, allowing head to be empty\n", __func__);
        return;
    }
    const uint16_t required = sa->node_size/2;
    assert(n->count < required); /* node too empty */

    struct node *next = n->fwd[0];
    if (next == NULL) {
        assert(n->back != NULL);
        struct node *prev = n->back;

        /* under-filled last node: possibly combine with previous */
        if (prev->count + n->count <= sa->node_size) { /* contents will fit */
            LOG(2, "%s: contents will fit in prev, moving and deleting\n",
                __func__);
            /* move to front, to make room */
            shift_pairs(prev, 0, prev->offset, prev->count);
            prev->offset = 0;

            /* move all pairs */
            move_pairs(prev, n, prev->count, n->offset, n->count);
            prev->count += n->count;

            if (n->fwd[0] != NULL) { n->fwd[0]->back = prev; }
            unlink_node(sa, n);
        } else {
            /* leave alone this time */
            LOG(2, "%s: contents (%" PRIu16 ") won't fit in prev (%"
                PRIu16 "), leaving alone\n", __func__, n->count, prev->count);
        }
    } else if (next->count + n->count <= sa->node_size) { /* merge */
        LOG(2, "%s: merging %p with next node %p (%" PRIu16 " + %" PRIu16 ")\n",
            __func__, (void *)n, (void *)next, n->count, next->count);

        if (LOG_LEVEL >= 4) {
            dump_raw_bindings("PRE_MERGE n", sa, n);
            dump_raw_bindings("PRE_MERGE next", sa, next);
        }

        if (n->offset > 0) {
            /* move to front, to make room */
            shift_pairs(n, 0, n->offset, n->count);
            n->offset = 0;
        }

        move_pairs(n, next, n->count, next->offset, next->count);
        n->count += next->count;

        unlink_node(sa, next);

        dump_raw_bindings("MERGED", sa, n);
    } else {                    /* shift pairs over */
        const uint16_t to_move = next->count - required;
        LOG(2, "%s: moving %" PRIu16 " pairs from next node (%p) to %p\n",
            __func__, to_move, (void *)next, (void *)n);
        if (n->offset > 0) {
            /* move to front, to make room */
            shift_pairs(n, 0, n->offset, n->count);
            n->offset = 0;
        }

        move_pairs(n, next, n->count, next->offset, to_move);

        next->count -= to_move;
        next->offset += to_move;
        n->count += to_move;
        assert(next->count == required);
        assert(n->count <= sa->node_size);

    }
}

/* Search to find the next-to-last nodes and unlink the now-empty last
 * node from them. */
static void
unlink_node(struct skiparray *sa, struct node *n) {
    LOG(2, "%s: unlinking empty node %p\n", __func__, (void *)n);

    if (n == sa->nodes[0]) {
        assert(n->fwd[0] != NULL); /* never unlink empty first node */
    }

    for (int level = sa->height - 1; level >= 0; level--) {
        if (sa->nodes[level] == n) {
            sa->nodes[level] = n->fwd[level];
            LOG(2, "%s: sa->nodes[%d] <- %p\n",
                __func__, level, (void *)sa->nodes[level]);
        }
    }
    while (sa->height > 1 && sa->nodes[sa->height - 1] == NULL) { sa->height--; }

    /* Since the node is empty, compare against either the last key
     * in the previous node or the first key in the next. One of
     * them must be available. */
    const struct node *nearest = (n->back ? n->back : n->fwd[0]);
    assert(nearest != NULL);
    const uint16_t nearest_index = (n->back
        ? nearest->offset + nearest->count - 1
        : nearest->offset);
    /* If using the key in the last node before, compare with <= instead of <. */
    const int cmp_condition = (n->back ? 1 /* res <= 0*/ : 0 /* res < 0 */);
    assert(nearest);

    int level = sa->height - 1;
    struct node *cur = NULL;
    while (level >= 0) {
        LOG(2, "%s: level %d, cur %p\n", __func__, level, (void *)cur);
        /* Get the first node (if any) before the unlinked node. */
        if (cur == NULL) {
            struct node *head = sa->nodes[level];
            if (head != NULL) {
                int res = sa->cmp(head->keys[head->offset + head->count - 1],
                    nearest->keys[nearest_index], sa->udata);
                if (res < cmp_condition) {
                    cur = head;
                } else {
                    level--;
                    continue;
                }
            }
        }

        assert(cur != NULL);

        /* Check for overshooting, advance, and unlink the node if found. */
        if (cur->fwd[level] == NULL) {
            level--;
        } else if (cur->fwd[level] == n) {
            LOG(2, "%s: unlinking node %p on level %d\n",
                __func__, (void *)n, level);
            struct node *nfwd = n->fwd[level];
            cur->fwd[level] = nfwd; /* unlink */
            if (nfwd != NULL && level == 0) {
                nfwd->back = cur; /* update back pointer */
            }
            level--;
            continue;
        } else {
            assert(cur);
            struct node *next = cur->fwd[level];
            if (next == NULL) {
                level--;
                continue;
            }
            int res = sa->cmp(next->keys[next->offset + next->count - 1],
                nearest->keys[nearest_index], sa->udata);
            LOG(2, "%s: cmp_res %d\n", __func__, res);
            if (res < cmp_condition) {
                LOG(2, "%s: advancing on level %d, %p => %p\n",
                    __func__, level, (void *)cur, (void *)next);
                cur = next;
            } else {
                LOG(2, "%s: overshot, descending\n", __func__);
                level--;
            }
        }
    }
    node_free(sa, n);
}

static void
shift_pairs(struct node *n,
    uint16_t to_pos, uint16_t from_pos, uint16_t count) {
    memmove(&n->keys[to_pos],
        &n->keys[from_pos],
        count * sizeof(n->keys[0]));
    if (n->values != NULL) {
        memmove(&n->values[to_pos],
            &n->values[from_pos],
            count * sizeof(n->values[0]));
    }
}

static void
move_pairs(struct node *to, struct node *from,
    uint16_t to_pos, uint16_t from_pos, uint16_t count) {
    memcpy(&to->keys[to_pos],
        &from->keys[from_pos],
        count * sizeof(to->keys[0]));
    if (to->values != NULL) {
        memcpy(&to->values[to_pos],
            &from->values[from_pos],
            count * sizeof(to->values[0]));
    }
}

static void
dump_raw_bindings(const char *tag,
    const struct skiparray *sa, const struct node *n) {
    if (LOG_LEVEL > 4) {
        LOG(4, "====== %s\n", tag);
        for (size_t i = 0; i < sa->node_size; i++) {
            LOG(4, "%zu: %p => %p\n", i, (void *)n->keys[i],
                n->values ? (void *)n->values[i] : NULL);
        }
    }
}

static void *
def_memory_fun(void *p, size_t nsize, void *udata) {
    (void)udata;
    if (p != NULL) {
        assert(nsize == 0);     /* no realloc used */
        free(p);
        return NULL;
    } else {
        return malloc(nsize);
    }
}

#include "splitmix64_stateless.h"

static int
def_level_fun(uint64_t prng_state_in,
    uint64_t *prng_state_out, void *udata) {
    (void)udata;

    uint64_t next = splitmix64_stateless(prng_state_in);
    *prng_state_out = next;
    LOG(4, "%s: %"PRIx64" -> %"PRIx64"\n", __func__, prng_state_in, next);
    for (uint8_t i = 0; i < SKIPARRAY_DEF_MAX_LEVEL; i++) {
        if ((next & (1LLU << i)) == 0) {
            return i;
        }
    }
    return SKIPARRAY_DEF_MAX_LEVEL;
}
