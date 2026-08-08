#include "test_support.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"

static BitString *from_text(const char *text)
{
    return iBitString.StringToBitString((unsigned char *)text);
}

static int has_text(BitString *bits, const char *expected)
{
    unsigned char output[256];
    size_t need;
    need = iBitString.Print(bits, sizeof(output), output);
    return need == strlen((const char *)output) + 1 &&
           strcmp((const char *)output, expected) == 0;
}

static int test_lifetime_and_sequence(void)
{
    BitString *bits = NULL;
    BitString *copy = NULL;
    BitString *range = NULL;
    BitString *reverse = NULL;
    size_t i;
    int result = 1;

    bits = iBitString.Create(0);
    TEST_REQUIRE(bits != NULL);
    TEST_REQUIRE(iBitString.Size(bits) == 0);
    for (i = 0; i < 65; ++i)
        TEST_REQUIRE(iBitString.Add(bits, (int)(i & 1)) == 1);
    TEST_REQUIRE(iBitString.Size(bits) == 65);
    TEST_REQUIRE(iBitString.GetElement(bits, 0) == 0);
    TEST_REQUIRE(iBitString.GetElement(bits, 64) == 0);
    TEST_REQUIRE(iBitString.SetCapacity(bits, 65) == 1);
    TEST_REQUIRE(iBitString.SetCapacity(bits, 1) == CONTAINER_ERROR_BADARG);
    copy = iBitString.Copy(bits);
    TEST_REQUIRE(copy != NULL && iBitString.Equal(bits, copy));
    TEST_REQUIRE(iBitString.SetElement(copy, 0, 1) == 1);
    TEST_REQUIRE(!iBitString.Equal(bits, copy));
    range = iBitString.GetRange(bits, 8, 24);
    TEST_REQUIRE(range != NULL && iBitString.Size(range) == 16);
    reverse = iBitString.Reverse(range);
    TEST_REQUIRE(reverse != NULL && iBitString.Size(reverse) == 16);
    TEST_REQUIRE(iBitString.PopulationCount(bits) == 32);
    TEST_REQUIRE(iBitString.BitBlockCount(bits) == 32);
    TEST_REQUIRE(iBitString.IndexOf(bits, 1, NULL, &i) == 1 && i == 1);
    TEST_REQUIRE(iBitString.IndexOf(bits, 0, NULL, &i) == 1 && i == 0);
    TEST_REQUIRE(iBitString.PopBack(bits) == 0);
    TEST_REQUIRE(iBitString.Size(bits) == 64);
    TEST_REQUIRE(iBitString.InsertAt(bits, 1, 1) == 65);
    TEST_REQUIRE(iBitString.EraseAt(bits, 1) == 1);
    TEST_REQUIRE(iBitString.Clear(bits) == 1 && iBitString.Size(bits) == 0);
    result = 0;

cleanup:
    if (reverse != NULL) iBitString.Finalize(reverse);
    if (range != NULL) iBitString.Finalize(range);
    if (copy != NULL) iBitString.Finalize(copy);
    if (bits != NULL) iBitString.Finalize(bits);
    return result;
}

static int test_parse_print_ranges(void)
{
    BitString *bits = NULL;
    BitString *empty = NULL;
    unsigned char bytes[2] = { 0xa5, 0x03 };
    unsigned char copied[2] = { 0, 0 };
    unsigned char output[128];
    int result = 1;

    bits = from_text("1111 1010  0101 1100");
    TEST_REQUIRE(bits != NULL && has_text(bits, "1111 1010  0101 1100"));
    TEST_REQUIRE(iBitString.Print(bits, 0, output) == 21);
    TEST_REQUIRE(iBitString.Print(bits, sizeof(output), NULL) == 21);
    TEST_REQUIRE(iBitString.CopyBits(bits, copied) == 1);
    TEST_REQUIRE(copied[0] == 0x5c && copied[1] == 0xfa);
    empty = iBitString.InitializeWith(sizeof(bytes), bytes);
    TEST_REQUIRE(empty != NULL && iBitString.Size(empty) == 16);
    TEST_REQUIRE(iBitString.CopyBits(empty, copied) == 1);
    TEST_REQUIRE(memcmp(bytes, copied, sizeof(bytes)) == 0);
    {
        BitString *prefixed = iBitString.StringToBitString((unsigned char *)"0b1010");
        TEST_REQUIRE(prefixed != NULL);
        iBitString.Finalize(prefixed);
    }
    TEST_REQUIRE(iBitString.StringToBitString((unsigned char *)"bad") == NULL);
    result = 0;

cleanup:
    if (empty != NULL) iBitString.Finalize(empty);
    if (bits != NULL) iBitString.Finalize(bits);
    return result;
}

static int test_add_range_and_append(void)
{
    BitString *bits = NULL;
    BitString *other = NULL;
    unsigned char source = 0x03;
    size_t i;
    int result = 1;
    const int expected[8] = { 1, 0, 1, 1, 1, 0, 0, 0 };

    bits = from_text("101");
    other = from_text("11");
    TEST_REQUIRE(bits != NULL && other != NULL);
    TEST_REQUIRE(iBitString.AddRange(bits, 5, &source) == 1);
    TEST_REQUIRE(iBitString.Size(bits) == 8);
    for (i = 0; i < 8; ++i)
        TEST_REQUIRE(iBitString.GetElement(bits, i) == expected[i]);
    TEST_REQUIRE(iBitString.Append(bits, other) == 1);
    TEST_REQUIRE(iBitString.Size(bits) == 10);
    TEST_REQUIRE(iBitString.GetElement(bits, 8) == 1);
    TEST_REQUIRE(iBitString.GetElement(bits, 9) == 1);
    TEST_REQUIRE(iBitString.Append(other, other) == 1);
    TEST_REQUIRE(iBitString.Size(other) == 4 && has_text(other, "1111"));
    result = 0;

cleanup:
    if (other != NULL) iBitString.Finalize(other);
    if (bits != NULL) iBitString.Finalize(bits);
    return result;
}

static int test_algebra_and_padding(void)
{
    BitString *a = NULL, *b = NULL, *r = NULL;
    BitString *n = NULL;
    int result = 1;

    a = from_text("1010");
    b = from_text("1100");
    TEST_REQUIRE(a != NULL && b != NULL);
    r = iBitString.Or(a, b);
    TEST_REQUIRE(r != NULL && has_text(r, "1110"));
    iBitString.Finalize(r); r = NULL;
    r = iBitString.And(a, b);
    TEST_REQUIRE(r != NULL && has_text(r, "1000"));
    iBitString.Finalize(r); r = NULL;
    r = iBitString.Nand(a, b);
    TEST_REQUIRE(r != NULL && has_text(r, "0111"));
    iBitString.Finalize(r); r = NULL;
    r = iBitString.Xor(a, b);
    TEST_REQUIRE(r != NULL && has_text(r, "0110"));
    iBitString.Finalize(r); r = NULL;
    n = iBitString.Not(a);
    TEST_REQUIRE(n != NULL && has_text(n, "0101") && iBitString.Size(n) == 4);
    TEST_REQUIRE(iBitString.NotAssign(a) == 1 && has_text(a, "0101"));
    TEST_REQUIRE(iBitString.NandAssign(a, b) == 1 && has_text(a, "1011"));
    TEST_REQUIRE(iBitString.OrAssign(a, b) == 1 && has_text(a, "1111"));
    TEST_REQUIRE(iBitString.AndAssign(a, b) == 1 && has_text(a, "1100"));
    TEST_REQUIRE(iBitString.XorAssign(a, b) == 1 && has_text(a, "0000"));
    iBitString.Finalize(n); n = NULL;
    iBitString.Finalize(a); a = NULL;
    iBitString.Finalize(b); b = NULL;
    a = from_text("000");
    TEST_REQUIRE(a != NULL);
    TEST_REQUIRE(iBitString.NotAssign(a) == 1);
    TEST_REQUIRE(iBitString.PopulationCount(a) == 3);
    TEST_REQUIRE(iBitString.Size(a) == 3);
    result = 0;

cleanup:
    if (n != NULL) iBitString.Finalize(n);
    if (r != NULL) iBitString.Finalize(r);
    if (a != NULL) iBitString.Finalize(a);
    if (b != NULL) iBitString.Finalize(b);
    return result;
}

static int test_search_shift_and_memset(void)
{
    BitString *text = NULL, *pattern = NULL, *empty = NULL;
    unsigned char out[32];
    int result = 1;

    text = from_text("1010010110");
    pattern = from_text("010");
    TEST_REQUIRE(text != NULL && pattern != NULL);
    TEST_REQUIRE(iBitString.Contains(text, pattern, NULL) == 1);
    TEST_REQUIRE(iBitString.Contains(pattern, text, NULL) == 0);
    empty = iBitString.Create(0);
    TEST_REQUIRE(empty != NULL && iBitString.Contains(text, empty, NULL) == 1);
    TEST_REQUIRE(iBitString.BitLeftShift(text, 2) == 1);
    TEST_REQUIRE(has_text(text, "10  0101 1000"));
    TEST_REQUIRE(iBitString.BitRightShift(text, 2) == 1);
    TEST_REQUIRE(has_text(text, "00  1001 0110"));
    TEST_REQUIRE(iBitString.Memset(text, 1, 3, 1) == 1);
    TEST_REQUIRE(iBitString.GetElement(text, 1) == 1);
    TEST_REQUIRE(iBitString.GetElement(text, 3) == 1);
    TEST_REQUIRE(iBitString.Memset(text, 0, 9, 0) == 1);
    TEST_REQUIRE(iBitString.PopulationCount(text) == 0);
    TEST_REQUIRE(iBitString.Print(text, sizeof(out), out) == 14);
    result = 0;

cleanup:
    if (empty != NULL) iBitString.Finalize(empty);
    if (pattern != NULL) iBitString.Finalize(pattern);
    if (text != NULL) iBitString.Finalize(text);
    return result;
}

static size_t allocations;
static size_t frees;
static void *count_malloc(size_t size) { ++allocations; return malloc(size); }
static void count_free(void *ptr) { ++frees; free(ptr); }
static void *count_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
static void *count_calloc(size_t n, size_t size) { ++allocations; return calloc(n, size); }

static int test_allocator_and_readonly(void)
{
    ContainerAllocator allocator = {
        count_malloc, count_free, count_realloc, count_calloc
    };
    BitString *bits = NULL;
    ContainerAllocator *saved;
    size_t before;
    int result = 1;

    allocations = frees = 0;
    saved = iAllocator.GetCurrent();
    bits = iBitString.CreateWithAllocator(1, &allocator);
    TEST_REQUIRE(bits != NULL && iBitString.GetAllocator(bits) == &allocator);
    before = allocations;
    TEST_REQUIRE(iBitString.Add(bits, 1) == 1);
    TEST_REQUIRE(allocations == before);
    TEST_REQUIRE(iBitString.SetFlags(bits, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iBitString.Add(bits, 1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.SetElement(bits, 0, 0) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.SetFlags(bits, 0) == CONTAINER_READONLY);
    iBitString.Finalize(bits); bits = NULL;
    TEST_REQUIRE(frees >= 2);
    iAllocator.Change(saved);
    result = 0;

cleanup:
    if (bits != NULL) iBitString.Finalize(bits);
    iAllocator.Change(saved);
    return result;
}

static int test_iterators_persistence_and_validation(void)
{
    BitString *bits = NULL, *loaded = NULL;
    Iterator *iterator = NULL;
    unsigned char storage[256];
    FILE *stream = NULL;
    int result = 1;
    int *value;

    bits = from_text("101");
    TEST_REQUIRE(bits != NULL);
    iterator = iBitString.NewIterator(bits);
    TEST_REQUIRE(iterator != NULL);
    value = (int *)iterator->GetFirst(iterator);
    TEST_REQUIRE(value != NULL && *value == 1);
    value = (int *)iterator->GetNext(iterator);
    TEST_REQUIRE(value != NULL && *value == 0);
    value = (int *)iterator->GetPrevious(iterator);
    TEST_REQUIRE(value != NULL && *value == 0);
    value = (int *)iterator->GetLast(iterator);
    TEST_REQUIRE(value != NULL && *value == 1);
    TEST_REQUIRE(iterator->Seek(iterator, 1) != NULL);
    value = (int *)iterator->GetCurrent(iterator);
    TEST_REQUIRE(value != NULL && *value == 0);
    iBitString.DeleteIterator(iterator); iterator = NULL;
    TEST_REQUIRE(iBitString.InitIterator(bits, storage) == 1);
    iterator = (Iterator *)storage;
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(iBitString.DeleteIterator(iterator) == 1);
    iterator = NULL;
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iBitString.Save(bits, stream, NULL, NULL) == 0);
    rewind(stream);
    loaded = iBitString.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL && iBitString.Equal(bits, loaded));
    fclose(stream); stream = NULL;
    TEST_REQUIRE(iBitString.IndexOf(bits, 1, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.InsertAt(bits, 99, 1) == 0);
    result = 0;

cleanup:
    if (stream != NULL) fclose(stream);
    if (iterator != NULL) iBitString.DeleteIterator(iterator);
    if (loaded != NULL) iBitString.Finalize(loaded);
    if (bits != NULL) iBitString.Finalize(bits);
    return result;
}

static int test_edge_and_error_paths(void)
{
    BitString *bits = NULL, *other = NULL, *tmp = NULL;
    Iterator *iterator = NULL;
    unsigned char short_output[2];
    unsigned char byte = 0x01;
    int result = 1;
    int replacement = 1;

    TEST_REQUIRE(iBitString.GetCapacity(NULL) == (size_t)CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.GetFlags(NULL) == 0);
    TEST_REQUIRE(iBitString.Sizeof(NULL) == sizeof(BitString));
    TEST_REQUIRE(iBitString.GetAllocator(NULL) == NULL);
    TEST_REQUIRE(iBitString.GetData(NULL) == NULL);
    TEST_REQUIRE(iBitString.GetElementSize(NULL) == 1);
    TEST_REQUIRE(iBitString.Copy(NULL) == NULL);
    TEST_REQUIRE(iBitString.Equal(NULL, NULL) == 0);
    TEST_REQUIRE(iBitString.LessEqual(NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.Or(NULL, NULL) == NULL);
    TEST_REQUIRE(iBitString.Not(NULL) == NULL);
    TEST_REQUIRE(iBitString.Reverse(NULL) == NULL);
    TEST_REQUIRE(iBitString.GetRange(NULL, 0, 0) == NULL);
    TEST_REQUIRE(iBitString.Contains(NULL, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.AddRange(NULL, 1, &byte) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.Append(NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.Save(NULL, NULL, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.Load(NULL, NULL, NULL) == NULL);
    TEST_REQUIRE(iBitString.Init(NULL, 0) == NULL);
    TEST_REQUIRE(iBitString.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iBitString.InitIterator(NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.DeleteIterator(NULL) == CONTAINER_ERROR_BADARG);

    bits = iBitString.Create(0);
    TEST_REQUIRE(bits != NULL);
    TEST_REQUIRE(iBitString.GetElement(bits, 0) == 0);
    TEST_REQUIRE(iBitString.SetElement(bits, 0, 1) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iBitString.GetRange(bits, 1, 1) == NULL);
    TEST_REQUIRE(iBitString.GetRange(bits, 0, 1) == NULL);
    TEST_REQUIRE(iBitString.AddRange(bits, 0, NULL) == 1);
    TEST_REQUIRE(iBitString.AddRange(bits, 1, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.InitializeWith(1, NULL) == NULL);
    TEST_REQUIRE(iBitString.StringToBitString((unsigned char *)"") == NULL);
    tmp = iBitString.StringToBitString((unsigned char *)"0");
    TEST_REQUIRE(tmp != NULL);
    iBitString.Finalize(tmp); tmp = NULL;
    TEST_REQUIRE(iBitString.Print(bits, sizeof(short_output), short_output) == 1);
    TEST_REQUIRE(iBitString.Memset(bits, 0, 0, 1) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iBitString.BitLeftShift(bits, 1) == 1);
    TEST_REQUIRE(iBitString.BitRightShift(bits, 1) == 1);
    TEST_REQUIRE(iBitString.PopBack(bits) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iBitString.EraseAt(bits, 0) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iBitString.InsertAt(bits, 2, 1) == 0);
    TEST_REQUIRE(iBitString.IndexOf(bits, 0, NULL, NULL) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(iBitString.Apply(bits, NULL, NULL) == CONTAINER_ERROR_BADARG);

    TEST_REQUIRE(iBitString.Add(bits, 1) == 1);
    TEST_REQUIRE(iBitString.AddRange(bits, 1, iBitString.GetData(bits)) == 1);
    TEST_REQUIRE(iBitString.ReplaceAt(bits, 0, 0) == 1);
    TEST_REQUIRE(iBitString.ReplaceAt(bits, 0, replacement) == 1);
    TEST_REQUIRE(iBitString.ReplaceAt(bits, 100, 0) == CONTAINER_ERROR_INDEX);
    TEST_REQUIRE(iBitString.Insert(bits, 0) != 0);
    TEST_REQUIRE(iBitString.Erase(bits, 1) == 1);
    TEST_REQUIRE(iBitString.Erase(bits, 1) == 1);
    TEST_REQUIRE(iBitString.Erase(bits, 1) == CONTAINER_ERROR_NOTFOUND);
    TEST_REQUIRE(iBitString.Memset(bits, 0, 100, 0) == 1);
    TEST_REQUIRE(iBitString.SetFlags(bits, CONTAINER_READONLY) == 0);
    TEST_REQUIRE(iBitString.SetCapacity(bits, 8) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.Clear(bits) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.BitLeftShift(bits, 1) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.NotAssign(bits) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.OrAssign(bits, bits) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.AddRange(bits, 1, &byte) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.Append(bits, bits) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.Memset(bits, 0, 0, 0) == CONTAINER_ERROR_READONLY);
    TEST_REQUIRE(iBitString.SetFlags(bits, 0) == CONTAINER_READONLY);
    iBitString.Finalize(bits); bits = NULL;

    other = from_text("1");
    bits = from_text("0000");
    TEST_REQUIRE(bits != NULL && other != NULL);
    TEST_REQUIRE(iBitString.LessEqual(other, bits) == 0);
    TEST_REQUIRE(iBitString.LessEqual(bits, other) == 1);
    TEST_REQUIRE(iBitString.SetElement(bits, 3, 1) == 1);
    TEST_REQUIRE(iBitString.LessEqual(bits, other) == 0);
    TEST_REQUIRE(iBitString.AndAssign(bits, other) == 1);
    TEST_REQUIRE(iBitString.XorAssign(bits, other) == 1);
    TEST_REQUIRE(iBitString.NandAssign(bits, other) == 1);
    TEST_REQUIRE(iBitString.Equal(bits, bits) == 1);
    TEST_REQUIRE(iBitString.Sizeof(bits) > sizeof(BitString));
    iterator = iBitString.NewIterator(bits);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) != NULL);
    TEST_REQUIRE(iterator->GetPosition(iterator) == 0);
    TEST_REQUIRE(iterator->Seek(iterator, 0) != NULL);
    TEST_REQUIRE(iterator->Replace(iterator, &replacement, 0) == 1);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    iBitString.DeleteIterator(iterator); iterator = NULL;
    iBitString.Finalize(other); other = NULL;
    result = 0;

cleanup:
    if (iterator != NULL) iBitString.DeleteIterator(iterator);
    if (tmp != NULL) iBitString.Finalize(tmp);
    if (other != NULL) iBitString.Finalize(other);
    if (bits != NULL) iBitString.Finalize(bits);
    return result;
}

static const TestCase tests[] = {
    { "lifetime and sequence", test_lifetime_and_sequence },
    { "parse, print, and ranges", test_parse_print_ranges },
    { "add range and append", test_add_range_and_append },
    { "algebra and padding", test_algebra_and_padding },
    { "search, shifts, and memset", test_search_shift_and_memset },
    { "allocator and read-only", test_allocator_and_readonly },
    { "iterators and persistence", test_iterators_persistence_and_validation },
    { "edge and error paths", test_edge_and_error_paths }
};

static const TestSuite suite = {
    "bitstrings",
    tests,
    sizeof(tests) / sizeof(tests[0])
};

const TestSuite *ccl_get_test_suite(void)
{
    return &suite;
}
