#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#define NOSTDLIB_IMPLEMENTATION
#include "nostdlib.h"

typedef arr_t(int) arr_int_t;

static int tests_run = 0;
static int tests_failed = 0;

#define ASSERT(cond)                                                           \
    do {                                                                       \
        tests_run++;                                                           \
        if (!(cond)) {                                                         \
            tests_failed++;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
        }                                                                      \
    } while (0)

#define TEST(name) static void test_##name(void)
#define RUN(name)                                                              \
    do {                                                                       \
        printf("  " #name "\n");                                               \
        test_##name();                                                         \
    } while (0)

/* --- allocator ------------------------------------------------------------ */

TEST(alclibc) {
    alc_t *gpa = alclibc();
    ASSERT(gpa != NULL);
    ASSERT(gpa->alloc != NULL);
    ASSERT(gpa->realloc != NULL);
    ASSERT(gpa->free != NULL);
    ASSERT(gpa == alclibc());
}

TEST(alc_ops) {
    alc_t *gpa = alclibc();
    void *ptr = alcalloc(gpa, 64);
    ASSERT(ptr != NULL);
    void *ptr2 = alcrealloc(gpa, ptr, 128);
    ASSERT(ptr2 != NULL);
    alcfree(gpa, ptr2);
}

TEST(alcfree_null_alc) { alcfree(NULL, NULL); }

/* --- bump allocator ------------------------------------------------------- */

TEST(alcbump_basic) {
    char buf[256];
    alcbump_t bump = alcbumpinit(buf, sizeof(buf));
    alc_t *alc = alcbump2alc(&bump);

    void *a = alcalloc(alc, 16);
    ASSERT(a != NULL);
    ASSERT(bump.len >= 16);

    void *b = alcalloc(alc, 32);
    ASSERT(b != NULL);
    ASSERT((char *)b >= (char *)a + 16);

    alcbumpreset(&bump);
    ASSERT(bump.len == 0);
    ASSERT(bump.last == NULL);
}

TEST(alcbump_oom) {
    char buf[32];
    alcbump_t bump = alcbumpinit(buf, sizeof(buf));
    alc_t *alc = alcbump2alc(&bump);

    ASSERT(alcalloc(alc, 16) != NULL);
    ASSERT(alcalloc(alc, 16) != NULL);
    ASSERT(alcalloc(alc, 1) == NULL); /* exhausted */
}

TEST(alcbump_realloc_inplace) {
    char buf[256];
    alcbump_t bump = alcbumpinit(buf, sizeof(buf));
    alc_t *alc = alcbump2alc(&bump);

    void *p = alcalloc(alc, 16);
    ASSERT(p != NULL);
    size_t len_after_first = bump.len;

    void *p2 = alcrealloc(alc, p, 32);
    ASSERT(p2 == p); /* in-place: same pointer */
    ASSERT(bump.len > len_after_first);
    ASSERT(bump.len <= len_after_first + 32);
}

TEST(alcbump_realloc_nonlast) {
    char buf[256];
    alcbump_t bump = alcbumpinit(buf, sizeof(buf));
    alc_t *alc = alcbump2alc(&bump);

    void *a = alcalloc(alc, 16);
    memset(a, 0xAB, 16);
    alcalloc(alc, 8); /* push another alloc so `a` is no longer last */

    void *a2 = alcrealloc(alc, a, 32);
    ASSERT(a2 != NULL);
    ASSERT(a2 != a);                       /* had to move */
    ASSERT(((char *)a2)[0] == (char)0xAB); /* data copied */
}

TEST(alcbump_reset_reuse) {
    char buf[64];
    alcbump_t bump = alcbumpinit(buf, sizeof(buf));
    alc_t *alc = alcbump2alc(&bump);

    void *p1 = alcalloc(alc, 32);
    ASSERT(p1 != NULL);
    alcbumpreset(&bump);

    void *p2 = alcalloc(alc, 32);
    ASSERT(p2 == p1); /* same start after reset */
}

TEST(alcbump_with_arr) {
    char buf[1024];
    alcbump_t bump = alcbumpinit(buf, sizeof(buf));
    alc_t *alc = alcbump2alc(&bump);

    typedef arr_t(int) arr_int_t;
    arr_int_t arr = {0};
    arr.alc = alc;

    err_t err = NULL;
    for (int i = 0; i < 20; i++)
        arrpush(&arr, i, &err);
    ASSERT(err == NULL);
    ASSERT(arr.len == 20);
    for (int i = 0; i < 20; i++)
        ASSERT(arr.data[i] == i);

    /* arr memory lives in buf — no free needed, just reset */
    alcbumpreset(&bump);
}

/* --- arrinit -------------------------------------------------------------- */

TEST(arrinit) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);
    ASSERT(arr.alc == gpa);
    ASSERT(arr.cap == ARR_INIT_CAP);
    ASSERT(arr.len == 0);
    ASSERT(arr.data != NULL);
    arrfree(&arr);
}

TEST(arrinitcap) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinitcap(&arr, 32, gpa);
    ASSERT(arr.alc == gpa);
    ASSERT(arr.cap == 32);
    ASSERT(arr.len == 0);
    ASSERT(arr.data != NULL);
    arrfree(&arr);
}

/* --- arrreserve / arrroom --------------------------------------------------
 */

TEST(arrreserve_noop) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);

    err_t err = NULL;
    arrreserve(&arr, 5, &err);
    ASSERT(err == NULL);
    ASSERT(arr.cap == ARR_INIT_CAP);

    arrfree(&arr);
}

TEST(arrreserve_grows) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);

    err_t err = NULL;
    arrreserve(&arr, 100, &err);
    ASSERT(err == NULL);
    ASSERT(arr.cap >= 100);

    arrfree(&arr);
}

TEST(arrroom_dynamic) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arr.alc = gpa;

    err_t err = NULL;
    arrroom(&arr, 5, &err);
    ASSERT(err == NULL);
    ASSERT(arr.cap >= 5);

    arrpush(&arr, 1);
    arrpush(&arr, 2);
    ASSERT(arr.len == 2);

    arrfree(&arr);
}

/* --- arrpush / arrpop ----------------------------------------------------- */

TEST(arrpush_pop) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);

    arrpush(&arr, 10);
    arrpush(&arr, 20);
    arrpush(&arr, 30);
    ASSERT(arr.len == 3);
    ASSERT(arr.data[0] == 10);
    ASSERT(arr.data[1] == 20);
    ASSERT(arr.data[2] == 30);

    ASSERT(arrpop(&arr) == 30);
    ASSERT(arr.len == 2);

    arrfree(&arr);
}

TEST(arrpush_autogrow) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arr.alc = gpa;

    err_t err = NULL;
    for (int i = 0; i < 100; i++)
        arrpush(&arr, i, &err);
    ASSERT(err == NULL);
    ASSERT(arr.len == 100);
    for (int i = 0; i < 100; i++)
        ASSERT(arr.data[i] == i);

    arrfree(&arr);
}

/* --- arrdel --------------------------------------------------------------- */

TEST(arrdel_mid) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);

    arrpush(&arr, 1);
    arrpush(&arr, 2);
    arrpush(&arr, 3);
    arrpush(&arr, 4);

    arrdel(&arr, 1);
    ASSERT(arr.len == 3);
    ASSERT(arr.data[0] == 1);
    ASSERT(arr.data[1] == 3);
    ASSERT(arr.data[2] == 4);

    arrfree(&arr);
}

/* --- arrdelunord ---------------------------------------------------------- */

TEST(arrdelunord_mid) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);

    arrpush(&arr, 1);
    arrpush(&arr, 2);
    arrpush(&arr, 3);
    arrpush(&arr, 4);

    arrdelunord(&arr, 1);
    ASSERT(arr.len == 3);
    ASSERT(arr.data[0] == 1);
    ASSERT(arr.data[1] == 4);
    ASSERT(arr.data[2] == 3);

    arrfree(&arr);
}

/* --- arrins --------------------------------------------------------------- */

TEST(arrins_mid) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arr.alc = gpa;

    err_t err = NULL;
    arrins(&arr, 0, 3, &err);
    ASSERT(err == NULL);
    arrins(&arr, 0, 1, &err);
    ASSERT(err == NULL);
    arrins(&arr, 1, 2, &err);
    ASSERT(err == NULL);

    ASSERT(arr.len == 3);
    ASSERT(arr.data[0] == 1);
    ASSERT(arr.data[1] == 2);
    ASSERT(arr.data[2] == 3);

    arrfree(&arr);
}

/* --- arrinsn -------------------------------------------------------------- */

TEST(arrinsn_mid) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arr.alc = gpa;

    err_t err = NULL;
    int ends[] = {1, 4};
    int vals[] = {2, 3};
    arrinsn(&arr, 0, ends, 2, &err);
    ASSERT(err == NULL);
    arrinsn(&arr, 1, vals, 2, &err);
    ASSERT(err == NULL);

    ASSERT(arr.len == 4);
    ASSERT(arr.data[0] == 1);
    ASSERT(arr.data[1] == 2);
    ASSERT(arr.data[2] == 3);
    ASSERT(arr.data[3] == 4);

    arrfree(&arr);
}

/* --- arrfree -------------------------------------------------------------- */

TEST(arrfree_zeroes) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);
    arrfree(&arr);
    ASSERT(arr.alc == NULL);
    ASSERT(arr.data == NULL);
    ASSERT(arr.cap == 0);
    ASSERT(arr.len == 0);
}

/* --- nostd_at ------------------------------------------------------------- */

TEST(nostd_at_read_write) {
    alc_t *gpa = alclibc();
    arr_int_t arr = {0};
    arrinit(&arr, gpa);
    arrpush(&arr, 10);
    arrpush(&arr, 20);

    ASSERT(nostd_at(arr, 0) == 10);
    ASSERT(nostd_at(arr, 1) == 20);
    nostd_at(arr, 0) = 99;
    ASSERT(arr.data[0] == 99);

    arrfree(&arr);
}

/* --- sv constructors ------------------------------------------------------ */

TEST(sv_literal) {
    sv_t s = sv("hello");
    ASSERT(s.len == 5);
    ASSERT(memcmp(s.data, "hello", 5) == 0);
}

TEST(svcstr) {
    sv_t s = svcstr("world");
    ASSERT(s.len == 5);
    ASSERT(memcmp(s.data, "world", 5) == 0);
}

/* --- sveq / svcmp --------------------------------------------------------- */

TEST(sveq) {
    ASSERT(sveq(sv("hello"), sv("hello")));
    ASSERT(!sveq(sv("hello"), sv("world")));
    ASSERT(!sveq(sv("hello"), sv("hell")));
}

TEST(svcmp) {
    ASSERT(svcmp(&sv("abc"), &sv("abc")) == 0);
    ASSERT(svcmp(&sv("abc"), &sv("abd")) < 0);
    ASSERT(svcmp(&sv("abd"), &sv("abc")) > 0);
    ASSERT(svcmp(&sv("ab"), &sv("abc")) < 0);
    ASSERT(svcmp(&sv("abc"), &sv("ab")) > 0);
}

/* --- svstarts / svends ---------------------------------------------------- */

TEST(svstarts) {
    ASSERT(svstarts(sv("hello world"), sv("hello")));
    ASSERT(!svstarts(sv("hello world"), sv("world")));
    ASSERT(!svstarts(sv("hi"), sv("hello")));
}

TEST(svends) {
    ASSERT(svends(sv("hello world"), sv("world")));
    ASSERT(!svends(sv("hello world"), sv("hello")));
    ASSERT(!svends(sv("hi"), sv("world")));
}

TEST(svslice) {
    sv_t s = sv("hello world");
    ASSERT(sveq(svslice(s, 0, 5), sv("hello")));
    ASSERT(sveq(svslice(s, 6, 11), sv("world")));
    ASSERT(sveq(svslice(s, 2, 4), sv("ll")));
    ASSERT(svslice(s, 3, 3).len == 0);
}

/* --- svfind --------------------------------------------------------------- */

TEST(svfind) {
    ASSERT(svfind(sv("hello world"), sv("world")) == 6);
    ASSERT(svfind(sv("hello world"), sv("hello")) == 0);
    ASSERT(svfind(sv("hello world"), sv("xyz")) == NPOS);
    ASSERT(svfind(sv("abcabc"), sv("bc")) == 1);
}

TEST(svfindr) {
    ASSERT(svfindr(sv("abcabc"), sv("bc")) == 4);
    ASSERT(svfindr(sv("abcabc"), sv("xy")) == NPOS);
}

TEST(svfindc) {
    ASSERT(svfindc(sv("hello"), 'l') == 2);
    ASSERT(svfindc(sv("hello"), 'z') == NPOS);
}

TEST(svfindrc) {
    ASSERT(svfindrc(sv("hello"), 'l') == 3);
    ASSERT(svfindrc(sv("hello"), 'z') == NPOS);
}

TEST(svfindp) {
    ASSERT(svfindp(sv("abc123"), isdigit) == 3);
    ASSERT(svfindp(sv("abcdef"), isdigit) == NPOS);
}

TEST(svfindrp) {
    ASSERT(svfindrp(sv("123abc"), isdigit) == 2);
    ASSERT(svfindrp(sv("abcdef"), isdigit) == NPOS);
}

/* --- svtrim --------------------------------------------------------------- */

TEST(svtrim) {
    ASSERT(sveq(svtrim(sv("  hello  ")), sv("hello")));
    ASSERT(sveq(svtriml(sv("  hello")), sv("hello")));
    ASSERT(sveq(svtrimr(sv("hello  ")), sv("hello")));
}

TEST(svtrimp) {
    ASSERT(sveq(svtrimp(sv("123abc123"), isdigit), sv("abc")));
    ASSERT(sveq(svtrimlp(sv("123abc"), isdigit), sv("abc")));
    ASSERT(sveq(svtrimrp(sv("abc123"), isdigit), sv("abc")));
}

/* --- svtok ---------------------------------------------------------------- */

TEST(svtok) {
    sv_t s = sv("a,b,c");
    ASSERT(sveq(svtok(&s, ','), sv("a")));
    ASSERT(sveq(svtok(&s, ','), sv("b")));
    ASSERT(sveq(svtok(&s, ','), sv("c")));
    ASSERT(s.len == 0);
}

TEST(svtoksv) {
    sv_t s = sv("a::b::c");
    ASSERT(sveq(svtoksv(&s, sv("::")), sv("a")));
    ASSERT(sveq(svtoksv(&s, sv("::")), sv("b")));
    ASSERT(sveq(svtoksv(&s, sv("::")), sv("c")));
    ASSERT(s.len == 0);
}

TEST(svtokp) {
    sv_t s = sv("hello world foo");
    ASSERT(sveq(svtokp(&s, isspace), sv("hello")));
    ASSERT(sveq(svtokp(&s, isspace), sv("world")));
    ASSERT(sveq(svtokp(&s, isspace), sv("foo")));
    ASSERT(s.len == 0);
}

TEST(svtokrp) {
    sv_t s = sv("hello world foo");
    ASSERT(sveq(svtokrp(&s, isspace), sv("foo")));
    ASSERT(sveq(svtokrp(&s, isspace), sv("world")));
    ASSERT(sveq(svtokrp(&s, isspace), sv("hello")));
    ASSERT(s.len == 0);
}

TEST(svtokr) {
    sv_t s = sv("a,b,c");
    ASSERT(sveq(svtokr(&s, ','), sv("c")));
    ASSERT(sveq(svtokr(&s, ','), sv("b")));
    ASSERT(sveq(svtokr(&s, ','), sv("a")));
    ASSERT(s.len == 0);
}

TEST(svtokrsv) {
    sv_t s = sv("a::b::c");
    ASSERT(sveq(svtokrsv(&s, sv("::")), sv("c")));
    ASSERT(sveq(svtokrsv(&s, sv("::")), sv("b")));
    ASSERT(sveq(svtokrsv(&s, sv("::")), sv("a")));
    ASSERT(s.len == 0);
}

/* --- sv2i / sv2f / sv2d --------------------------------------------------- */

TEST(sv2i) {
    err_t err = NULL;
    ASSERT(sv2i(sv("42"), &err) == 42 && err == NULL);
    ASSERT(sv2i(sv("-7"), &err) == -7 && err == NULL);
    ASSERT(sv2i(sv("+3"), &err) == 3 && err == NULL);
    ASSERT(sv2i(sv("0"), &err) == 0 && err == NULL);
    sv2i(sv("abc"), &err);
    ASSERT(err == sv_PARSE_ERROR);
    sv2i(sv(""), &err);
    ASSERT(err == sv_PARSE_ERROR);
    sv2i(sv("-"), &err);
    ASSERT(err == sv_PARSE_ERROR);
    sv2i(sv("+"), &err);
    ASSERT(err == sv_PARSE_ERROR);
    /* INT_MIN must parse correctly; INT_MIN - 1 must fail */
    char int_min_str[32], overflow_str[32];
    snprintf(int_min_str, sizeof(int_min_str), "%d", INT_MIN);
    snprintf(overflow_str, sizeof(overflow_str), "%lld",
             (long long)INT_MIN - 1);
    ASSERT(sv2i(svcstr(int_min_str), &err) == INT_MIN && err == NULL);
    sv2i(svcstr(overflow_str), &err);
    ASSERT(err == sv_PARSE_ERROR);
}

TEST(sv2f) {
    err_t err = NULL;
    ASSERT(sv2f(sv("3.14"), &err) > 3.13f && sv2f(sv("3.14"), &err) < 3.15f);
    ASSERT(err == NULL);
    sv2f(sv("abc"), &err);
    ASSERT(err == sv_PARSE_ERROR);
}

TEST(sv2d) {
    err_t err = NULL;
    ASSERT(sv2d(sv("3.14"), &err) > 3.13 && sv2d(sv("3.14"), &err) < 3.15);
    ASSERT(err == NULL);
    sv2d(sv("abc"), &err);
    ASSERT(err == sv_PARSE_ERROR);
}

/* --- hashmap -------------------------------------------------------------- */

typedef hm_t(sv_t, int) hm_sv_int_t;
typedef hm_t(uint64_t, int) hm_u64_int_t;

TEST(hm_put_find) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());

    *hmput(&hm, sv("apple"), NULL) = 1;
    *hmput(&hm, sv("score"), NULL) = 2;
    *hmput(&hm, sv("total"), NULL) = 3;
    ASSERT(hm.len == 3);

    int *v = hmfind(&hm, sv("apple"));
    ASSERT(v != NULL && *v == 1);
    v = hmfind(&hm, sv("score"));
    ASSERT(v != NULL && *v == 2);
    v = hmfind(&hm, sv("total"));
    ASSERT(v != NULL && *v == 3);
    ASSERT(hmfind(&hm, sv("missing")) == NULL);

    hmfree(&hm);
}

TEST(hm_upsert) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());

    *hmput(&hm, sv("x"), NULL) = 10;
    ASSERT(hm.len == 1);

    *hmput(&hm, sv("x"), NULL) = 20;
    ASSERT(hm.len == 1);
    ASSERT(*hmfind(&hm, sv("x")) == 20);

    hmfree(&hm);
}

TEST(hm_del) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());

    *hmput(&hm, sv("a"), NULL) = 1;
    *hmput(&hm, sv("b"), NULL) = 2;
    *hmput(&hm, sv("c"), NULL) = 3;

    hmdel(&hm, sv("b"));
    ASSERT(hm.len == 2);
    ASSERT(hmfind(&hm, sv("b")) == NULL);
    ASSERT(*hmfind(&hm, sv("a")) == 1);
    ASSERT(*hmfind(&hm, sv("c")) == 3);

    hmdel(&hm, sv("missing"));
    ASSERT(hm.len == 2);

    hmfree(&hm);
}

TEST(hm_del_reinsert) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());

    *hmput(&hm, sv("a"), NULL) = 1;
    *hmput(&hm, sv("b"), NULL) = 2;
    hmdel(&hm, sv("a"));
    ASSERT(hmfind(&hm, sv("a")) == NULL);

    *hmput(&hm, sv("a"), NULL) = 99;
    ASSERT(*hmfind(&hm, sv("a")) == 99);
    ASSERT(hm.len == 2);

    hmfree(&hm);
}

TEST(hm_grow) {
    hm_u64_int_t hm = {0};
    hminit(&hm, hmopsu64, alclibc());

    for (uint64_t k = 0; k < 100; k++)
        *hmput(&hm, k, NULL) = (int)(k * 3);
    ASSERT(hm.len == 100);

    for (uint64_t k = 0; k < 100; k++) {
        int *v = hmfind(&hm, k);
        ASSERT(v != NULL && *v == (int)(k * 3));
    }

    hmfree(&hm);
}

TEST(hm_del_grow) {
    hm_u64_int_t hm = {0};
    hminit(&hm, hmopsu64, alclibc());

    for (uint64_t k = 0; k < 60; k++)
        *hmput(&hm, k, NULL) = (int)k;

    for (uint64_t k = 0; k < 60; k += 2)
        hmdel(&hm, k);
    ASSERT(hm.len == 30);

    for (uint64_t k = 1; k < 60; k += 2) {
        int *v = hmfind(&hm, k);
        ASSERT(v != NULL && *v == (int)k);
    }
    for (uint64_t k = 0; k < 60; k += 2)
        ASSERT(hmfind(&hm, k) == NULL);

    hmfree(&hm);
}

TEST(hm_foreach) {
    hm_u64_int_t hm = {0};
    hminit(&hm, hmopsu64, alclibc());

    *hmput(&hm, (uint64_t){1}, NULL) = 10;
    *hmput(&hm, (uint64_t){2}, NULL) = 20;
    *hmput(&hm, (uint64_t){3}, NULL) = 30;

    int count = 0, sum = 0;
    for (size_t i = 0; i < hm.len; i++) {
        count++;
        sum += hm.data[i].val;
    }
    ASSERT(count == 3);
    ASSERT(sum == 60);

    hmfree(&hm);
}

TEST(hm_free_zeroes) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());
    *hmput(&hm, sv("x"), NULL) = 1;
    hmfree(&hm);
    ASSERT(hm.data == NULL);
    ASSERT(hm.slots == NULL);
    ASSERT(hm.len == 0);
    ASSERT(hm.cap == 0);
}

TEST(hm_len_cap_data) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());
    *hmput(&hm, sv("a"), NULL) = 1;
    *hmput(&hm, sv("b"), NULL) = 2;
    ASSERT(hm.len == 2);
    ASSERT(hm.cap > 0);
    ASSERT(hm.data != NULL);
    hmfree(&hm);
}

TEST(hm_reserve) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());
    err_t err = NULL;
    hmreserve(&hm, 100, &err);
    ASSERT(err == NULL);
    ASSERT(hm.cap * 3 >= 100 * 4);
    size_t cap_before = hm.cap;
    char bufs[100][8];
    for (int i = 0; i < 100; i++) {
        snprintf(bufs[i], sizeof(bufs[i]), "%d", i);
        *hmput(&hm, svcstr(bufs[i]), NULL) = i;
    }
    ASSERT(hm.cap == cap_before);
    ASSERT(hm.len == 100);
    for (int i = 0; i < 100; i++) {
        int *v = hmfind(&hm, svcstr(bufs[i]));
        ASSERT(v != NULL && *v == i);
    }
    hmfree(&hm);
}

TEST(hm_clear) {
    hm_sv_int_t hm = {0};
    hminit(&hm, hmopssv, alclibc());
    *hmput(&hm, sv("a"), NULL) = 1;
    *hmput(&hm, sv("b"), NULL) = 2;
    size_t cap_before = hm.cap;
    hmclear(&hm);
    ASSERT(hm.len == 0);
    ASSERT(hm.cap == cap_before);
    ASSERT(hmfind(&hm, sv("a")) == NULL);
    *hmput(&hm, sv("c"), NULL) = 3;
    ASSERT(hm.len == 1);
    hmfree(&hm);
}

/* --- sb ------------------------------------------------------------------- */

TEST(sbinit) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 64, gpa);
    ASSERT(buf.alc == gpa);
    ASSERT(buf.len == 0);
    ASSERT(buf.cap >= 1);
    ASSERT(buf.data != NULL);
    ASSERT(buf.data[0] == '\0');
    sbfree(&buf);
}

TEST(sbinit_zero_cap) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 0, gpa);
    ASSERT(buf.data != NULL);
    ASSERT(buf.len == 0);
    ASSERT(buf.data[0] == '\0');
    sbfree(&buf);
}

TEST(sbpush) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 16, gpa);

    err_t err = NULL;
    sv_t w = sbpush(&buf, sv("hello"), &err);
    ASSERT(err == NULL);
    ASSERT(w.len == 5);
    ASSERT(memcmp(w.data, "hello", 5) == 0);
    ASSERT(buf.len == 5);
    ASSERT(buf.data[5] == '\0');

    sbpush(&buf, sv(", world"), &err);
    ASSERT(err == NULL);
    ASSERT(buf.len == 12);
    ASSERT(memcmp(buf.data, "hello, world", 12) == 0);
    ASSERT(buf.data[12] == '\0');

    sbfree(&buf);
}

TEST(sbpush_empty) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 16, gpa);

    err_t err = NULL;
    sv_t w = sbpush(&buf, sv(""), &err);
    ASSERT(err == NULL);
    ASSERT(w.len == 0);
    ASSERT(w.data != NULL);
    ASSERT(buf.len == 0);

    sbfree(&buf);
}

TEST(sbpush_autogrow) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 4, gpa);

    err_t err = NULL;
    for (int i = 0; i < 10; i++)
        sbpush(&buf, sv("ab"), &err);
    ASSERT(err == NULL);
    ASSERT(buf.len == 20);
    ASSERT(buf.data[20] == '\0');

    sbfree(&buf);
}

TEST(sbpushc) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 8, gpa);

    err_t err = NULL;
    sv_t w = sbpushc(&buf, 'A', &err);
    ASSERT(err == NULL);
    ASSERT(w.len == 1);
    ASSERT(w.data[0] == 'A');
    ASSERT(buf.len == 1);
    ASSERT(buf.data[1] == '\0');

    sbpushc(&buf, 'B', &err);
    sbpushc(&buf, 'C', &err);
    ASSERT(err == NULL);
    ASSERT(buf.len == 3);
    ASSERT(memcmp(buf.data, "ABC", 3) == 0);
    ASSERT(buf.data[3] == '\0');

    sbfree(&buf);
}

TEST(sbpushfmt) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 16, gpa);

    err_t err = NULL;
    sv_t w = sbpushfmt(&buf, &err, "x=%d", 42);
    ASSERT(err == NULL);
    ASSERT(w.len == 4);
    ASSERT(memcmp(w.data, "x=42", 4) == 0);
    ASSERT(buf.len == 4);
    ASSERT(buf.data[4] == '\0');

    sbpushfmt(&buf, &err, " y=%d", 7);
    ASSERT(err == NULL);
    ASSERT(buf.len == 8);
    ASSERT(memcmp(buf.data, "x=42 y=7", 8) == 0);
    ASSERT(buf.data[8] == '\0');

    sbfree(&buf);
}

TEST(sbpushfmt_returned_view) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("prefix:"), &err);
    sv_t w = sbpushfmt(&buf, &err, "%d", 99);
    ASSERT(err == NULL);
    ASSERT(w.len == 2);
    ASSERT(memcmp(w.data, "99", 2) == 0);
    ASSERT(buf.len == 9);

    sbfree(&buf);
}

TEST(sbdel_start) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("helloworld"), &err);
    sv_t view = sb2sv(&buf);
    sbdel(&buf, (sv_t){5, view.data});

    ASSERT(buf.len == 5);
    ASSERT(memcmp(buf.data, "world", 5) == 0);
    ASSERT(buf.data[5] == '\0');

    sbfree(&buf);
}

TEST(sbdel_middle) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("hello world"), &err);
    sv_t view = sb2sv(&buf);
    size_t pos = svfind(view, sv(" world"));
    ASSERT(pos != NPOS);
    sbdel(&buf, (sv_t){6, view.data + pos});

    ASSERT(buf.len == 5);
    ASSERT(memcmp(buf.data, "hello", 5) == 0);
    ASSERT(buf.data[5] == '\0');

    sbfree(&buf);
}

TEST(sbdel_end) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("helloworld"), &err);
    sv_t view = sb2sv(&buf);
    sbdel(&buf, (sv_t){5, view.data + 5});

    ASSERT(buf.len == 5);
    ASSERT(memcmp(buf.data, "hello", 5) == 0);
    ASSERT(buf.data[5] == '\0');

    sbfree(&buf);
}

TEST(sbdel_noop_empty_sv) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("hello"), &err);
    sv_t view = sb2sv(&buf);
    sbdel(&buf, (sv_t){0, view.data});

    ASSERT(buf.len == 5);
    ASSERT(buf.data[5] == '\0');

    sbfree(&buf);
}

TEST(sb2sv) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("hello"), &err);
    sv_t s = sb2sv(&buf);

    ASSERT(s.len == 5);
    ASSERT(s.data == buf.data);
    ASSERT(sveq(s, sv("hello")));

    sbfree(&buf);
}

TEST(sbclear) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("hello"), &err);
    char *data_ptr = buf.data;
    size_t old_cap = buf.cap;

    sbclear(&buf);
    ASSERT(buf.len == 0);
    ASSERT(buf.data == data_ptr);
    ASSERT(buf.cap == old_cap);
    ASSERT(buf.data[0] == '\0');

    sbfree(&buf);
}

TEST(sbpushmap) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sv_t w = sbpushmap(&buf, sv("Hello"), tolower, &err);
    ASSERT(err == NULL);
    ASSERT(w.len == 5);
    ASSERT(memcmp(w.data, "hello", 5) == 0);
    ASSERT(buf.len == 5);
    ASSERT(buf.data[5] == '\0');

    sbpushmap(&buf, sv("WORLD"), toupper, &err);
    ASSERT(err == NULL);
    ASSERT(buf.len == 10);
    ASSERT(memcmp(buf.data, "helloWORLD", 10) == 0);

    sbfree(&buf);
}

TEST(sbpushmap_empty) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 16, gpa);

    err_t err = NULL;
    sv_t w = sbpushmap(&buf, sv(""), tolower, &err);
    ASSERT(err == NULL);
    ASSERT(w.len == 0);
    ASSERT(w.data != NULL);
    ASSERT(buf.len == 0);

    sbfree(&buf);
}

TEST(sbmap) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("prefix:"), &err);
    sv_t w = sbpush(&buf, sv("Hello"), &err);
    sbpush(&buf, sv(":suffix"), &err);

    sbmap(&buf, w, tolower);

    ASSERT(buf.len == 19);
    ASSERT(memcmp(buf.data, "prefix:hello:suffix", 19) == 0);
    ASSERT(buf.data[19] == '\0');

    sbfree(&buf);
}

TEST(sbmap_full) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 16, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("Hello World"), &err);
    sbmap(&buf, sb2sv(&buf), toupper);

    ASSERT(memcmp(buf.data, "HELLO WORLD", 11) == 0);
    ASSERT(buf.data[11] == '\0');

    sbfree(&buf);
}

TEST(sbfree_zeroes) {
    alc_t *gpa = alclibc();
    sb_t buf = {0};
    sbinit(&buf, 32, gpa);

    err_t err = NULL;
    sbpush(&buf, sv("hello"), &err);
    sbfree(&buf);

    ASSERT(buf.data == NULL);
    ASSERT(buf.len == 0);
    ASSERT(buf.cap == 0);
    ASSERT(buf.alc == NULL);
}

/* --- main ----------------------------------------------------------------- */

int main(void) {
    printf("allocator:\n");
    RUN(alclibc);
    RUN(alc_ops);
    RUN(alcfree_null_alc);

    printf("bump allocator:\n");
    RUN(alcbump_basic);
    RUN(alcbump_oom);
    RUN(alcbump_realloc_inplace);
    RUN(alcbump_realloc_nonlast);
    RUN(alcbump_reset_reuse);
    RUN(alcbump_with_arr);

    printf("arrinit:\n");
    RUN(arrinit);
    RUN(arrinitcap);

    printf("arrreserve/arrroom:\n");
    RUN(arrreserve_noop);
    RUN(arrreserve_grows);
    RUN(arrroom_dynamic);

    printf("arrpush/arrpop:\n");
    RUN(arrpush_pop);
    RUN(arrpush_autogrow);

    printf("arrdel:\n");
    RUN(arrdel_mid);

    printf("arrdelunord:\n");
    RUN(arrdelunord_mid);

    printf("arrins:\n");
    RUN(arrins_mid);

    printf("arrinsn:\n");
    RUN(arrinsn_mid);

    printf("arrfree:\n");
    RUN(arrfree_zeroes);

    printf("nostd_at:\n");
    RUN(nostd_at_read_write);

    printf("sv constructors:\n");
    RUN(sv_literal);
    RUN(svcstr);

    printf("sveq/svcmp:\n");
    RUN(sveq);
    RUN(svcmp);

    printf("svstarts/svends:\n");
    RUN(svstarts);
    RUN(svends);
    RUN(svslice);

    printf("svfind:\n");
    RUN(svfind);
    RUN(svfindr);
    RUN(svfindc);
    RUN(svfindrc);
    RUN(svfindp);
    RUN(svfindrp);

    printf("svtrim:\n");
    RUN(svtrim);
    RUN(svtrimp);

    printf("svtok:\n");
    RUN(svtok);
    RUN(svtoksv);
    RUN(svtokp);
    RUN(svtokr);
    RUN(svtokrsv);
    RUN(svtokrp);

    printf("sv2i/sv2f/sv2d:\n");
    RUN(sv2i);
    RUN(sv2f);
    RUN(sv2d);

    printf("hashmap:\n");
    RUN(hm_put_find);
    RUN(hm_upsert);
    RUN(hm_del);
    RUN(hm_del_reinsert);
    RUN(hm_grow);
    RUN(hm_del_grow);
    RUN(hm_foreach);
    RUN(hm_free_zeroes);
    RUN(hm_len_cap_data);
    RUN(hm_reserve);
    RUN(hm_clear);

    printf("sb:\n");
    RUN(sbinit);
    RUN(sbinit_zero_cap);
    RUN(sbpush);
    RUN(sbpush_empty);
    RUN(sbpush_autogrow);
    RUN(sbpushc);
    RUN(sbpushfmt);
    RUN(sbpushfmt_returned_view);
    RUN(sbdel_start);
    RUN(sbdel_middle);
    RUN(sbdel_end);
    RUN(sbdel_noop_empty_sv);
    RUN(sb2sv);
    RUN(sbclear);
    RUN(sbpushmap);
    RUN(sbpushmap_empty);
    RUN(sbmap);
    RUN(sbmap_full);
    RUN(sbfree_zeroes);

    printf("\n%d/%d passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
