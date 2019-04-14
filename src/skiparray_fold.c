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

#include "skiparray_fold_internal.h"

#ifdef SKIPARRAY_LOG_FOLD
#define LOG(...) fprintf(stdout, __VA_ARGS__)
#else
#define LOG(...)
#endif

enum skiparray_fold_res
skiparray_fold(enum skiparray_fold_type type,
    struct skiparray *sa, skiparray_fold_fun *cb, void *udata,
    struct skiparray_fold_state **fs) {
    return skiparray_fold_multi(type, 1, &sa,
        cb, NULL, udata, fs);
}

enum skiparray_fold_res
skiparray_fold_multi(enum skiparray_fold_type type,
    uint8_t skiparray_count, struct skiparray **skiparrays,
    skiparray_fold_fun *cb, skiparray_fold_merge_fun *merge, void *udata,
    struct skiparray_fold_state **fs) {

    if (skiparrays == NULL || skiparray_count < 1 || cb == NULL || fs == NULL) {
        return SKIPARRAY_FOLD_ERROR_MISUSE;
    }

    if (skiparray_count > 1 && merge == NULL) {
        return SKIPARRAY_FOLD_ERROR_MISUSE;
    }

    /* All skiparrays must have the same cmp and mem callbacks,
     * and either all or none must use values. */
    for (size_t i = 0; i < skiparray_count; i++) {
        if (skiparrays[i] == NULL
            || skiparrays[i]->cmp != skiparrays[0]->cmp
            || skiparrays[i]->mem != skiparrays[0]->mem
            || skiparrays[i]->use_values != skiparrays[0]->use_values) {
            return SKIPARRAY_FOLD_ERROR_MISUSE;
        }
    }

    skiparray_memory_fun *mem = skiparrays[0]->mem;
    void *sa_udata = skiparrays[0]->udata;

    uint8_t *current_ids = NULL;
    struct skiparray_fold_state *res = NULL;
    const size_t res_alloc_size = sizeof(*res)
      + skiparray_count*sizeof(res->iters[0]);
    res = mem(NULL, res_alloc_size, sa_udata);
    if (res == NULL) { goto cleanup; }
    memset(res, 0x00, res_alloc_size);

    const size_t current_ids_alloc_size = skiparray_count * sizeof(uint8_t);
    current_ids = mem(NULL, current_ids_alloc_size, sa_udata);
    if (current_ids == NULL) { goto cleanup; }
    memset(current_ids, 0x00, current_ids_alloc_size);

    for (size_t i = 0; i < skiparray_count; i++) {
        struct skiparray_iter *iter = NULL;
        enum skiparray_iter_new_res ires;
        ires = skiparray_iter_new(skiparrays[i], &iter);
        switch (ires) {
        default:
            assert(false);
        case SKIPARRAY_ITER_NEW_ERROR_MEMORY:
            goto cleanup;
        case SKIPARRAY_ITER_NEW_EMPTY:
        case SKIPARRAY_ITER_NEW_OK:
            break;                  /* continue below */
        }

        if (iter && type == SKIPARRAY_FOLD_RIGHT) {
            skiparray_iter_seek_endpoint(iter, SKIPARRAY_ITER_SEEK_LAST);
        }

        res->iters[i].iter = iter; /* can be NULL -- immediately empty */
        res->iter_count++;
    }

    res->type = type;
    res->use_values = skiparrays[0]->use_values;

    res->cbs.fold = cb;
    res->cbs.fold_udata = udata;
    res->cbs.mem = mem;
    res->cbs.sa_udata = sa_udata;
    res->cbs.cmp = skiparrays[0]->cmp;
    res->cbs.merge = merge;

    assert(res->iter_count == skiparray_count);
    res->iter_live = res->iter_count;

    res->ids.current = current_ids;

    *fs = res;
    return SKIPARRAY_FOLD_OK;

cleanup:
    if (current_ids != NULL) { mem(current_ids, 0, sa_udata); }
    if (res != NULL) {
        for (size_t i = 0; i < res->iter_count; i++) {
            skiparray_iter_free(res->iters[i].iter);
        }
        mem(res, 0, sa_udata);
    }
    return SKIPARRAY_FOLD_ERROR_MEMORY;
}

void
skiparray_fold_halt(struct skiparray_fold_state *fs) {
    if (fs == NULL) { return; }
    assert(fs->iter_count > 0);

    for (size_t i = 0; i < fs->iter_count; i++) {
        if (fs->iters[i].iter != NULL) {
            skiparray_iter_free(fs->iters[i].iter);
        }
    }

    if (fs->ids.current != NULL) {
        fs->cbs.mem(fs->ids.current, 0, fs->cbs.sa_udata);
    }

    fs->cbs.mem(fs, 0, fs->cbs.sa_udata);
}

enum skiparray_fold_next_res
skiparray_fold_next(struct skiparray_fold_state *fs) {
    assert(fs != NULL);
    LOG("%s: ids.available %zu, live %zu, count %zu\n",
        __func__,
        (size_t)fs->ids.available,
        (size_t)fs->iter_live,
        (size_t)fs->iter_count);
    assert(fs->ids.available <= fs->iter_count);
    assert(fs->iter_live <= fs->iter_count);

    if (fs->iter_live == 0 && fs->ids.available == 0) {
        skiparray_fold_halt(fs);
        return SKIPARRAY_FOLD_NEXT_DONE;
    }

    if (fs->iter_live > 0) { step_active_iterators(fs); }
    call_with_next(fs, fs->iter_count);
    return SKIPARRAY_FOLD_NEXT_OK;
}

static void
step_active_iterators(struct skiparray_fold_state *fs) {
    /* This could use a next chain, rather than walking the entire
     * array, but it's probably not worth the complexity since
     * the iterator count is likely to be small. */
    assert(fs->iter_live > 0);

    for (size_t i_i = 0; i_i < fs->iter_count; i_i++) {
        struct iter_state *is = &fs->iters[i_i];
        LOG("%s: %zu -- %p, %d\n", __func__, i_i, (void *)is->iter, is->state);
        if (is->iter == NULL) { continue; }     /* done */
        if (is->state != PS_NONE) { continue; } /* ids.available */

        struct skiparray_pair *p = &is->pair;
        skiparray_iter_get(is->iter, &p->key, &p->value);

        insert_pair(fs, i_i);
        assert(is->state != PS_NONE); /* set during insertion */
        LOG("%s: set %zu's state to %d, %p => %p\n",
            __func__, i_i, is->state, p->key, p->value);

        enum skiparray_iter_step_res sres;
        if (fs->type == SKIPARRAY_FOLD_RIGHT) {
            sres = skiparray_iter_prev(is->iter);
        } else if (fs->type == SKIPARRAY_FOLD_LEFT) {
            sres = skiparray_iter_next(is->iter);
        } else {
            assert(!"unreachable");
        }

        if (sres == SKIPARRAY_ITER_STEP_END) {
            LOG("%s: done: %zu\n", __func__, i_i);
            skiparray_iter_free(is->iter);
            is->iter = NULL;
            assert(fs->iter_live > 0);
            fs->iter_live--;
            continue;
        }
        assert(sres == SKIPARRAY_ITER_STEP_OK);
    }
}

static void
insert_pair(struct skiparray_fold_state *fs, size_t iter_i) {
    /* This could use binary search, but again, the iterator
     * count is likely to be small. */
    uint8_t *ids = fs->ids.current;

    if (fs->ids.offset > 0) {
        memmove(&ids[0], &ids[fs->ids.offset], fs->ids.available);
        fs->ids.offset = 0;
    }

    struct iter_state *is = &fs->iters[iter_i];
    const void *key = is->pair.key;
    LOG("%s: %p => %p\n", __func__, key, is->pair.value);

    for (size_t ci_i = 0; ci_i < fs->ids.available; ci_i++) {
        void *other = fs->iters[ids[ci_i]].pair.key;
        const int cmp_res = fs->cbs.cmp(key, other, fs->cbs.sa_udata);
        if (cmp_res <= 0) {      /* shift forward */
            memmove(&ids[ci_i + 1], &ids[ci_i], fs->ids.available - ci_i);
            ids[ci_i] = iter_i;
            is->state = (cmp_res == 0) ? PS_AVAILABLE_EQ : PS_AVAILABLE_LT;
            fs->ids.available++;
            return;
        } else {
            assert(cmp_res > 0);
            continue;
        }
    }

    /* > last */
    assert(iter_i <= UINT8_MAX);
    ids[fs->ids.available] = (uint8_t)iter_i;
    is->state = PS_AVAILABLE_LT;
    fs->ids.available++;
}

static void
call_with_next(struct skiparray_fold_state *fs, size_t count) {
    /* Get the next entries, starting at &ids.current[ids.offset]; if LT
     * then just call the fold callback on it; if EQ then collect all
     * the equal pairs and call merge first. */
    assert(fs->ids.available > 0);
    const uint8_t base = fs->ids.offset;

    struct iter_state *first = &fs->iters[fs->ids.current[base]];
    if (first->state == PS_AVAILABLE_LT) {
        struct skiparray_pair *p = &first->pair;
        LOG("%s: p %p => %p\n", __func__, (void *)p->key, (void *)p->value);
        fs->cbs.fold(p->key, p->value, fs->cbs.fold_udata);
        fs->ids.available--;
        fs->ids.offset++;
        first->state = PS_NONE;
        return;
    }

    assert(first->state == PS_AVAILABLE_EQ);

    /* Given N key/value pairs, choose a key (by ID) and a merged value
     * (which can point to an existing value or a new allocation). */

    void *keys[count];
    void *values[count];
    uint8_t used = 0;

    for (size_t id_i = 0; id_i < fs->ids.available; id_i++) {
        uint8_t id = fs->ids.current[id_i + base];
        struct iter_state *is = &fs->iters[id];
        if (is->state != PS_AVAILABLE_EQ) { break; }
        keys[used] = is->pair.key;
        values[used] = (fs->use_values ? is->pair.value : NULL);
        used++;
    }
    assert(used > 0);

    void *merged_value = NULL;
    const uint8_t choice = fs->cbs.merge(used,
        (const void **)keys, values, &merged_value, fs->cbs.fold_udata);
    LOG("%s: choice %u\n", __func__, choice);
    assert(choice < used);

    fs->cbs.fold(keys[choice], merged_value, fs->cbs.fold_udata);

    fs->ids.available -= used;
    fs->ids.offset += used;

    for (size_t id_i = 0; id_i < used; id_i++) {
        uint8_t id = fs->ids.current[id_i + base];
        struct iter_state *is = &fs->iters[id];
        is->state = PS_NONE;
    }
}
