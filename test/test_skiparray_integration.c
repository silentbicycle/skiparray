#include "test_skiparray.h"

struct symbol {
    uint8_t len;
    /* Note: this length is only hard-coded to make constructing stack-allocated
     * symbols for comparison easy, for testing purposes. */
    char name[256];
};

static int cmp_symbol(const void *pa, const void *pb, void *unused) {
    (void)unused;

    const struct symbol *a = (const struct symbol *)pa;
    const struct symbol *b = (const struct symbol *)pb;

    if (a->len < b->len) { return -1; }
    if (a->len > b->len) { return 1; }
    return strncmp(a->name, b->name, a->len);    
}

static void
free_symbol(void *key, void *value, void *udata) {
    (void)udata;
    (void)value;
    free(key);
}

static struct symbol *
mksymbol(const char *str) {
    size_t len = strlen(str);
    assert(len < UINT8_MAX);
    struct symbol *res = malloc(sizeof(*res));
    if (res == NULL) { return NULL; }
    memcpy((void *)res->name, str, len);
    res->name[len] = '\0';
    res->len = (uint8_t)len;
    return res;
}

TEST symbol_table(size_t limit) {
    struct skiparray *sa = NULL;
    struct skiparray_config cfg = {
        .cmp = cmp_symbol,
        .free = free_symbol,
    };
    ASSERT_EQ_FMT(SKIPARRAY_NEW_OK, skiparray_new(&cfg, &sa), "%d");

    enum skiparray_set_res sres;
    char buf[64];

    for (size_t i = 0; i < limit; i++) {
        if (sizeof(buf) < (size_t)snprintf(buf, sizeof(buf), "key_%zu", i)) {
            FAILm("snprintf");
        }

        struct symbol *sym = mksymbol(buf);        

        sres = skiparray_set(sa, sym, (void *)1);
        ASSERT_EQ_FMT(SKIPARRAY_SET_BOUND, sres, "%d");
    }

    for (size_t i = 0; i < limit; i++) {
        if (sizeof(buf) < (size_t)snprintf(buf, sizeof(buf), "key_%zu", i)) {
            FAILm("snprintf");
        }

        struct symbol *sym = mksymbol(buf);        

        const bool replace_previous_key = (i & 0x01);

        struct skiparray_pair pair;
        sres = skiparray_set_with_pair(sa, sym, (void *)2,
            replace_previous_key, &pair);
        ASSERT_EQ_FMT(SKIPARRAY_SET_REPLACED, sres, "%d");

        ASSERT_EQ_FMT((size_t)1, (size_t)(uintptr_t)pair.value, "%zu");
        if (replace_previous_key) {
            struct symbol *old_sym = pair.key;
            free(old_sym);
        } else {
            free(sym);
        }
    }

    for (size_t i = 0; i < limit; i++) {
        struct symbol sym;
        sym.len = snprintf(sym.name, sizeof(sym.name), "key_%zu", i);

        struct skiparray_pair p;
        ASSERT(skiparray_get_pair(sa, &sym, &p));
        ASSERT(p.key != NULL);
        const struct symbol *used_symbol = (struct symbol *)p.key;

        ASSERT_EQ_FMT(sym.len, used_symbol->len, "%u");
        GREATEST_ASSERT_STRN_EQ(sym.name, used_symbol->name, sym.len);
        ASSERT_EQ_FMT((size_t)2, (size_t)(uintptr_t)p.value, "%zu");
    }

    skiparray_free(sa);
    PASS();
}

SUITE(integration) {
    RUN_TESTp(symbol_table, 1000);
    RUN_TESTp(symbol_table, 100000);
}
