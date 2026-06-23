/*
 * nostdlib.h - single-header C library: dynamic arrays, string views,
 *              string builders, and hashmaps with pluggable allocators.
 *
 * MIT License
 * Copyright (c) 2026 Alexey Sadovnikov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef NOSTDLIB_H
#define NOSTDLIB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef NOSTD_API
#define NOSTD_API
#endif

#if defined(__SANITIZE_ADDRESS__)
#include <sanitizer/asan_interface.h>
#define NOSTD_POISON(ptr, sz) __asan_poison_memory_region((ptr), (sz))
#define NOSTD_UNPOISON(ptr, sz) __asan_unpoison_memory_region((ptr), (sz))
#elif defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define NOSTD_POISON(ptr, sz) __msan_poison((ptr), (sz))
#define NOSTD_UNPOISON(ptr, sz) __msan_unpoison((ptr), (sz))
#else
#define NOSTD_POISON(ptr, sz) ((void)0)
#define NOSTD_UNPOISON(ptr, sz) ((void)0)
#endif

#ifdef NOSTD_DEBUG
#include <stdio.h>
#define NOSTD_ASSERT(cond)                                                     \
    ((void)((cond) || (fprintf(stderr, "ASSERT: %s:%s:%d:\t%s\n", __FILE__,    \
                               __func__, __LINE__, #cond),                     \
                       abort(), 0)))
#else
#define NOSTD_ASSERT(cond) ((void)0)
#endif

#define NPOS ((size_t)-1)
#define SV_NUM_BUF_MAX 64

#define len(c) ((c).len)
#define cap(c) ((c).cap)
#define data(c) ((c).data)
#define at(c, i) *(NOSTD_ASSERT((size_t)(i) < (c).len), (c).data + (i))

typedef const char *err_t;
extern const err_t MEMORY_ALLOC_ERROR;

typedef int (*cmpf_t)(const void *a, const void *b);
typedef uint64_t (*hashf_t)(const void *data, size_t size);

typedef struct {
    hashf_t hash;
    cmpf_t cmp;
} hmops_t;

typedef struct alc alc_t;
typedef void *(*alcalloc_t)(void *ctx, size_t sz);
typedef void *(*alcrealloc_t)(void *ctx, void *ptr, size_t sz);
typedef void (*alcfree_t)(void *ctx, void *ptr);
/* Custom allocator contract:
 *   alloc(ctx, sz)          — allocate sz bytes; return NULL on failure
 *   realloc(ctx, ptr, sz)   — resize ptr to sz bytes; if ptr is NULL, behave
 *                             like alloc; return NULL on failure (ptr stays
 *                             valid)
 *   free(ctx, ptr)          — free ptr; ptr may be NULL (must be a no-op)
 * Call NOSTD_POISON on free/reset and NOSTD_UNPOISON on alloc for ASan/MSan
 * support in custom allocators (e.g. arenas, pools).
 */
struct alc {
    void *ctx;
    alcalloc_t alloc;
    alcrealloc_t realloc;
    alcfree_t free;
};

NOSTD_API void *alcalloc_impl(alc_t *alc, size_t sz);
NOSTD_API void *alcrealloc_impl(alc_t *alc, void *ptr, size_t sz);
NOSTD_API void alcfree_impl(alc_t *alc, void *ptr);
NOSTD_API alc_t *alclibc(void);

#define alcalloc(alc, sz) alcalloc_impl((alc), (sz))
#define alcrealloc(alc, ptr, sz) alcrealloc_impl((alc), (ptr), (sz))
#define alcfree(alc, ptr) alcfree_impl((alc), (ptr))

#define arr_t(T)                                                               \
    struct {                                                                   \
        alc_t *alc;                                                            \
        size_t cap;                                                            \
        size_t len;                                                            \
        T *data;                                                               \
    }

#define ARR_INIT_CAP 16

NOSTD_API void arrinit_impl(void *arr, size_t elem, size_t cap, alc_t *alc);
#define arrinit(arr, alc)                                                      \
    arrinit_impl((arr), sizeof(*(arr)->data), ARR_INIT_CAP, (alc))
#define arrinitcap(arr, cap, alc)                                              \
    arrinit_impl((arr), sizeof(*(arr)->data), (cap), (alc))

NOSTD_API int arrreserve_impl(void *arr, size_t elem, alc_t *alc, size_t cap,
                              err_t *err);
#define arrreserve(arr, cap, err)                                              \
    arrreserve_impl((arr), sizeof(*(arr)->data), (arr)->alc, (cap), (err))
#define arrroom(arr, count, err)                                               \
    arrreserve_impl((arr), sizeof(*(arr)->data), (arr)->alc,                   \
                    (arr)->len + (count), (err))

#define __VSEL3(_1, _2, _3, NAME, ...) NAME
#define __VSEL4(_1, _2, _3, _4, NAME, ...) NAME
#define __VSEL5(_1, _2, _3, _4, _5, NAME, ...) NAME

#define __arrpush2(a, v)                                                       \
    (NOSTD_ASSERT((a)->len < (a)->cap), (a)->data[(a)->len++] = (v))
#define __arrpush3(a, v, err)                                                  \
    (arrreserve_impl((a), sizeof(*(a)->data), (a)->alc, (a)->len + 1,          \
                     (err)) &&                                                 \
     ((a)->data[(a)->len++] = (v), 1))
#define arrpush(...) __VSEL3(__VA_ARGS__, __arrpush3, __arrpush2)(__VA_ARGS__)

#define arrpop(a) (NOSTD_ASSERT((a)->len > 0), (a)->data[--(a)->len])
#define arrfree(a)                                                             \
    do {                                                                       \
        alcfree((a)->alc, (a)->data);                                          \
        *(a) = (typeof(*(a))){0};                                              \
    } while (0)

#define __arrins3(arr, idx, val)                                               \
    do {                                                                       \
        NOSTD_ASSERT((arr)->len < (arr)->cap);                                 \
        memmove((arr)->data + (idx) + 1, (arr)->data + (idx),                  \
                ((arr)->len - (idx)) * sizeof(*(arr)->data));                  \
        (arr)->data[(idx)] = (val);                                            \
        (arr)->len++;                                                          \
    } while (0)
#define __arrins4(arr, idx, val, err)                                          \
    do {                                                                       \
        if (arrreserve_impl((arr), sizeof(*(arr)->data), (arr)->alc,           \
                            (arr)->len + 1, (err))) {                          \
            memmove((arr)->data + (idx) + 1, (arr)->data + (idx),              \
                    ((arr)->len - (idx)) * sizeof(*(arr)->data));              \
            (arr)->data[(idx)] = (val);                                        \
            (arr)->len++;                                                      \
        }                                                                      \
    } while (0)
#define arrins(...) __VSEL4(__VA_ARGS__, __arrins4, __arrins3)(__VA_ARGS__)

#define __arrinsn4(arr, idx, vals, n)                                          \
    do {                                                                       \
        NOSTD_ASSERT((arr)->len + (n) <= (arr)->cap);                          \
        memmove((arr)->data + (idx) + (n), (arr)->data + (idx),                \
                ((arr)->len - (idx)) * sizeof(*(arr)->data));                  \
        memcpy((arr)->data + (idx), (vals), (n) * sizeof(*(arr)->data));       \
        (arr)->len += (n);                                                     \
    } while (0)
#define __arrinsn5(arr, idx, vals, n, err)                                     \
    do {                                                                       \
        if (arrreserve_impl((arr), sizeof(*(arr)->data), (arr)->alc,           \
                            (arr)->len + (n), (err))) {                        \
            memmove((arr)->data + (idx) + (n), (arr)->data + (idx),            \
                    ((arr)->len - (idx)) * sizeof(*(arr)->data));              \
            memcpy((arr)->data + (idx), (vals), (n) * sizeof(*(arr)->data));   \
            (arr)->len += (n);                                                 \
        }                                                                      \
    } while (0)
#define arrinsn(...) __VSEL5(__VA_ARGS__, __arrinsn5, __arrinsn4)(__VA_ARGS__)

#define arrdel(arr, i)                                                         \
    do {                                                                       \
        memmove((arr)->data + (i), (arr)->data + (i) + 1,                      \
                (--(arr)->len - (i)) * sizeof(*(arr)->data));                  \
    } while (0)

#define arrdelunord(arr, idx)                                                  \
    do {                                                                       \
        (arr)->data[idx] = (arr)->data[--(arr)->len];                          \
    } while (0)

// String View (sv)

typedef struct sv {
    size_t len;
    const char *data;
} sv_t;

typedef int (*svcpred_t)(int c);

extern const err_t sv_PARSE_ERROR;

#define sv(literal) ((sv_t){sizeof(literal) - 1, (literal)})
#define svcstr(cstr) ((sv_t){strlen(cstr), (cstr)})
#define svslice(sv_, start, end) ((sv_t){(end) - (start), (sv_).data + (start)})
#define SVFMT "%.*s"
#define SVARG(sv) (int)(sv).len, (sv).data

NOSTD_API int sveq(sv_t a, sv_t b);
NOSTD_API int svcmp(const void *a, const void *b);
NOSTD_API int svstarts(sv_t sv, sv_t prefix);
NOSTD_API int svends(sv_t sv, sv_t suffix);

NOSTD_API size_t svfind(sv_t sv, sv_t needle);
NOSTD_API size_t svfindr(sv_t sv, sv_t needle);
NOSTD_API size_t svfindc(sv_t sv, char c);
NOSTD_API size_t svfindrc(sv_t sv, char c);
NOSTD_API size_t svfindp(sv_t sv, svcpred_t pred);
NOSTD_API size_t svfindrp(sv_t sv, svcpred_t pred);

NOSTD_API sv_t svtrim(sv_t sv);
NOSTD_API sv_t svtriml(sv_t sv);
NOSTD_API sv_t svtrimr(sv_t sv);
NOSTD_API sv_t svtrimp(sv_t sv, svcpred_t pred);
NOSTD_API sv_t svtrimlp(sv_t sv, svcpred_t pred);
NOSTD_API sv_t svtrimrp(sv_t sv, svcpred_t pred);

NOSTD_API sv_t svtok(sv_t *sv, char delim);
NOSTD_API sv_t svtoksv(sv_t *sv, sv_t delim);
NOSTD_API sv_t svtokp(sv_t *sv, svcpred_t pred);
NOSTD_API sv_t svtokr(sv_t *sv, char delim);
NOSTD_API sv_t svtokrsv(sv_t *sv, sv_t delim);
NOSTD_API sv_t svtokrp(sv_t *sv, svcpred_t pred);

NOSTD_API int sv2i(sv_t sv, err_t *err);
NOSTD_API float sv2f(sv_t sv, err_t *err);
NOSTD_API double sv2d(sv_t sv, err_t *err);
NOSTD_API uint64_t svhash(const void *data, size_t size);

/* String Buffer (sb) */

typedef arr_t(char) sb_t;

extern const err_t sb_FORMAT_ERROR;

NOSTD_API void sbinit(sb_t *sb, size_t cap, alc_t *alc);
NOSTD_API sv_t sbpush(sb_t *sb, sv_t sv, err_t *err);
NOSTD_API sv_t sbpushc(sb_t *sb, char ch, err_t *err);
NOSTD_API sv_t sbpushva(sb_t *sb, err_t *err, const char *fmt, va_list ap);
NOSTD_API sv_t sbpushfmt(sb_t *sb, err_t *err, const char *fmt, ...);
NOSTD_API sv_t sbpushmap(sb_t *sb, sv_t sv, svcpred_t fn, err_t *err);
NOSTD_API void sbmap(sb_t *sb, sv_t sv, svcpred_t fn);
NOSTD_API sv_t sb2sv(sb_t *sb);
NOSTD_API void sbdel(sb_t *sb, sv_t sv);
NOSTD_API void sbclear(sb_t *sb);
NOSTD_API void sbfree(sb_t *sb);

/* --- hashmap -------------------------------------------------------------- */

NOSTD_API uint64_t fnv1ahash(const void *data, size_t size);
NOSTD_API uint64_t hmhashcstr(const void *data, size_t size);
NOSTD_API uint64_t hmhashu64(const void *data, size_t size);
NOSTD_API int hmcmpu64(const void *a, const void *b);
NOSTD_API int hmcmpcstr(const void *a, const void *b);

extern const hmops_t hmopssv;
extern const hmops_t hmopsu64;
extern const hmops_t hmopscstr;

#define HM_TOMB ((size_t)-2)
#define HM_INITCAP 16

typedef struct {
    hmops_t ops;
    alc_t *alc;
    size_t cap;
    size_t len;
    size_t *slots;
} __hmhdr_t;

#define hm1_t(T)                                                               \
    struct {                                                                   \
        hmops_t ops;                                                           \
        alc_t *alc;                                                            \
        size_t cap;                                                            \
        size_t len;                                                            \
        size_t *slots;                                                         \
        T *data;                                                               \
    }

#define hm_t(K, V)                                                             \
    struct {                                                                   \
        hmops_t ops;                                                           \
        alc_t *alc;                                                            \
        size_t cap;                                                            \
        size_t len;                                                            \
        size_t *slots;                                                         \
        struct {                                                               \
            K key;                                                             \
            V val;                                                             \
        } *data;                                                               \
    }

#define hminit(hm, ops_, alc_)                                                 \
    do {                                                                       \
        (hm)->ops = (ops_);                                                    \
        (hm)->alc = (alc_);                                                    \
    } while (0)

typedef struct {
    size_t dsize;
    size_t ksize;
    size_t voffset;
} __hmdlayout_t;
#define __HMDLAYOUT(hm)                                                        \
    ((__hmdlayout_t){sizeof(*(hm)->data), sizeof((hm)->data->key),             \
                     offsetof(typeof(*(hm)->data), val)})

NOSTD_API void *hmput_impl(void *hm, void *key, __hmdlayout_t layout,
                           err_t *err);
NOSTD_API void *hmfind_impl(void *hm, void *key, __hmdlayout_t layout);
NOSTD_API void hmdel_impl(void *hm, void *key, __hmdlayout_t layout);
NOSTD_API int hmreserve_impl(void *hm, __hmdlayout_t layout, size_t n,
                              err_t *err);

#define __TADDROF(T, v) ((T[1]){(v)})

#define hmfind(hm, k_)                                                         \
    (typeof(&(hm)->data->val))hmfind_impl(                                     \
        (hm), __TADDROF(typeof((hm)->data->key), k_), __HMDLAYOUT(hm))

#define hmput(hm, k_, err_)                                                    \
    ((typeof(&(hm)->data->val))hmput_impl(                                     \
        (hm), __TADDROF(typeof((hm)->data->key), k_), __HMDLAYOUT(hm),         \
        (err_)))

#define hmdel(hm, k_)                                                          \
    hmdel_impl((hm), __TADDROF(typeof((hm)->data->key), k_), __HMDLAYOUT(hm))

#define hmreserve(hm, n, err) hmreserve_impl((hm), __HMDLAYOUT(hm), (n), (err))

#define hmforeach(it, hm)                                                      \
    for (typeof((hm)->data)(it) = (hm)->data; it < &(hm)->data[(hm)->len]; it++)

#define hmfree(hm)                                                             \
    do {                                                                       \
        alcfree((hm)->alc, (hm)->data);                                        \
        *(hm) = (typeof(*(hm))){0};                                            \
    } while (0)

#ifdef NOSTDLIB_IMPLEMENTATION
#undef NOSTDLIB_IMPLEMENTATION

#include <stdio.h>

const err_t MEMORY_ALLOC_ERROR = "Could not allocate memory";

typedef struct {
    alc_t *alc;
    size_t cap;
    size_t len;
    void *data;
} arrhdr_t;

static void *gpa_alloc(void *ctx, size_t sz) {
    (void)ctx;
    return malloc(sz);
}
static void *gpa_realloc(void *ctx, void *ptr, size_t sz) {
    (void)ctx;
    return realloc(ptr, sz);
}
static void gpa_free(void *ctx, void *ptr) {
    (void)ctx;
    free(ptr);
}

NOSTD_API alc_t *alclibc(void) {
    static alc_t gpa = {.ctx = NULL,
                        .alloc = gpa_alloc,
                        .realloc = gpa_realloc,
                        .free = gpa_free};
    return &gpa;
}

NOSTD_API void *alcalloc_impl(alc_t *alc, size_t sz) {
    void *ptr = alc->alloc(alc->ctx, sz);
    if (ptr)
        NOSTD_UNPOISON(ptr, sz);
    return ptr;
}

NOSTD_API void *alcrealloc_impl(alc_t *alc, void *ptr, size_t sz) {
    void *newptr = alc->realloc(alc->ctx, ptr, sz);
    if (newptr)
        NOSTD_UNPOISON(newptr, sz);
    return newptr;
}

NOSTD_API void alcfree_impl(alc_t *alc, void *ptr) {
    if (alc)
        alc->free(alc->ctx, ptr);
}

NOSTD_API void arrinit_impl(void *arr, size_t elem, size_t cap, alc_t *alc) {
    arrhdr_t *a = (arrhdr_t *)arr;
    a->alc = alc;
    a->cap = cap;
    a->len = 0;
    a->data = (alc && cap) ? alcalloc_impl(alc, cap * elem) : NULL;
}

NOSTD_API int arrreserve_impl(void *arr, size_t elem, alc_t *alc, size_t cap,
                              err_t *err) {
    if (err)
        *err = NULL;
    arrhdr_t *a = (arrhdr_t *)arr;
    if (a->cap >= cap)
        return 1;
    if (!alc) {
        if (err)
            *err = MEMORY_ALLOC_ERROR;
        return 0;
    }
    size_t newcap = a->cap ? a->cap : ARR_INIT_CAP;
    while (newcap < cap)
        newcap *= 2;
    void *newdata = alcrealloc_impl(alc, a->data, newcap * elem);
    if (!newdata) {
        if (err)
            *err = MEMORY_ALLOC_ERROR;
        return 0;
    }
    a->data = newdata;
    a->cap = newcap;
    return 1;
}

const err_t sv_PARSE_ERROR = "Could not parse string";

NOSTD_API int sveq(sv_t a, sv_t b) {
    return a.len == b.len && memcmp(a.data, b.data, a.len) == 0;
}

NOSTD_API int svcmp(const void *_a, const void *_b) {
    sv_t a = *(sv_t *)_a;
    sv_t b = *(sv_t *)_b;
    size_t minlen = a.len < b.len ? a.len : b.len;
    int cmp = memcmp(a.data, b.data, minlen);
    if (cmp != 0)
        return cmp;
    if (a.len < b.len)
        return -1;
    if (a.len > b.len)
        return 1;
    return 0;
}

NOSTD_API int svstarts(sv_t sv, sv_t prefix) {
    if (prefix.len > sv.len)
        return 0;
    return memcmp(sv.data, prefix.data, prefix.len) == 0;
}

NOSTD_API int svends(sv_t sv, sv_t suffix) {
    if (suffix.len > sv.len)
        return 0;
    return memcmp(sv.data + sv.len - suffix.len, suffix.data, suffix.len) == 0;
}

NOSTD_API size_t svfind(sv_t sv, sv_t needle) {
    if (needle.len == 0)
        return 0;
    if (needle.len > sv.len)
        return NPOS;
    for (size_t i = 0; i <= sv.len - needle.len; i++)
        if (memcmp(sv.data + i, needle.data, needle.len) == 0)
            return i;
    return NPOS;
}

NOSTD_API size_t svfindr(sv_t sv, sv_t needle) {
    if (needle.len == 0)
        return sv.len;
    if (needle.len > sv.len)
        return NPOS;
    for (size_t i = sv.len - needle.len;; i--) {
        if (memcmp(sv.data + i, needle.data, needle.len) == 0)
            return i;
        if (i == 0)
            break;
    }
    return NPOS;
}

NOSTD_API size_t svfindc(sv_t sv, char c) {
    for (size_t i = 0; i < sv.len; i++)
        if (sv.data[i] == c)
            return i;
    return NPOS;
}

NOSTD_API size_t svfindrc(sv_t sv, char c) {
    if (sv.len == 0)
        return NPOS;
    for (size_t i = sv.len - 1;; i--) {
        if (sv.data[i] == c)
            return i;
        if (i == 0)
            break;
    }
    return NPOS;
}

NOSTD_API size_t svfindp(sv_t sv, svcpred_t pred) {
    for (size_t i = 0; i < sv.len; i++)
        if (pred((unsigned char)sv.data[i]))
            return i;
    return NPOS;
}

NOSTD_API size_t svfindrp(sv_t sv, svcpred_t pred) {
    if (sv.len == 0)
        return NPOS;
    for (size_t i = sv.len - 1;; i--) {
        if (pred((unsigned char)sv.data[i]))
            return i;
        if (i == 0)
            break;
    }
    return NPOS;
}

static int sv_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

NOSTD_API sv_t svtrimlp(sv_t sv, svcpred_t pred) {
    while (sv.len > 0 && pred((unsigned char)sv.data[0])) {
        sv.data++;
        sv.len--;
    }
    return sv;
}

NOSTD_API sv_t svtrimrp(sv_t sv, svcpred_t pred) {
    while (sv.len > 0 && pred((unsigned char)sv.data[sv.len - 1]))
        sv.len--;
    return sv;
}

NOSTD_API sv_t svtrimp(sv_t sv, svcpred_t pred) {
    return svtrimlp(svtrimrp(sv, pred), pred);
}

NOSTD_API sv_t svtriml(sv_t sv) { return svtrimlp(sv, sv_isspace); }
NOSTD_API sv_t svtrimr(sv_t sv) { return svtrimrp(sv, sv_isspace); }
NOSTD_API sv_t svtrim(sv_t sv) { return svtrimp(sv, sv_isspace); }

NOSTD_API sv_t svtok(sv_t *sv, char delim) {
    size_t i = svfindc(*sv, delim);
    sv_t tok;
    if (i == NPOS) {
        tok = *sv;
        sv->data += sv->len;
        sv->len = 0;
    } else {
        tok = (sv_t){i, sv->data};
        sv->data += i + 1;
        sv->len -= i + 1;
    }
    return tok;
}

NOSTD_API sv_t svtoksv(sv_t *sv, sv_t delim) {
    size_t i = svfind(*sv, delim);
    sv_t tok;
    if (i == NPOS) {
        tok = *sv;
        sv->data += sv->len;
        sv->len = 0;
    } else {
        tok = (sv_t){i, sv->data};
        sv->data += i + delim.len;
        sv->len -= i + delim.len;
    }
    return tok;
}

NOSTD_API sv_t svtokp(sv_t *sv, svcpred_t pred) {
    size_t i = svfindp(*sv, pred);
    sv_t tok;
    if (i == NPOS) {
        tok = *sv;
        sv->data += sv->len;
        sv->len = 0;
    } else {
        tok = (sv_t){i, sv->data};
        sv->data += i + 1;
        sv->len -= i + 1;
    }
    return tok;
}

NOSTD_API sv_t svtokr(sv_t *sv, char delim) {
    size_t i = svfindrc(*sv, delim);
    sv_t tok;
    if (i == NPOS) {
        tok = *sv;
        sv->len = 0;
    } else {
        tok = (sv_t){sv->len - i - 1, sv->data + i + 1};
        sv->len = i;
    }
    return tok;
}

NOSTD_API sv_t svtokrsv(sv_t *sv, sv_t delim) {
    size_t i = svfindr(*sv, delim);
    sv_t tok;
    if (i == NPOS) {
        tok = *sv;
        sv->len = 0;
    } else {
        tok = (sv_t){sv->len - i - delim.len, sv->data + i + delim.len};
        sv->len = i;
    }
    return tok;
}

NOSTD_API sv_t svtokrp(sv_t *sv, svcpred_t pred) {
    size_t i = svfindrp(*sv, pred);
    sv_t tok;
    if (i == NPOS) {
        tok = *sv;
        sv->len = 0;
    } else {
        tok = (sv_t){sv->len - i - 1, sv->data + i + 1};
        sv->len = i;
    }
    return tok;
}

NOSTD_API int sv2i(sv_t sv, err_t *err) {
    *err = NULL;
    if (sv.len == 0) {
        *err = sv_PARSE_ERROR;
        return 0;
    }
    int sign = 1;
    size_t i = 0;
    if (sv.data[0] == '-') {
        sign = -1;
        i++;
    } else if (sv.data[0] == '+') {
        i++;
    }
    if (i == sv.len) {
        *err = sv_PARSE_ERROR;
        return 0;
    }
    int result = 0;
    for (; i < sv.len; i++) {
        if (sv.data[i] < '0' || sv.data[i] > '9') {
            *err = sv_PARSE_ERROR;
            return 0;
        }
        result = result * 10 + (sv.data[i] - '0');
    }
    return sign * result;
}

NOSTD_API float sv2f(sv_t sv, err_t *err) {
    *err = NULL;
    if (sv.len == 0 || sv.len >= SV_NUM_BUF_MAX) {
        *err = sv_PARSE_ERROR;
        return 0.0f;
    }
    char buf[SV_NUM_BUF_MAX];
    memcpy(buf, sv.data, sv.len);
    buf[sv.len] = '\0';
    char *end;
    float result = strtof(buf, &end);
    if (end != buf + sv.len) {
        *err = sv_PARSE_ERROR;
        return 0.0f;
    }
    return result;
}

NOSTD_API double sv2d(sv_t sv, err_t *err) {
    *err = NULL;
    if (sv.len == 0 || sv.len >= SV_NUM_BUF_MAX) {
        *err = sv_PARSE_ERROR;
        return 0.0;
    }
    char buf[SV_NUM_BUF_MAX];
    memcpy(buf, sv.data, sv.len);
    buf[sv.len] = '\0';
    char *end;
    double result = strtod(buf, &end);
    if (end != buf + sv.len) {
        *err = sv_PARSE_ERROR;
        return 0.0;
    }
    return result;
}

NOSTD_API uint64_t svhash(const void *data, size_t size) {
    (void)size;
    sv_t sv = *(sv_t *)data;
    return fnv1ahash(sv.data, sv.len);
}

/* --- hashmap impl --------------------------------------------------------- */

typedef struct {
    __hmhdr_t hdr;
    void *data;
} __hmvoid_t;

uint64_t fnv1ahash(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < size; i++) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}

NOSTD_API uint64_t hmhashcstr(const void *data, size_t size) {
    (void)size;
    const uint8_t *s = *(const uint8_t **)data;
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= *s++;
        h *= 1099511628211ULL;
    }
    return h;
}

NOSTD_API uint64_t hmhashu64(const void *data, size_t size) {
    uint64_t v = *(uint64_t *)data;
    (void)size;
    v += 0x9e3779b97f4a7c15ULL;
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}

NOSTD_API int hmcmpu64(const void *a, const void *b) {
    uint64_t x = *(uint64_t *)a, y = *(uint64_t *)b;
    return (x > y) - (x < y);
}

NOSTD_API int hmcmpcstr(const void *_a, const void *_b) {
    const char *a = *(const char **)_a;
    const char *b = *(const char **)_b;
    return strcmp(a, b);
}

const hmops_t hmopssv   = { svhash,     svcmp     };
const hmops_t hmopsu64  = { hmhashu64,  hmcmpu64  };
const hmops_t hmopscstr = { hmhashcstr, hmcmpcstr };

static void hm_rebuild(__hmvoid_t *h, __hmdlayout_t dlayout) {
    __hmhdr_t *hdr = &h->hdr;
    void *data = h->data;
    for (size_t i = 0; i < hdr->cap; i++)
        hdr->slots[i] = NPOS;
    for (size_t i = 0; i < hdr->len; i++) {
        void *key = (char *)data + i * dlayout.dsize;
        size_t s = hdr->ops.hash(key, dlayout.ksize) % hdr->cap;
        while (hdr->slots[s] != NPOS)
            s = (s + 1) % hdr->cap;
        hdr->slots[s] = i;
    }
}

static int __hmgrow(__hmvoid_t *h, __hmdlayout_t dlayout) {
    __hmhdr_t *hdr = &h->hdr;
    size_t newcap = hdr->cap ? hdr->cap * 2 : HM_INITCAP;
    size_t slots_off =
        (newcap * dlayout.dsize + sizeof(size_t) - 1) & ~(sizeof(size_t) - 1);
    size_t total = slots_off + newcap * sizeof(size_t);
    void *buf = alcrealloc_impl(hdr->alc, h->data, total);
    if (!buf)
        return 0;
    h->data = buf;
    hdr->slots = (size_t *)((char *)buf + slots_off);
    hdr->cap = newcap;
    hm_rebuild(h, dlayout);
    return 1;
}

typedef struct {
    size_t offset;
    size_t __i;
    size_t __cap;
} __hmprobe_t;

static inline __hmprobe_t __hmprobe(uint64_t hash, size_t cap) {
    return (__hmprobe_t){
        .offset = hash % cap,
        .__i = 0,
        .__cap = cap,
    };
}
static inline int __hmprobenext(__hmprobe_t *it) {
    if (it->__i >= it->__cap)
        return 0;
    if (it->__i > 0)
        it->offset = (it->offset + 1) % it->__cap;
    it->__i++;
    return 1;
}

NOSTD_API void *hmfind_impl(void *hm, void *key, __hmdlayout_t dlayout) {
    __hmvoid_t *h = (__hmvoid_t *)hm;
    __hmhdr_t *hdr = &h->hdr;
    void *data = h->data;
    if (!hdr->cap)
        return NULL;

    uint64_t hash = hdr->ops.hash(key, dlayout.ksize);
    for (__hmprobe_t it = __hmprobe(hash, hdr->cap); __hmprobenext(&it);) {
        size_t idx = hdr->slots[it.offset];
        if (idx == NPOS)
            return NULL;
        if (idx != HM_TOMB) {
            void *ekey = (char *)data + idx * dlayout.dsize;
            if (hdr->ops.cmp(ekey, key) == 0)
                return (char *)ekey + dlayout.voffset;
        }
    }
    return NULL;
}

NOSTD_API void *hmput_impl(void *hm, void *key, __hmdlayout_t dlayout,
                           err_t *err) {
    __hmvoid_t *h = (__hmvoid_t *)hm;
    __hmhdr_t *hdr = &h->hdr;
    void *data = h->data;
    if (err != NULL)
        *err = NULL;

    size_t tombs = 0;
    uint64_t hash = hdr->ops.hash(key, dlayout.ksize);

    size_t tomb = NPOS;
    if (hdr->cap > 0) {
        for (__hmprobe_t it = __hmprobe(hash, hdr->cap); __hmprobenext(&it);) {
            size_t idx = hdr->slots[it.offset];
            if (idx == NPOS)
                break;
            if (idx == HM_TOMB) {
                tombs++;
                if (tomb == NPOS)
                    tomb = it.offset;
            } else {
                void *ekey = (char *)data + idx * dlayout.dsize;
                if (hdr->ops.cmp(ekey, key) == 0)
                    return (char *)ekey + dlayout.voffset;
            }
        }
    }

    if (hdr->cap == 0 || (hdr->len + 1 + tombs) * 4 > hdr->cap * 3) {
        if (!__hmgrow(h, dlayout)) {
            if (err != NULL)
                *err = MEMORY_ALLOC_ERROR;
            return NULL;
        }
        tomb = NPOS;
    }

    size_t ins = tomb;
    if (ins == NPOS) {
        for (__hmprobe_t it = __hmprobe(hash, hdr->cap); __hmprobenext(&it);) {
            ins = it.offset;
            size_t idx = hdr->slots[it.offset];
            if (idx == NPOS || idx == HM_TOMB)
                break;
        }
    }

    size_t idx = hdr->len++;
    void *entry = (char *)h->data + idx * dlayout.dsize;
    memcpy(entry, key, dlayout.ksize);
    memset((char *)entry + dlayout.voffset, 0, dlayout.dsize - dlayout.voffset);
    hdr->slots[ins] = idx;
    return (char *)entry + dlayout.voffset;
}

NOSTD_API void hmdel_impl(void *hm, void *key, __hmdlayout_t dlayout) {
    __hmvoid_t *h = (__hmvoid_t *)hm;
    __hmhdr_t *hdr = &h->hdr;
    void *data = h->data;

    if (!hdr->cap)
        return;

    size_t idx;
    int found = 0;
    uint64_t hash = hdr->ops.hash(key, dlayout.ksize);
    for (__hmprobe_t it = __hmprobe(hash, hdr->cap); __hmprobenext(&it);) {
        idx = hdr->slots[it.offset];
        if (idx == NPOS)
            return;
        if (idx != HM_TOMB) {
            void *ekey = (char *)data + idx * dlayout.dsize;
            if (hdr->ops.cmp(ekey, key) == 0) {
                hdr->slots[it.offset] = HM_TOMB;
                found = 1;
                break;
            }
        }
    }

    if (!found)
        return;

    size_t last = --hdr->len;
    if (idx >= last)
        return;

    void *dst = (char *)data + idx * dlayout.dsize;
    void *src = (char *)data + last * dlayout.dsize;
    memcpy(dst, src, dlayout.dsize);

    uint64_t last_hash = hdr->ops.hash(dst, dlayout.ksize);
    for (__hmprobe_t it = __hmprobe(last_hash, hdr->cap); __hmprobenext(&it);) {
        if (hdr->slots[it.offset] == last) {
            hdr->slots[it.offset] = idx;
            return;
        }
    }
}

NOSTD_API int hmreserve_impl(void *hm, __hmdlayout_t dlayout, size_t n,
                              err_t *err) {
    __hmvoid_t *h = (__hmvoid_t *)hm;
    if (err != NULL)
        *err = NULL;
    while (h->hdr.cap == 0 || h->hdr.cap * 3 < n * 4) {
        if (!__hmgrow(h, dlayout)) {
            if (err != NULL)
                *err = MEMORY_ALLOC_ERROR;
            return 0;
        }
    }
    return 1;
}

/* --- sb impl ------------------------------------------------------------ */

const err_t sb_FORMAT_ERROR = "Could not format string";

NOSTD_API void sbinit(sb_t *sb, size_t cap, alc_t *alc) {
    sb->alc = alc;
    sb->len = 0;
    size_t actual = cap > 0 ? cap : 1;
    sb->data = (char *)(alc ? alcalloc_impl(alc, actual) : NULL);
    sb->cap = sb->data ? actual : 0;
    if (sb->data)
        sb->data[0] = '\0';
}

NOSTD_API sv_t sbpush(sb_t *sb, sv_t sv, err_t *err) {
    if (err)
        *err = NULL;
    if (sv.len == 0)
        return (sv_t){0, sb->data ? sb->data + sb->len : NULL};
    if (!arrreserve_impl(sb, 1, sb->alc, sb->len + sv.len + 1, err))
        return (sv_t){0, NULL};
    char *start = sb->data + sb->len;
    memcpy(start, sv.data, sv.len);
    sb->len += sv.len;
    sb->data[sb->len] = '\0';
    return (sv_t){sv.len, start};
}

NOSTD_API sv_t sbpushc(sb_t *sb, char ch, err_t *err) {
    if (err)
        *err = NULL;
    if (!arrreserve_impl(sb, 1, sb->alc, sb->len + 2, err))
        return (sv_t){0, NULL};
    char *start = sb->data + sb->len;
    *start = ch;
    sb->len++;
    sb->data[sb->len] = '\0';
    return (sv_t){1, start};
}

NOSTD_API sv_t sbpushva(sb_t *sb, err_t *err, const char *fmt, va_list ap) {
    if (err)
        *err = NULL;
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    if (n < 0) {
        va_end(ap2);
        if (err)
            *err = sb_FORMAT_ERROR;
        return (sv_t){0, NULL};
    }
    if (!arrreserve_impl(sb, 1, sb->alc, sb->len + (size_t)n + 1, err)) {
        va_end(ap2);
        return (sv_t){0, NULL};
    }
    char *start = sb->data + sb->len;
    vsnprintf(start, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
    return (sv_t){(size_t)n, start};
}

NOSTD_API sv_t sbpushfmt(sb_t *sb, err_t *err, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    sv_t result = sbpushva(sb, err, fmt, ap);
    va_end(ap);
    return result;
}

NOSTD_API void sbdel(sb_t *sb, sv_t sv) {
    if (sv.len == 0)
        return;
    NOSTD_ASSERT(sb->data != NULL);
    NOSTD_ASSERT((uintptr_t)sv.data >= (uintptr_t)sb->data &&
                 (uintptr_t)sv.data + sv.len <= (uintptr_t)sb->data + sb->len);
    if (!sb->data || (uintptr_t)sv.data < (uintptr_t)sb->data ||
        (uintptr_t)sv.data + sv.len > (uintptr_t)sb->data + sb->len)
        return;
    size_t off = (size_t)(sv.data - sb->data);
    memmove(sb->data + off, sb->data + off + sv.len, sb->len - off - sv.len);
    sb->len -= sv.len;
    sb->data[sb->len] = '\0';
}

NOSTD_API sv_t sbpushmap(sb_t *sb, sv_t sv, svcpred_t fn, err_t *err) {
    if (err)
        *err = NULL;
    if (sv.len == 0)
        return (sv_t){0, sb->data ? sb->data + sb->len : NULL};
    if (!arrreserve_impl(sb, 1, sb->alc, sb->len + sv.len + 1, err))
        return (sv_t){0, NULL};
    char *start = sb->data + sb->len;
    for (size_t i = 0; i < sv.len; i++)
        start[i] = (char)fn((unsigned char)sv.data[i]);
    sb->len += sv.len;
    sb->data[sb->len] = '\0';
    return (sv_t){sv.len, start};
}

NOSTD_API void sbmap(sb_t *sb, sv_t sv, svcpred_t fn) {
    if (sv.len == 0)
        return;
    NOSTD_ASSERT(sb->data != NULL);
    NOSTD_ASSERT((uintptr_t)sv.data >= (uintptr_t)sb->data &&
                 (uintptr_t)sv.data + sv.len <= (uintptr_t)sb->data + sb->len);
    size_t off = (size_t)(sv.data - sb->data);
    for (size_t i = 0; i < sv.len; i++)
        sb->data[off + i] = (char)fn((unsigned char)sb->data[off + i]);
}

NOSTD_API sv_t sb2sv(sb_t *sb) { return (sv_t){sb->len, sb->data}; }

NOSTD_API void sbclear(sb_t *sb) {
    sb->len = 0;
    if (sb->data)
        sb->data[0] = '\0';
}

NOSTD_API void sbfree(sb_t *sb) {
    alcfree_impl(sb->alc, sb->data);
    *sb = (sb_t){0};
}

#endif // NOSTDLIB_IMPLEMENTATION
#endif // NOSTDLIB_H
