#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

typedef struct {
    unsigned char bytes[32];
} LargeValue;

typedef struct __attribute__((aligned(32))) {
    unsigned char bytes[32];
} OverAlignedValue;

static int destructor_calls;
static const void *last_destructor_value;

static int failing_save(const void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int failing_load(void *element, void *arg, FILE *stream)
{
    (void)element;
    (void)arg;
    (void)stream;
    return 0;
}

static int count_destructor(void *value)
{
    destructor_calls++;
    last_destructor_value = value;
    return 1;
}

static int test_basic_binary_and_large_values(void)
{
    HashTable *table = NULL;
    HashTable *aligned_table = NULL;
    LargeValue values[40];
    char keys[40][8];
    unsigned char binary_key[] = {'a', '\0', 'b', 'c'};
    LargeValue binary_value;
    LargeValue *stored;
    size_t i;
    int result = -1;

    table = iHashTable.Create(sizeof(LargeValue));
    TEST_REQUIRE(table != NULL);
    TEST_REQUIRE(iHashTable.Size(table) == 0);
    TEST_REQUIRE(iHashTable.GetElement(table, "missing", 7) == NULL);
    TEST_REQUIRE(iHashTable.Contains(table, "missing", 7) == 0);

    for (i = 0; i < 40; ++i) {
        unsigned before_timestamp = table->timestamp;
        memset(&values[i], (int)i, sizeof(values[i]));
        snprintf(keys[i], sizeof(keys[i]), "k%03lu", (unsigned long)i);
        TEST_REQUIRE(iHashTable.Add(table, keys[i], strlen(keys[i]), &values[i]) == 1);
        TEST_REQUIRE(table->timestamp == before_timestamp + 1);
    }
    TEST_REQUIRE(iHashTable.Size(table) == 40);
    for (i = 0; i < 40; ++i) {
        stored = iHashTable.GetElement(table, keys[i], strlen(keys[i]));
        TEST_REQUIRE(stored != NULL);
        TEST_REQUIRE(memcmp(stored, &values[i], sizeof(*stored)) == 0);
    }

    memset(&binary_value, 0xa5, sizeof(binary_value));
    TEST_REQUIRE(iHashTable.Add(table, binary_key, sizeof(binary_key), &binary_value) == 1);
    TEST_REQUIRE(iHashTable.Contains(table, binary_key, sizeof(binary_key)) == 1);
    TEST_REQUIRE(iHashTable.Add(table, binary_key, sizeof(binary_key), &binary_value) == 0);
    binary_value.bytes[0] = 0x11;
    TEST_REQUIRE(iHashTable.Replace(table, binary_key, sizeof(binary_key), &binary_value) == 1);
    TEST_REQUIRE(((LargeValue *)iHashTable.GetElement(table, binary_key,
                                                       sizeof(binary_key)))->bytes[0] == 0x11);
    TEST_REQUIRE(iHashTable.Erase(table, "not-there", 9) == 0);
    TEST_REQUIRE(iHashTable.Erase(table, binary_key, sizeof(binary_key)) == 1);
    TEST_REQUIRE(iHashTable.Add(table, binary_key, sizeof(binary_key), &binary_value) == 1);
    TEST_REQUIRE(iHashTable.Erase(table, binary_key, sizeof(binary_key)) == 1);
    TEST_REQUIRE(iHashTable.Contains(table, binary_key, sizeof(binary_key)) == 0);

    aligned_table = iHashTable.Create(sizeof(OverAlignedValue));
    TEST_REQUIRE(aligned_table != NULL);
    {
        OverAlignedValue aligned_value;
        OverAlignedValue *aligned_stored;
        memset(&aligned_value, 0x5a, sizeof(aligned_value));
        TEST_REQUIRE(iHashTable.Add(aligned_table, "aligned", 7,
                                     &aligned_value) == 1);
        aligned_stored = iHashTable.GetElement(aligned_table, "aligned", 7);
        TEST_REQUIRE(aligned_stored != NULL &&
                     ((uintptr_t)aligned_stored % _Alignof(OverAlignedValue)) == 0);
    }
    result = 0;

cleanup:
    if (aligned_table != NULL)
        iHashTable.Finalize(aligned_table);
    if (table != NULL)
        iHashTable.Finalize(table);
    return result;
}

static int test_resize_and_iterators(void)
{
    HashTable *table = NULL;
    Iterator *iterator = NULL;
    void *buffer = NULL;
    char keys[4][8] = {{'a'}, {'b'}, {'c'}, {'d'}};
    int values[4] = {1, 2, 3, 4};
    int replacement = 99;
    size_t seen = 0;
    int result = -1;

    table = iHashTable.Create(sizeof(int));
    TEST_REQUIRE(table != NULL);
    TEST_REQUIRE(iHashTable.Resize(table, 31) == 1);
    TEST_REQUIRE(iHashTable.Resize(table, 64) == 1);
    TEST_REQUIRE(iHashTable.Resize(table, 0) == 1);
    TEST_REQUIRE(iHashTable.Size(table) == 0);
    for (seen = 0; seen < 4; ++seen)
        TEST_REQUIRE(iHashTable.Add(table, keys[seen], 1, &values[seen]) == 1);

    iterator = iHashTable.NewIterator(table);
    TEST_REQUIRE(iterator != NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    do {
        ++seen;
    } while (iterator->GetNext(iterator) != NULL);
    TEST_REQUIRE(seen >= 4);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    TEST_REQUIRE(iHashTable.DeleteIterator(iterator) == 1);
    iterator = NULL;

    buffer = malloc(iHashTable.SizeofIterator(table));
    TEST_REQUIRE(buffer != NULL);
    TEST_REQUIRE(iHashTable.InitIterator(table, buffer) == 1);
    iterator = buffer;
    TEST_REQUIRE(iterator->GetFirst(iterator) != NULL);
    TEST_REQUIRE(iterator->Replace(iterator, &replacement, 0) == 1);
    TEST_REQUIRE(*(int *)iHashTable.GetElement(table, keys[0], 1) == 99 ||
                 *(int *)iHashTable.GetElement(table, keys[1], 1) == 99 ||
                 *(int *)iHashTable.GetElement(table, keys[2], 1) == 99 ||
                 *(int *)iHashTable.GetElement(table, keys[3], 1) == 99);
    TEST_REQUIRE(iHashTable.Add(table, "new", 3, &replacement) == 1);
    TEST_REQUIRE(iterator->GetNext(iterator) == NULL);
    TEST_REQUIRE(iterator->GetFirst(iterator) == NULL);
    TEST_REQUIRE(iterator->GetCurrent(iterator) == NULL);
    result = 0;

cleanup:
    if (buffer != NULL)
        free(buffer);
    if (iterator != NULL && iterator != buffer)
        iHashTable.DeleteIterator(iterator);
    if (table != NULL)
        iHashTable.Finalize(table);
    return result;
}

static int search_counter(void *rec, const void *key, size_t klen, const void *value)
{
    size_t *count = rec;
    (void)key;
    (void)klen;
    (void)value;
    (*count)++;
    return 1;
}

static int stop_search(void *rec, const void *key, size_t klen, const void *value)
{
    size_t *count = rec;
    (void)key;
    (void)klen;
    (void)value;
    (*count)++;
    return 0;
}

static int apply_counter(void *key, size_t klen, void *value, void *arg)
{
    size_t *count = arg;
    (void)key;
    (void)klen;
    (void)value;
    (*count)++;
    return 1;
}

static int stop_apply(void *key, size_t klen, void *value, void *arg)
{
    size_t *count = arg;
    (void)key;
    (void)klen;
    (void)value;
    (*count)++;
    return 0;
}

static unsigned int fixed_hash(const char *key, size_t *length)
{
    (void)key;
    (void)length;
    return 0;
}

static int test_argument_validation_and_readonly(void)
{
    HashTable *table = NULL;
    FILE *stream = NULL;
    int value = 7;
    GeneralHashFunction old_hash;
    int result = -1;

    TEST_REQUIRE(iHashTable.Create(SIZE_MAX) == NULL);
    TEST_REQUIRE(iHashTable.Init(NULL, sizeof(value)) == NULL);
    TEST_REQUIRE(iHashTable.Size(NULL) == 0);
    TEST_REQUIRE(iHashTable.GetFlags(NULL) == 0);
    TEST_REQUIRE(iHashTable.SetFlags(NULL, 1) == 0);
    TEST_REQUIRE(iHashTable.GetElementSize(NULL) == 0);
    TEST_REQUIRE(iHashTable.Sizeof(NULL) == sizeof(HashTable));
    TEST_REQUIRE(iHashTable.SetHashFunction(NULL, fixed_hash) == NULL);
    TEST_REQUIRE(iHashTable.Copy(NULL, NULL) == NULL);
    TEST_REQUIRE(iHashTable.Merge(NULL, NULL, NULL, NULL, NULL) == NULL);
    TEST_REQUIRE(iHashTable.Search(NULL, NULL, NULL) < 0);
    TEST_REQUIRE(iHashTable.Apply(NULL, NULL, NULL) < 0);
    TEST_REQUIRE(iHashTable.Erase(NULL, "x", 1) < 0);
    TEST_REQUIRE(iHashTable.Clear(NULL) < 0);
    TEST_REQUIRE(iHashTable.Finalize(NULL) < 0);
    TEST_REQUIRE(iHashTable.Save(NULL, NULL, NULL, NULL) < 0);
    TEST_REQUIRE(iHashTable.Load(NULL, NULL, NULL) == NULL);
    TEST_REQUIRE(iHashTable.NewIterator(NULL) == NULL);
    TEST_REQUIRE(iHashTable.InitIterator(NULL, NULL) < 0);
    TEST_REQUIRE(iHashTable.DeleteIterator(NULL) < 0);
    TEST_REQUIRE(iHashTable.SetErrorFunction(NULL, NULL) != NULL);
    TEST_REQUIRE(iHashTable.SetDestructor(NULL, NULL) == NULL);

    table = iHashTable.Create(sizeof(value));
    TEST_REQUIRE(table != NULL);
    {
        Iterator *empty_iterator = iHashTable.NewIterator(table);
        size_t empty_count = 0;
        TEST_REQUIRE(empty_iterator != NULL);
        TEST_REQUIRE(empty_iterator->GetFirst(empty_iterator) == NULL);
        TEST_REQUIRE(empty_iterator->GetNext(empty_iterator) == NULL);
        TEST_REQUIRE(empty_iterator->GetCurrent(empty_iterator) == NULL);
        TEST_REQUIRE(iHashTable.DeleteIterator(empty_iterator) == 1);
        TEST_REQUIRE(iHashTable.Search(table, search_counter, &empty_count) == 1);
        TEST_REQUIRE(iHashTable.Apply(table, apply_counter, &empty_count) == 1);
    }
    TEST_REQUIRE(iHashTable.Add(table, NULL, 1, &value) < 0);
    TEST_REQUIRE(iHashTable.Add(table, "x", 1, NULL) < 0);
    TEST_REQUIRE(iHashTable.GetElement(table, NULL, 1) == NULL);
    TEST_REQUIRE(iHashTable.GetElement(table, "x", 0) == NULL);
    TEST_REQUIRE(iHashTable.Contains(table, NULL, 1) < 0);
    TEST_REQUIRE(iHashTable.Contains(table, "x", 0) < 0);
    TEST_REQUIRE(iHashTable.Replace(table, NULL, 1, &value) < 0);
    TEST_REQUIRE(iHashTable.Replace(table, "x", 0, &value) < 0);
    TEST_REQUIRE(iHashTable.Replace(table, "x", 1, NULL) < 0);
    TEST_REQUIRE(iHashTable.Replace(table, "x", 1, &value) == 0);
    old_hash = iHashTable.SetHashFunction(table, fixed_hash);
    TEST_REQUIRE(old_hash != NULL);
    TEST_REQUIRE(iHashTable.Add(table, "x", 1, &value) == 1);
    TEST_REQUIRE(iHashTable.Add(table, "z", 1, &value) == 1);
    TEST_REQUIRE(iHashTable.Contains(table, "z", 1) == 1);
    TEST_REQUIRE(iHashTable.SetHashFunction(table, old_hash) == fixed_hash);
    TEST_REQUIRE(iHashTable.SetHashFunction(table, NULL) == old_hash);
    TEST_REQUIRE(iHashTable.SetErrorFunction(table, NULL) != NULL);
    TEST_REQUIRE(iHashTable.SetDestructor(table, NULL) == NULL);
    TEST_REQUIRE(iHashTable.Resize(table, 31) == 1);
    TEST_REQUIRE(iHashTable.Resize(table, 31) == 1);
    TEST_REQUIRE(iHashTable.Resize(table, SIZE_MAX) < 0);
    TEST_REQUIRE(iHashTable.Add(table, "sentinel", (size_t)-1, &value) == 1);
    TEST_REQUIRE(iHashTable.Contains(table, "sentinel", 8) == 1);
    TEST_REQUIRE(iHashTable.Erase(table, "sentinel", 0) < 0);

    iHashTable.SetFlags(table, CONTAINER_READONLY);
    TEST_REQUIRE(iHashTable.Add(table, "y", 1, &value) < 0);
    TEST_REQUIRE(iHashTable.Replace(table, "x", 1, &value) < 0);
    TEST_REQUIRE(iHashTable.Erase(table, "x", 1) < 0);
    TEST_REQUIRE(iHashTable.Clear(table) < 0);
    TEST_REQUIRE(iHashTable.Resize(table, 63) < 0);
    iHashTable.SetFlags(table, 0);

    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iHashTable.Load(stream, NULL, NULL) == NULL);
    fclose(stream);
    stream = NULL;
    result = 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (table != NULL)
        iHashTable.Finalize(table);
    return result;
}

static void *sum_merger(Pool *pool, const void *key, size_t klen,
                        const void *overlay, const void *base,
                        const void *data)
{
    static int value;
    (void)pool;
    (void)key;
    (void)klen;
    (void)data;
    value = *(const int *)overlay + *(const int *)base;
    return &value;
}

static void *null_merger(Pool *pool, const void *key, size_t klen,
                         const void *overlay, const void *base,
                         const void *data)
{
    (void)pool;
    (void)key;
    (void)klen;
    (void)overlay;
    (void)base;
    (void)data;
    return NULL;
}

static int test_copy_merge_callbacks_and_destructor(void)
{
    HashTable *base = NULL, *overlay = NULL, *copy = NULL, *merged = NULL;
    HashTable *empty_left = NULL, *empty_right = NULL, *empty_merged = NULL;
    HashTable *overlay_result = NULL;
    HashTable *bulk = NULL, *bulk_merged = NULL;
    HashTable *incompatible = NULL;
    Pool *copy_pool = NULL, *merge_pool = NULL, *empty_pool = NULL, *overlay_pool = NULL, *bulk_pool = NULL, *incompat_pool = NULL;
    int one = 1, two = 2, three = 3;
    size_t searched = 0, applied = 0;
    size_t i;
    char bulk_keys[20][16];
    int result = -1;

    base = iHashTable.Create(sizeof(int));
    overlay = iHashTable.Create(sizeof(int));
    TEST_REQUIRE(base != NULL && overlay != NULL);
    TEST_REQUIRE(iHashTable.Add(base, "same", 4, &one) == 1);
    TEST_REQUIRE(iHashTable.Add(base, "base", 4, &two) == 1);
    TEST_REQUIRE(iHashTable.Add(overlay, "same", 4, &two) == 1);
    TEST_REQUIRE(iHashTable.Add(overlay, "extra", 5, &three) == 1);
    TEST_REQUIRE(iHashTable.Search(base, search_counter, &searched) == 1);
    TEST_REQUIRE(iHashTable.Apply(base, apply_counter, &applied) == 1);
    TEST_REQUIRE(searched == 2 && applied == 2);
    searched = applied = 0;
    TEST_REQUIRE(iHashTable.Search(base, stop_search, &searched) == 0);
    TEST_REQUIRE(iHashTable.Apply(base, stop_apply, &applied) == 0);
    TEST_REQUIRE(searched == 1 && applied == 1);

    copy_pool = iPool.Create(NULL);
    TEST_REQUIRE(copy_pool != NULL);
    copy = iHashTable.Copy(base, copy_pool);
    TEST_REQUIRE(copy != NULL && copy->VTable == &iHashTable);
    TEST_REQUIRE(*(int *)iHashTable.GetElement(copy, "base", 4) == 2);

    merge_pool = iPool.Create(NULL);
    TEST_REQUIRE(merge_pool != NULL);
    merged = iHashTable.Merge(merge_pool, overlay, base, sum_merger, NULL);
    TEST_REQUIRE(merged != NULL && iHashTable.Size(merged) == 3);
    TEST_REQUIRE(*(int *)iHashTable.GetElement(merged, "same", 4) == 3);
    TEST_REQUIRE(*(int *)iHashTable.GetElement(merged, "extra", 5) == 3);
    incompatible = iHashTable.Create(sizeof(LargeValue));
    incompat_pool = iPool.Create(NULL);
    TEST_REQUIRE(incompatible != NULL && incompat_pool != NULL);
    TEST_REQUIRE(iHashTable.Merge(incompat_pool, incompatible, base, NULL, NULL) == NULL);
    TEST_REQUIRE(iHashTable.Merge(incompat_pool, overlay, base, null_merger, NULL) == NULL);

    empty_left = iHashTable.Create(sizeof(int));
    empty_right = iHashTable.Create(sizeof(int));
    empty_pool = iPool.Create(NULL);
    TEST_REQUIRE(empty_left != NULL && empty_right != NULL && empty_pool != NULL);
    empty_merged = iHashTable.Merge(empty_pool, empty_left, empty_right, NULL, NULL);
    TEST_REQUIRE(empty_merged != NULL && iHashTable.Size(empty_merged) == 0);

    overlay_pool = iPool.Create(NULL);
    TEST_REQUIRE(overlay_pool != NULL);
    overlay_result = iHashTable.Overlay(overlay_pool, overlay, base);
    TEST_REQUIRE(overlay_result != NULL &&
                 *(int *)iHashTable.GetElement(overlay_result, "same", 4) == 2);

    bulk = iHashTable.Create(sizeof(int));
    TEST_REQUIRE(bulk != NULL);
    for (i = 0; i < 20; ++i) {
        snprintf(bulk_keys[i], sizeof(bulk_keys[i]), "bulk-%lu", (unsigned long)i);
        TEST_REQUIRE(iHashTable.Add(bulk, bulk_keys[i], strlen(bulk_keys[i]), &three) == 1);
    }
    bulk_pool = iPool.Create(NULL);
    TEST_REQUIRE(bulk_pool != NULL);
    bulk_merged = iHashTable.Merge(bulk_pool, bulk, empty_left, NULL, NULL);
    TEST_REQUIRE(bulk_merged != NULL && iHashTable.Size(bulk_merged) == 20);

    destructor_calls = 0;
    last_destructor_value = NULL;
    iHashTable.SetDestructor(base, count_destructor);
    TEST_REQUIRE(iHashTable.Erase(base, "same", 4) == 1);
    TEST_REQUIRE(destructor_calls == 1 && last_destructor_value != NULL);
    TEST_REQUIRE(iHashTable.Clear(base) == 1);
    TEST_REQUIRE(destructor_calls == 2);
    result = 0;

cleanup:
    if (merged != NULL)
        iHashTable.Finalize(merged);
    else if (merge_pool != NULL)
        iPool.Finalize(merge_pool);
    if (empty_merged != NULL)
        iHashTable.Finalize(empty_merged);
    else if (empty_pool != NULL)
        iPool.Finalize(empty_pool);
    if (overlay_result != NULL)
        iHashTable.Finalize(overlay_result);
    else if (overlay_pool != NULL)
        iPool.Finalize(overlay_pool);
    if (bulk_merged != NULL)
        iHashTable.Finalize(bulk_merged);
    else if (bulk_pool != NULL)
        iPool.Finalize(bulk_pool);
    if (bulk != NULL)
        iHashTable.Finalize(bulk);
    if (incompatible != NULL)
        iHashTable.Finalize(incompatible);
    if (incompat_pool != NULL)
        iPool.Finalize(incompat_pool);
    if (copy != NULL)
        iHashTable.Finalize(copy);
    else if (copy_pool != NULL)
        iPool.Finalize(copy_pool);
    if (overlay != NULL)
        iHashTable.Finalize(overlay);
    if (base != NULL)
        iHashTable.Finalize(base);
    if (empty_left != NULL)
        iHashTable.Finalize(empty_left);
    if (empty_right != NULL)
        iHashTable.Finalize(empty_right);
    return result;
}

static int test_save_and_load(void)
{
    HashTable *source = NULL, *loaded = NULL;
    FILE *stream = NULL;
    FILE *variant = NULL;
    unsigned char *serialized = NULL;
    long serialized_size;
    size_t header_size;
    int values[3] = {10, 20, 30};
    char large_key[130];
    int large_value = 40;
    size_t i;
    const char *keys[] = {"one", "two", "three"};
    int result = -1;

    source = iHashTable.Create(sizeof(int));
    TEST_REQUIRE(source != NULL);
    for (i = 0; i < 3; ++i)
        TEST_REQUIRE(iHashTable.Add(source, keys[i], strlen(keys[i]), &values[i]) == 1);
    memset(large_key, 'L', sizeof(large_key));
    large_key[sizeof(large_key) - 1] = '\0';
    TEST_REQUIRE(iHashTable.Add(source, large_key, strlen(large_key), &large_value) == 1);
    stream = ccl_test_tmpfile();
    TEST_REQUIRE(stream != NULL);
    TEST_REQUIRE(iHashTable.Save(source, stream, NULL, NULL) == 1);
    TEST_REQUIRE(iHashTable.Save(source, stream, failing_save, NULL) == 0);
    rewind(stream);
    loaded = iHashTable.Load(stream, NULL, NULL);
    TEST_REQUIRE(loaded != NULL && iHashTable.Size(loaded) == 4);
    for (i = 0; i < 3; ++i)
        TEST_REQUIRE(*(int *)iHashTable.GetElement(loaded, keys[i], strlen(keys[i])) == values[i]);
    TEST_REQUIRE(*(int *)iHashTable.GetElement(loaded, large_key, strlen(large_key)) == large_value);
    iHashTable.Finalize(loaded);
    loaded = NULL;

    TEST_REQUIRE(fseek(stream, 0, SEEK_END) == 0);
    serialized_size = ftell(stream);
    TEST_REQUIRE(serialized_size > 0);
    serialized = malloc((size_t)serialized_size);
    TEST_REQUIRE(serialized != NULL);
    rewind(stream);
    TEST_REQUIRE(fread(serialized, 1, (size_t)serialized_size, stream) ==
                 (size_t)serialized_size);
    header_size = sizeof(guid) + sizeof(HashTable);

    variant = ccl_test_tmpfile();
    TEST_REQUIRE(variant != NULL);
    serialized[0] ^= 0xff;
    TEST_REQUIRE(fwrite(serialized, 1, (size_t)serialized_size, variant) ==
                 (size_t)serialized_size);
    rewind(variant);
    TEST_REQUIRE(iHashTable.Load(variant, NULL, NULL) == NULL);
    fclose(variant);
    variant = ccl_test_tmpfile();
    TEST_REQUIRE(variant != NULL);
    serialized[0] ^= 0xff;
    for (i = 0; i < 10; ++i)
        serialized[header_size + i] = 0x80;
    TEST_REQUIRE(fwrite(serialized, 1, (size_t)serialized_size, variant) ==
                 (size_t)serialized_size);
    rewind(variant);
    TEST_REQUIRE(iHashTable.Load(variant, NULL, NULL) == NULL);
    fclose(variant);
    variant = ccl_test_tmpfile();
    TEST_REQUIRE(variant != NULL);
    TEST_REQUIRE(fwrite(serialized, 1, (size_t)serialized_size - 1, variant) ==
                 (size_t)serialized_size - 1);
    rewind(variant);
    TEST_REQUIRE(iHashTable.Load(variant, NULL, NULL) == NULL);
    fclose(variant);
    variant = ccl_test_tmpfile();
    TEST_REQUIRE(variant != NULL);
    serialized[header_size] = 3;
    serialized[header_size + 1] = 'o';
    serialized[header_size + 2] = 'n';
    serialized[header_size + 3] = 'e';
    rewind(stream);
    TEST_REQUIRE(fwrite(serialized, 1, (size_t)serialized_size, variant) ==
                 (size_t)serialized_size);
    rewind(variant);
    TEST_REQUIRE(iHashTable.Load(variant, failing_load, NULL) == NULL);
    result = 0;

cleanup:
    if (stream != NULL)
        fclose(stream);
    if (variant != NULL)
        fclose(variant);
    free(serialized);
    if (loaded != NULL)
        iHashTable.Finalize(loaded);
    if (source != NULL)
        iHashTable.Finalize(source);
    return result;
}

static const TestCase hashtable_tests[] = {
    {"basic binary keys and large values", test_basic_binary_and_large_values},
    {"resize and iterators", test_resize_and_iterators},
    {"copy merge callbacks and destructor", test_copy_merge_callbacks_and_destructor},
    {"save and load", test_save_and_load},
    {"argument validation and readonly", test_argument_validation_and_readonly},
};

static const TestSuite hashtable_suite = {
    "hashtable",
    hashtable_tests,
    sizeof(hashtable_tests) / sizeof(hashtable_tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &hashtable_suite;
}
