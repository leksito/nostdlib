# nostdlib.h

**nostdlib** ("not THE standard library") — a single-header C library providing generic dynamic arrays, string views, string builders, and hashmaps with pluggable allocators.

## Requirements

C99 or later with `__typeof__` support. Works with GCC and Clang in any `-std=` mode, and with MSVC in C mode (requires `/Zc:preprocessor`). C23 is also supported.

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

### Build flags

| Flag | Effect | Default |
|------|--------|---------|
| `NOSTD_DEBUG` | printf diagnostics (`<stdio.h>`) + bounds traps | off |
| `NOSTD_BOUNDS_CHECK` | bounds traps on hot paths, no stdio; hardened release | off |
| `NOSTD_API` | linkage/visibility of public functions | empty |

The default build has no checks and no extra includes. Enable `-DNOSTD_BOUNDS_CHECK` for a hardened release build that traps on out-of-bounds access without pulling in stdio.

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

for (size_t i = 0; i < arr.len; i++)
    printf("%d\n", arr.data[i]);

arrfree(&arr);
```

`arrpush(a, v)` without `err` is a fast pre-reserved path — call `arrreserve` or `arrroom` first. `arrpush(a, v, &err)` grows automatically. `nostd_at(arr, i)` provides bounds-checked element access (lvalue) under `NOSTD_DEBUG` or `NOSTD_BOUNDS_CHECK`.

### String view — `sv_t`

Non-owning slice of a string. No allocation, no NUL terminator required.

```c
sv_t s = sv("hello, world");
sv_t word = svtok(&s, ',');     // "hello"
sv_t trimmed = svtrim(word);    // "hello"

printf(SVFMT "\n", SVARG(trimmed));
```

**Lifetime:** `sv_t` does not own its backing memory. When used as a hashmap key, the backing string must outlive the map entry. Watch out for `sb_t` keys — a realloc (triggered by any push) invalidates all views into the builder.

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

for (size_t i = 0; i < hm.len; i++)
    printf(SVFMT ": %d\n", SVARG(hm.data[i].key), hm.data[i].val);

hmfree(&hm);
```

Entries are stored densely in `data[0..len)`, so iteration is a plain index loop.

**Pointer invalidation:** both `hmput` and `hmdel` invalidate all pointers and references into `hm.data` — `hmput` may grow the table, `hmdel` swaps the deleted entry with the last one. Do not hold pointers across these calls.

Safe deletion during iteration (do not increment `i` after a delete, since the last entry moved into position `i`):

```c
for (size_t i = 0; i < hm.len; ) {
    if (want_delete(&hm.data[i].key))
        hmdel(&hm, hm.data[i].key);
    else
        i++;
}
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
