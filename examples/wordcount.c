#include <assert.h>
#include <ctype.h>
#include <stdio.h>

#define NOSTDLIB_IMPLEMENTATION
#include "../nostdlib.h"

typedef struct {
    sv_t key;
    int  val;
} kv_t;
typedef hm1_t(kv_t) hmwords_t;
typedef arr_t(kv_t) arrwords_t;

static int cmpcount(const void *a, const void *b) {
    return ((const kv_t *)b)->val - ((const kv_t *)a)->val;
}

static int isnotalpha(int c) { return !isalpha(c); }

static const char *input =
    "Banana, apple!\t orange Apple... banana ORANGE pear banAna";

int main(void) {
    sv_t text = svcstr(input);

    hmwords_t counts = {0};
    hminit(&counts, hmopssv, alclibc());

    sb_t buf = {0};
    sbinit(&buf, text.len, alclibc());

    err_t err = NULL;

    while (text.len) {
        sv_t word = svtokp(&text, isnotalpha);
        if (word.len == 0)
            continue;
        sv_t lower = sbpushmap(&buf, word, tolower, &err);
        assert(err == NULL);
        *hmput(&counts, lower, &err) += 1;
        assert(err == NULL);
    }

    arrwords_t sorted = {0};
    arrinitcap(&sorted, counts.len, alclibc());
    arrinsn(&sorted, 0, counts.data, counts.len, &err);
    assert(err == NULL);
    qsort(sorted.data, sorted.len, sizeof(sorted.data[0]), cmpcount);

    for (size_t i = 0; i < sorted.len; i++) {
        kv_t kv = sorted.data[i];
        printf(SVFMT ": %d\n", SVARG(kv.key), kv.val);
    }

    arrfree(&sorted);
    sbfree(&buf);
    hmfree(&counts);

    return 0;
}
