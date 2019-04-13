#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <getopt.h>

#include "skiparray.h"

#include <sys/time.h>

static const size_t usec_per_sec = 1000000L;
static const size_t msec_per_sec = 1000L;

static size_t
get_usec_delta(struct timeval *pre, struct timeval *post) {
    return (usec_per_sec * (post->tv_sec - pre->tv_sec)
        + (post->tv_usec - pre->tv_usec));
}

#define TIME(NAME)                                                      \
        struct timeval timer_##NAME = { 0, 0 };                         \
        int timer_res_##NAME = gettimeofday(&timer_##NAME, NULL);       \
        (void)timer_res_##NAME;                                         \
        assert(0 == timer_res_##NAME);                                  \

#define CMP_TIME(LABEL, LIMIT, N1, N2)                                  \
    do {                                                                \
        size_t usec_delta = get_usec_delta(&timer_##N1, &timer_##N2);   \
        double usec_per = usec_delta / (double)LIMIT;                   \
        double per_second = usec_per_sec / usec_per;                    \
        printf("%-30s limit %9zu %9.3f msec, %6.3f usec per, "          \
            "%11.3f K ops/sec",                                         \
            LABEL, LIMIT, usec_delta / (double)msec_per_sec,            \
            usec_per, per_second / 1000);                               \
        if (track_memory) {                                             \
            printf(", %g MB hwm, %g w/e",                               \
                memory_hwm / (1024.0 * 1024),                           \
                memory_hwm / (1.0 * sizeof(void *) * LIMIT));           \
        }                                                               \
        printf("\n");                                                   \
    } while(0)                                                          \

#define TDIFF() CMP_TIME(__func__, limit, pre, post)

#define MAX_LIMITS 64
#define DEF_LIMIT ((size_t)1000000)
#define DEF_CYCLES ((size_t)1)

static const int prime = 7919;
static size_t cycles = DEF_CYCLES;
static uint8_t limit_count = 0;
static size_t limits[MAX_LIMITS];
static size_t node_size = SKIPARRAY_DEF_NODE_SIZE;
static const char *name;
static bool track_memory;
static size_t memory_used;
static size_t memory_hwm;       /* allocation high-water mark */

static void
usage(void) {
    fprintf(stderr, "Usage: benchmarks [-c <cycles>] [-l <limit>] [-m] [-n <name>]\n\n");
    fprintf(stderr, "  -c: run multiple cycles of benchmarks (def. 1)\n");
    fprintf(stderr, "  -l: set limit(s); comma-separated, default %zu.\n", DEF_LIMIT);
    fprintf(stderr, "  -m: track the memory high-water mark, in MB and words/entry.\n");
    fprintf(stderr, "  -n: run one benchmark. 'help' prints available benchmarks.\n");
    exit(EXIT_FAILURE);
}

static int cmp_size_t(const void *pa, const void *pb) {
    const size_t a = *(size_t *)pa;
    const size_t b = *(size_t *)pb;
    return a < b ? -1 : a > b ? 1 : 0;
}

static bool
parse_limits(char *optarg) {
    char *arg = strtok(optarg, ",");
    while (arg) {
        size_t nlimit = strtoul(arg, NULL, 0);
        if (nlimit <= 1) { return false; }
        limits[limit_count] = nlimit;
        if (limit_count == MAX_LIMITS) {
            fprintf(stderr, "Error: Too many limits (max %d)\n", (int)MAX_LIMITS);
            exit(EXIT_FAILURE);
        }
        limit_count++;
        arg = strtok(NULL, ",");
    }

    qsort(limits, limit_count, sizeof(limits[0]), cmp_size_t);
    return true;
}

static void
handle_args(int argc, char **argv) {
    int fl;
    while ((fl = getopt(argc, argv, "hc:l:mn:s:")) != -1) {
        switch (fl) {
        case 'h':               /* help */
            usage();
            break;
        case 'c':               /* cycles */
            cycles = strtoul(optarg, NULL, 0);
            if (cycles == 0) {
                fprintf(stderr, "Bad cycles: %zu\n", cycles);
                usage();
            }
            break;
        case 'l':               /* limit */
            if (!parse_limits(optarg)) {
                fprintf(stderr, "Bad limit(s): %s\n", optarg);
                usage();
            }
            break;
        case 'm':               /* memory */
            track_memory = true;
            break;
        case 'n':               /* name */
            name = optarg;
            break;
        case 's':               /* node_size */
            node_size = strtoul(optarg, NULL, 0);
            if (node_size < 2) {
                fprintf(stderr, "Bad node_size: %zu.\n", node_size);
                usage();
            }
            break;
        case '?':
        default:
            usage();
        }
    }
}

static int
cmp_intptr_t(const void *ka,
    const void *kb, void *udata) {
    (void)udata;
    intptr_t a = (intptr_t)ka;
    intptr_t b = (intptr_t)kb;
    return (a < b ? -1 : a > b ? 1 : 0);
}

static struct skiparray_config sa_config = {
    .cmp = cmp_intptr_t,
    .seed = 0,
};

static struct skiparray_config sa_config_no_values;


/* Measure insertions. */
/* Measure getting existing values (successful lookup). */
static void
get_sequential(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        intptr_t v = 0;
        skiparray_get(sa, (void *) k, (void **)&v);
        assert(v == k);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

/* Measure getting existing values (successful lookup). */
static void
get_random_access(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        intptr_t v = 0;
        skiparray_get(sa, (void *) k, (void **)&v);
        assert(v == k);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

/* Same, but only use keys. */
static void
get_random_access_no_values(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config_no_values, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        skiparray_get(sa, (void *) k, NULL);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

/* Measure getting _nonexistent_ values (lookup failure). */
static void
get_nonexistent(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = ((i * prime) % limit) + limit;
        intptr_t v = 0;
        skiparray_get(sa, (void *) k, (void **)&v);
        assert(v == 0);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
count(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    size_t count = skiparray_count(sa);
    assert(count == limit);
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_sequential(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        skiparray_set(sa, (void *) k, (void *) k);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_sequential_builder(size_t limit) {
    struct skiparray_builder *b = NULL;

    enum skiparray_builder_new_res bnres = skiparray_builder_new(&sa_config,
        false, &b);
    (void)bnres;

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        enum skiparray_builder_append_res bares =
          skiparray_builder_append(b, (void *) k, (void *) k);
        (void)bares;
    }

    struct skiparray *sa = NULL;
    skiparray_builder_finish(&b, &sa);
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_sequential_builder_no_chk(size_t limit) {
    struct skiparray_builder *b = NULL;

    enum skiparray_builder_new_res bnres = skiparray_builder_new(&sa_config,
        true, &b);
    (void)bnres;

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        enum skiparray_builder_append_res bares =
          skiparray_builder_append(b, (void *) k, (void *) k);
        (void)bares;
    }

    struct skiparray *sa = NULL;
    skiparray_builder_finish(&b, &sa);
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_random_access(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        skiparray_set(sa, (void *) k, (void *) k);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_random_access_no_values(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config_no_values, &sa);
    (void)nres;

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        skiparray_set(sa, (void *) k, NULL);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_replacing_sequential(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        skiparray_set(sa, (void *) k, (void *) k);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        skiparray_set(sa, (void *) k, (void *) (k + 1));
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
set_replacing_random_access(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        skiparray_set(sa, (void *) k, (void *) k);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        skiparray_set(sa, (void *) k, (void *) (k + 1));
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
forget_sequential(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = i;
        (void)skiparray_forget(sa, (void *) k, NULL);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
forget_random_access(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        (void)skiparray_forget(sa, (void *) k, NULL);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
forget_random_access_no_values(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config_no_values, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, NULL);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) % limit;
        (void)skiparray_forget(sa, (void *)k, NULL);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
forget_nonexistent(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = (i * prime) + limit;
        (void)skiparray_forget(sa, (void *) k, NULL);
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
pop_first(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = 0, v = 0;
        enum skiparray_pop_res res = skiparray_pop_first(sa,
            (void *) &k, (void *) &v);
        if (res == SKIPARRAY_POP_EMPTY) { assert(false); }
        assert(res >= 0);
        assert(v == k);
        (void) res;
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
pop_last(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        intptr_t k = 0, v = 0;
        int res = skiparray_pop_last(sa, (void *) &k, (void *) &v);
        assert(res >= 0);
        assert(v == k);
        (void) res;
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
member_sequential(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        assert(skiparray_member(sa, (void *)i));
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
member_random_access(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);
    for (size_t i = 0; i < limit; i++) {
        size_t k = (i * prime) % limit;
        assert(skiparray_member(sa, (void *)k));
    }
    TIME(post);

    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

static void
sum(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    uintptr_t actual = 0;
    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
        actual += i;
    }

    TIME(pre);
    uintptr_t total = 0;

    struct skiparray_iter *iter = NULL;
    if (SKIPARRAY_ITER_NEW_OK != skiparray_iter_new(sa, &iter)) {
        assert(false);
    }

    skiparray_iter_seek_endpoint(iter, SKIPARRAY_ITER_SEEK_FIRST);

    do {
        void *k, *v;
        skiparray_iter_get(iter, &k, &v);
        total += (uintptr_t)v;
    } while (skiparray_iter_next(iter) == SKIPARRAY_ITER_STEP_OK);

    skiparray_iter_free(iter);

    TIME(post);
    TDIFF();
    skiparray_free(sa, NULL, NULL);

    assert(total == actual);
}

static void
sum_partway(size_t limit) {
    struct skiparray *sa = NULL;
    enum skiparray_new_res nres = skiparray_new(&sa_config, &sa);
    (void)nres;

    for (size_t i = 0; i < limit; i++) {
        skiparray_set(sa, (void *)i, (void *)i);
    }

    TIME(pre);

    struct skiparray_iter *iter = NULL;
    if (SKIPARRAY_ITER_NEW_OK != skiparray_iter_new(sa, &iter)) {
        assert(false);
    }

    const uintptr_t starting_point = limit / 2;

    if (SKIPARRAY_ITER_SEEK_FOUND !=
        skiparray_iter_seek(iter, (const void *)starting_point)) {
        assert(false);
    }

    do {
        void *k, *v;
        skiparray_iter_get(iter, &k, &v);
        (void)v;
    } while (skiparray_iter_next(iter) == SKIPARRAY_ITER_STEP_OK);

    skiparray_iter_free(iter);

    TIME(post);
    TDIFF();
    skiparray_free(sa, NULL, NULL);
}

typedef void
benchmark_fun(size_t limit);

struct benchmark {
    const char *name;
    benchmark_fun *fun;
};

static struct benchmark benchmarks[] = {
    { "get_sequential", get_sequential },
    { "get_random_access", get_random_access },
    { "get_random_access_no_values", get_random_access_no_values },
    { "get_nonexistent", get_nonexistent },
    { "set_sequential", set_sequential },
    { "set_sequential_builder", set_sequential_builder },
    { "set_sequential_builder_no_chk", set_sequential_builder_no_chk },
    { "set_random_access", set_random_access },
    { "set_random_access_no_values", set_random_access_no_values },
    { "set_replacing_sequential", set_replacing_sequential },
    { "set_replacing_random_access", set_replacing_random_access },
    { "forget_sequential", forget_sequential },
    { "forget_random_access", forget_random_access },
    { "forget_random_access_no_values", forget_random_access_no_values },
    { "forget_nonexistent", forget_nonexistent },
    { "count", count },
    { "pop_first", pop_first },
    { "pop_last", pop_last },
    { "member_sequential", member_sequential },
    { "member_random_access", member_random_access },
    { "sum", sum },
    { "sum_partway", sum_partway },
    { NULL, NULL },
};

static void *
memory_cb(void *p, size_t size, void *udata) {
    /* Do a word-aligned allocation, and save the size immediately
     * before the memory allocated for the caller. */
    uintptr_t *word_aligned = NULL;
    (void)udata;
    if (p != NULL) {
        assert(size == 0);      /* no realloc used */
        word_aligned = p;
        word_aligned--;
        memory_used -= word_aligned[0];
        free(word_aligned);
        return NULL;
    } else {
        memory_used += size;
        if (memory_used > memory_hwm) { memory_hwm = memory_used; }
        word_aligned = malloc(sizeof(*word_aligned) + size);
        if (word_aligned == NULL) { return NULL; }
        word_aligned[0] = size;
        return &word_aligned[1];
    }
}

int
main(int argc, char **argv) {
    handle_args(argc, argv);

    if (limit_count == 0) {
        limits[limit_count] = DEF_LIMIT;
        limit_count++;
    }

    sa_config.node_size = node_size;
    if (track_memory) { sa_config.memory = memory_cb; }

    memcpy(&sa_config_no_values, &sa_config, sizeof(sa_config));
    sa_config_no_values.ignore_values = true;

    if (name != NULL && 0 == strcmp(name, "help")) {
        for (struct benchmark *b = &benchmarks[0]; b->name; b++) {
            printf("  -- %s\n", b->name);
        }
        exit(EXIT_SUCCESS);
    }

    TIME(pre);

    size_t namelen = 0;
    if (name != NULL) { namelen = strlen(name); }

    for (size_t l_i = 0; l_i < limit_count; l_i++) {
        for (size_t c_i = 0; c_i < cycles; c_i++) {
            for (struct benchmark *b = &benchmarks[0]; b->name; b++) {
                memory_used = 0;
                memory_hwm = 0;
                if (name == NULL || 0 == strncmp(name, b->name, namelen)) {
                    b->fun(limits[l_i]);
                }
            }
        }
    }

    TIME(post);

    double usec_total = (double)get_usec_delta(&timer_pre, &timer_post);
    printf("----\n%-30s %.3f sec\n", "total", usec_total / usec_per_sec);
    return 0;
}
