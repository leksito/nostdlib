# nostdlib.h

**nostdlib** ("not THE standard library") — a single-header C library providing generic dynamic arrays, string views, string builders, and hashmaps with pluggable allocators.

## Usage

In **one** translation unit, define `NOSTDLIB_IMPLEMENTATION` before including:

```c
#define NOSTDLIB_IMPLEMENTATION
#include "nostdlib.h"
```

In all other files, include normally:

```c
#include "nostdlib.h"
```

## Modules

### Allocator — `alc_t`

Pluggable allocator interface. Use `alclibc()` for the standard malloc/realloc/free allocator.

```c
alc_t *alc = alclibc();
void *ptr = alcalloc(alc, 64);
alcfree(alc, ptr);
```

### Dynamic array — `arr_t(T)`

```c
typedef arr_t(int) ints_t;

ints_t arr = {0};
arrinit(&arr, alclibc());

err_t err = NULL;
arrpush(&arr, 42, &err);
arrpush(&arr, 99, &err);

for (size_t i = 0; i < len(arr); i++)
    printf("%d\n", at(arr, i));

arrfree(&arr);
```

### String view — `sv_t`

Non-owning slice of a string. No allocation, no NUL terminator required.

```c
sv_t s = sv("hello, world");
sv_t word = svtok(&s, ',');          // "hello"
sv_t trimmed = svtrim(word);         // "hello"

printf(SVFMT "\n", SVARG(trimmed));
```

### String builder — `sb_t`

```c
sb_t sb = {0};
sbinit(&sb, 64, alclibc());

err_t err = NULL;
sbpush(&sb, sv("hello"), &err);
sbpushfmt(&sb, &err, " %d", 42);

sv_t result = sb2sv(&sb);
printf(SVFMT "\n", SVARG(result));

sbfree(&sb);
```

### Hashmap — `hm_t(K, V)`

Open-addressing hashmap with pluggable hash and compare via `hmops_t`. Built-in ops: `hmopssv` (sv_t keys), `hmopsu64` (uint64_t keys), `hmopscstr` (C string keys).

```c
typedef hm_t(sv_t, int) wordcount_t;

wordcount_t hm = {0};
hminit(&hm, hmopssv, alclibc());

err_t err = NULL;
*hmput(&hm, sv("apple"), &err) += 1;
*hmput(&hm, sv("apple"), &err) += 1;

int *n = hmfind(&hm, sv("apple"));  // *n == 2

hmforeach(it, &hm)
    printf(SVFMT ": %d\n", SVARG(it->key), it->val);

hmfree(&hm);
```

## Build & test

```sh
make
make valgrind
```

## Example

See [`examples/wordcount.c`](examples/wordcount.c) — a word frequency counter using all four modules together.

```sh
gcc -o wordcount examples/wordcount.c && ./wordcount
```

## License

MIT — see [`nostdlib.h`](nostdlib.h) for the full text.
