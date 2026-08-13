# CCL

CCL is a C library of reusable containers, data structures, and supporting
utilities. It provides a consistent interface-oriented API for common
collections while remaining suitable for use from both C and C++ programs.

The library includes:

- lists, doubly linked lists, vectors, queues, deques, and priority queues;
- dictionaries, hash tables, binary search trees, red-black trees, and
  scapegoat trees;
- bit strings, Bloom filters, suffix trees, heaps, and masks;
- string and wide-string collections;
- lazy ranges, adaptors, and collection algorithms;
- memory pools, custom allocators, observers, and stream/circular buffers;
- generated, type-specific containers for common numeric and string types.

## Building

CCL requires CMake 3.13 or newer and a C compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

This produces the static `ccl` library. Public headers are in `include/`; the
umbrella header is `include/containers.h`.

To install the library and headers:

```sh
cmake --install build --prefix /path/to/install
```

## Using the library

Include `containers.h`, create a container through its interface object, and
finalize it when it is no longer needed. Containers store values by copying
their bytes, so the element size passed to `Create` must match the values added
to the container.

This example creates a vector of integers:

```c
#include <stdio.h>

#include <containers.h>

int main(void)
{
    Vector *numbers = iVector.Create(sizeof(int), 4);
    int values[] = { 10, 20, 30, 40 };
    size_t index;

    if (numbers == NULL)
        return 1;

    for (index = 0; index < sizeof(values) / sizeof(values[0]); ++index) {
        if (!iVector.Add(numbers, &values[index])) {
            iVector.Finalize(numbers);
            return 1;
        }
    }

    for (index = 0; index < iVector.Size(numbers); ++index) {
        const int *value = iVector.GetElement(numbers, index);
        printf("%d\n", *value);
    }

    iVector.Finalize(numbers);
    return 0;
}
```

### Lazy ranges

Ranges provide read-only, lazy pipelines over sequential containers and
arrays. An adaptor changes only the range recipe; elements are read when a
cursor or terminal algorithm is opened. The source storage must therefore
outlive the range and must not be modified while a cursor is active.

```c
#include <stdio.h>

#include <containers.h>

static int is_even(const void *element, void *arg)
{
    (void)arg;
    return (*(const int *)element % 2) == 0;
}

static int square(const void *input, void *output, void *arg)
{
    int value = *(const int *)input;

    (void)arg;
    *(int *)output = value * value;
    return 1;
}

static int print_int(const void *element, void *arg)
{
    (void)arg;
    printf("%d\n", *(const int *)element);
    return 1;
}

int main(void)
{
    int values[] = { 1, 2, 3, 4, 5, 6 };
    Range *range = NULL;
    int result;

    result = iRange.FromArray(values, 6, sizeof(int), &range);
    if (result > 0)
        result = iRange.Filter(&range, is_even, NULL);
    if (result > 0)
        result = iRange.Transform(&range, sizeof(int), square, NULL);
    if (result > 0)
        result = iRange.Take(&range, 2);
    if (result > 0)
        result = iRange.ForEach(range, print_int, NULL);

    if (range != NULL)
        iRange.Finalize(range);
    return result < 0;
}
```

Unary adaptors accept `Range **` and replace the handle only after successful
construction, so allocation failures leave the existing pipeline valid.
`Concat` similarly consumes its right handle only on success. `Next` returns
`1` for an element, `0` for normal end, or a negative error. Callbacks may use
any positive value for true, success, or continuation; zero means false or a
normal visitor stop, and negative `CONTAINER_ERROR_*` values are propagated as
errors without being reported a second time through the range error handler.
Terminal algorithms open independent cursors and do not consume the range.
Only the outermost range in a pipeline needs to be finalized.
Range handles have unique ownership and should not be copied; after an adaptor
succeeds, any previously retained alias refers to an owned inner recipe node.

### ValArray examples

ValArrays provide type-specific numeric containers. Include `valarray.h` and
use the interface matching the element type, such as `iValArrayInt`,
`iValArrayDouble`, or `iValArrayFloat`. Unlike the generic vector interface,
ValArray operations accept and return numeric values directly.

The following example creates an integer sequence and applies scalar
arithmetic to every element:

```c
#include <stdio.h>

#include <valarray.h>

int main(void)
{
    ValArrayInt *values = iValArrayInt.CreateSequence(4, 1, 1);
    size_t index;

    if (values == NULL)
        return 1;

    /* {1, 2, 3, 4} -> {6, 7, 8, 9} -> {12, 14, 16, 18} */
    if (iValArrayInt.SumScalarTo(values, 5) != 1 ||
        iValArrayInt.MultiplyWithScalar(values, 2) != 1) {
        iValArrayInt.Finalize(values);
        return 1;
    }

    for (index = 0; index < iValArrayInt.Size(values); ++index)
        printf("%d\n", iValArrayInt.GetElement(values, index));

    printf("sum: %d\n", iValArrayInt.Accumulate(values));
    iValArrayInt.Finalize(values);
    return 0;
}
```

A slice is a strided view of an existing ValArray. While a slice is active,
operations such as `Size`, `GetElement`, and scalar arithmetic address only
the selected elements. This example halves elements at indices 1, 3, and 5:

```c
#include <stdio.h>

#include <valarray.h>

int main(void)
{
    double input[] = { 10.0, 20.0, 30.0, 40.0, 50.0, 60.0 };
    ValArrayDouble *values = iValArrayDouble.InitializeWith(6, input);
    size_t index;

    if (values == NULL)
        return 1;

    /* start index 1, three elements, stride 2: 20, 40, 60 */
    if (iValArrayDouble.SetSlice(values, 1, 3, 2) != 1 ||
        iValArrayDouble.MultiplyWithScalar(values, 0.5) != 1 ||
        iValArrayDouble.ResetSlice(values) != 1) {
        iValArrayDouble.Finalize(values);
        return 1;
    }

    for (index = 0; index < iValArrayDouble.Size(values); ++index)
        printf("%.1f%s", iValArrayDouble.GetElement(values, index),
               index + 1 == iValArrayDouble.Size(values) ? "\n" : " ");

    iValArrayDouble.Finalize(values);
    return 0;
}
```

The resulting array is `10 10 30 20 50 30`. ValArrays also support
array-to-array arithmetic, comparisons and masks, sorting, rotation,
selection, minimum/maximum queries, and copying ranges. Arrays returned by
operations such as `Copy`, `GetRange`, `SelectCopy`, and `CreateSequence` are
owned by the caller and must be released with the matching `Finalize` method.

When CCL is included directly in another CMake project, link its `ccl` target:

```cmake
add_subdirectory(path/to/ccl)

add_executable(example main.c)
target_link_libraries(example PRIVATE ccl)
```

For a manually installed copy, add `<prefix>/include/ccl` to the compiler's
include path and link `<prefix>/lib/ccl.lib` on Windows or
`<prefix>/lib/libccl.a` on Unix-like systems. The API follows the same pattern
for other data structures: for example, `iList`, `iQueue`, `iDictionary`, and
`iHashTable` expose the operations for their corresponding container types.

## Testing

Configure a test build, compile it, and run the registered CTest suites:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For GCC or Clang builds, AddressSanitizer and UndefinedBehaviorSanitizer can be
enabled with `-DCCL_ENABLE_SANITIZERS=ON`. GCC coverage instrumentation is
available with `-DCCL_ENABLE_COVERAGE=ON`. These two options are mutually
exclusive.

## Documentation

The full library reference is available in [`doc/ccl.pdf`](doc/ccl.pdf).
Module-level implementation and test notes are collected under `analysis/`.

## License

CCL is distributed under the [MIT License](LICENSE). Some incorporated source
files retain their original copyright and permissive license notices; those
notices remain in the corresponding files.
