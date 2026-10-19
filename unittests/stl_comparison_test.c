#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "containers.h"
#include "ccl_internal.h"
#include "test_support.h"

/* Helper structure to accumulate list elements during iList.Apply */
typedef struct {
    int sum;
    size_t count;
    int elements[16];
} ListAccumulator;

static int list_accumulate_apply(void *element, void *arg)
{
    ListAccumulator *acc = (ListAccumulator *)arg;
    int val = *(int *)element;
    if (acc->count < sizeof(acc->elements) / sizeof(acc->elements[0])) {
        acc->elements[acc->count] = val;
    }
    acc->count++;
    acc->sum += val;
    return 1;
}

/* Word token extraction from §9.3.4 */
static char *GetWord(char *line)
{
    if (line == NULL || *line == 0)
        return NULL;
    if (!(isalpha((unsigned char)*line) || isdigit((unsigned char)*line)))
        return line + 1;
    while (isalpha((unsigned char)*line) || isdigit((unsigned char)*line))
        line++;
    return line;
}

/* §9.3.1 Filling & Sorting Integer Table */
static int test_fill_and_sort_integer_table(void)
{
    ValArrayInt *table = NULL;
    size_t cap;
    size_t i;

    table = iValArrayInt.Create(100);
    TEST_REQUIRE(table != NULL);

    cap = iValArrayInt.GetCapacity(table);
    TEST_REQUIRE(cap >= 100);

    for (i = 0; i < cap; i++) {
        int val = (int)((cap - i) * 31 % 997);
        TEST_REQUIRE(iValArrayInt.Add(table, val) > 0);
    }
    TEST_REQUIRE(iValArrayInt.Size(table) == cap);

    TEST_REQUIRE(iValArrayInt.Sort(table) == 1);
    TEST_REQUIRE(iValArrayInt.Size(table) == cap);

    for (i = 1; i < cap; i++) {
        TEST_REQUIRE(iValArrayInt.GetElement(table, i - 1) <= iValArrayInt.GetElement(table, i));
    }

    iValArrayInt.Finalize(table);
    table = NULL;
    return 0;

cleanup:
    if (table != NULL)
        iValArrayInt.Finalize(table);
    return -1;
}

/* §9.3.2 Filling & Traversing Integer List */
static int test_fill_and_traverse_integer_list(void)
{
    List *list = NULL;
    int data;
    ListAccumulator acc;

    list = iList.Create(sizeof(int));
    TEST_REQUIRE(list != NULL);
    TEST_REQUIRE(iList.Size(list) == 0);

    /* Insert at end (Add) */
    data = 0;
    TEST_REQUIRE(iList.Add(list, &data) > 0);

    /* Insert at front (PushFront) */
    TEST_REQUIRE(iList.PushFront(list, &data) > 0);

    /* Insert in middle (InsertAt) */
    data = 2;
    TEST_REQUIRE(iList.InsertAt(list, 1, &data) > 0);

    /* Append remaining elements at end */
    data = 5;
    TEST_REQUIRE(iList.Add(list, &data) > 0);
    data = 6;
    TEST_REQUIRE(iList.Add(list, &data) > 0);

    TEST_REQUIRE(iList.Size(list) == 5);

    /* Traverse and accumulate elements */
    memset(&acc, 0, sizeof(acc));
    TEST_REQUIRE(iList.Apply(list, list_accumulate_apply, &acc) == 1);

    /* Expected sequence: 0, 2, 0, 5, 6 */
    TEST_REQUIRE(acc.count == 5);
    TEST_REQUIRE(acc.sum == 13);
    TEST_REQUIRE(acc.elements[0] == 0);
    TEST_REQUIRE(acc.elements[1] == 2);
    TEST_REQUIRE(acc.elements[2] == 0);
    TEST_REQUIRE(acc.elements[3] == 5);
    TEST_REQUIRE(acc.elements[4] == 6);

    iList.Finalize(list);
    list = NULL;
    return 0;

cleanup:
    if (list != NULL)
        iList.Finalize(list);
    return -1;
}

/* §9.3.3 Sorting Text Files */
static int test_sort_text_files(void)
{
    const char *input_file = "test_stl_unsorted.txt";
    const char *output_file = "test_stl_sorted.txt";
    FILE *f = NULL;
    strCollection *data = NULL;
    strCollection *sorted_data = NULL;
    size_t count;
    size_t i;

    f = fopen(input_file, "w");
    TEST_REQUIRE(f != NULL);
    fputs("zebra\n", f);
    fputs("apple\n", f);
    fputs("mango\n", f);
    fputs("banana\n", f);
    fputs("orange\n", f);
    fclose(f);
    f = NULL;

    /* Read unsorted lines from file */
    data = istrCollection.CreateFromFile(input_file);
    TEST_REQUIRE(data != NULL);
    count = istrCollection.Size(data);
    TEST_REQUIRE(count == 5);

    /* Sort lines */
    TEST_REQUIRE(istrCollection.Sort(data) == 1);

    /* Write sorted output to file */
    TEST_REQUIRE(istrCollection.WriteToFile(data, output_file) == 1);

    /* Read back output file and verify sorted line order */
    sorted_data = istrCollection.CreateFromFile(output_file);
    TEST_REQUIRE(sorted_data != NULL);
    TEST_REQUIRE(istrCollection.Size(sorted_data) == count);

    for (i = 1; i < count; i++) {
        const char *prev = istrCollection.GetElement(sorted_data, i - 1);
        const char *curr = istrCollection.GetElement(sorted_data, i);
        TEST_REQUIRE(prev != NULL && curr != NULL);
        TEST_REQUIRE(strcmp(prev, curr) <= 0);
    }
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sorted_data, 0), "apple") == 0);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sorted_data, 1), "banana") == 0);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sorted_data, 2), "mango") == 0);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sorted_data, 3), "orange") == 0);
    TEST_REQUIRE(strcmp(istrCollection.GetElement(sorted_data, 4), "zebra") == 0);

    /* Clean up collections and temporary files */
    istrCollection.Finalize(data);
    data = NULL;
    istrCollection.Finalize(sorted_data);
    sorted_data = NULL;
    remove(input_file);
    remove(output_file);
    return 0;

cleanup:
    if (f != NULL)
        fclose(f);
    if (data != NULL)
        istrCollection.Finalize(data);
    if (sorted_data != NULL)
        istrCollection.Finalize(sorted_data);
    remove(input_file);
    remove(output_file);
    return -1;
}

/* §9.3.4 Storing a container in another container (Inverted Word Index / Concordance) */
static int test_nested_container_concordance(void)
{
    const char *concordance_file = "test_stl_concordance.txt";
    FILE *f = NULL;
    Dictionary *dict = NULL;
    strCollection *text = NULL;
    strCollection *words = NULL;
    ValArrayInt *positions = NULL;
    char *start, *end, *line;
    size_t top, num_words;
    size_t i;
    int c;

    /* Prepare input text file with repeated and unique words across lines */
    f = fopen(concordance_file, "w");
    TEST_REQUIRE(f != NULL);
    fputs("ccl containers library\n", f);
    fputs("containers and algorithms\n", f);
    fputs("ccl stl comparison\n", f);
    fputs("ccl containers stl ccl\n", f);
    fclose(f);
    f = NULL;

    dict = iDictionary.Create(sizeof(ValArrayInt *), 100);
    TEST_REQUIRE(dict != NULL);

    text = istrCollection.CreateFromFile(concordance_file);
    TEST_REQUIRE(text != NULL);

    top = istrCollection.Size(text);
    TEST_REQUIRE(top == 4);

    for (i = 0; i < top; i++) {
        line = istrCollection.GetElement(text, i);
        TEST_REQUIRE(line != NULL);
        start = line;
        while (isspace((unsigned char)*start))
            start++;
        end = GetWord(start);
        while (end != NULL) {
            c = *end;
            *end = 0;
            if (!iDictionary.Contains(dict, start)) {
                positions = iValArrayInt.Create(5);
                TEST_REQUIRE(positions != NULL);
                TEST_REQUIRE(iDictionary.Add(dict, start, &positions) == 1);
            } else {
                TEST_REQUIRE(iDictionary.CopyElement(dict, start, &positions) == 1);
            }
            TEST_REQUIRE(iValArrayInt.Add(positions, (int)i) > 0);
            *end = (char)c;
            start = end;
            while (isspace((unsigned char)*start))
                start++;
            end = GetWord(start);
        }
    }

    /* Query words from dictionary */
    words = iDictionary.GetKeys(dict);
    TEST_REQUIRE(words != NULL);
    num_words = istrCollection.Size(words);
    TEST_REQUIRE(num_words == 7);

    /* Verify word-to-line mappings for repeated and unique words */
    /* Repeated across multiple lines and within a line: "ccl" -> [0, 2, 3, 3] */
    TEST_REQUIRE(iDictionary.Contains(dict, "ccl") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "ccl", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 4);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 0);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 1) == 2);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 2) == 3);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 3) == 3);

    /* Repeated across multiple lines: "containers" -> [0, 1, 3] */
    TEST_REQUIRE(iDictionary.Contains(dict, "containers") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "containers", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 3);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 0);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 1) == 1);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 2) == 3);

    /* Repeated across multiple lines: "stl" -> [2, 3] */
    TEST_REQUIRE(iDictionary.Contains(dict, "stl") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "stl", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 2);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 2);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 1) == 3);

    /* Unique words: each appears on exactly one line */
    TEST_REQUIRE(iDictionary.Contains(dict, "library") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "library", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 1);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 0);

    TEST_REQUIRE(iDictionary.Contains(dict, "and") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "and", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 1);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 1);

    TEST_REQUIRE(iDictionary.Contains(dict, "algorithms") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "algorithms", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 1);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 1);

    TEST_REQUIRE(iDictionary.Contains(dict, "comparison") == 1);
    positions = NULL;
    TEST_REQUIRE(iDictionary.CopyElement(dict, "comparison", &positions) == 1);
    TEST_REQUIRE(positions != NULL);
    TEST_REQUIRE(iValArrayInt.Size(positions) == 1);
    TEST_REQUIRE(iValArrayInt.GetElement(positions, 0) == 2);

    /* Verify non-existent word */
    TEST_REQUIRE(iDictionary.Contains(dict, "absent") == 0);

    /* Verify that every key extracted is valid and present in dict */
    for (i = 0; i < num_words; i++) {
        char *word = istrCollection.GetElement(words, i);
        TEST_REQUIRE(word != NULL);
        TEST_REQUIRE(iDictionary.Contains(dict, word) == 1);
        positions = NULL;
        TEST_REQUIRE(iDictionary.CopyElement(dict, word, &positions) == 1);
        TEST_REQUIRE(positions != NULL);
        TEST_REQUIRE(iValArrayInt.Size(positions) >= 1);
    }

    /* Clean up: retrieve positions with CopyElement and finalize with iValArrayInt.Finalize */
    for (i = 0; i < num_words; i++) {
        char *word = istrCollection.GetElement(words, i);
        positions = NULL;
        if (iDictionary.CopyElement(dict, word, &positions) == 1 && positions != NULL) {
            iValArrayInt.Finalize(positions);
            ValArrayInt *null_pos = NULL;
            iDictionary.Replace(dict, word, &null_pos);
        }
    }

    istrCollection.Finalize(words);
    words = NULL;
    istrCollection.Finalize(text);
    text = NULL;
    iDictionary.Finalize(dict);
    dict = NULL;
    remove(concordance_file);
    return 0;

cleanup:
    if (f != NULL)
        fclose(f);
    if (dict != NULL) {
        strCollection *cleanup_keys = (words != NULL) ? words : iDictionary.GetKeys(dict);
        if (cleanup_keys != NULL) {
            size_t nkeys = istrCollection.Size(cleanup_keys);
            for (i = 0; i < nkeys; i++) {
                char *word = istrCollection.GetElement(cleanup_keys, i);
                ValArrayInt *pos = NULL;
                if (iDictionary.CopyElement(dict, word, &pos) == 1 && pos != NULL) {
                    iValArrayInt.Finalize(pos);
                    ValArrayInt *null_pos = NULL;
                    iDictionary.Replace(dict, word, &null_pos);
                }
            }
            if (cleanup_keys != words) {
                istrCollection.Finalize(cleanup_keys);
            }
        }
    }
    if (words != NULL)
        istrCollection.Finalize(words);
    if (text != NULL)
        istrCollection.Finalize(text);
    if (dict != NULL)
        iDictionary.Finalize(dict);
    remove(concordance_file);
    return -1;
}

static const TestCase tests[] = {
    {"filling and sorting integer table (§9.3.1)", test_fill_and_sort_integer_table},
    {"filling and traversing integer list (§9.3.2)", test_fill_and_traverse_integer_list},
    {"sorting text files (§9.3.3)", test_sort_text_files},
    {"concordance inverted word index (§9.3.4)", test_nested_container_concordance},
};

const TestSuite *ccl_get_test_suite(void)
{
    static const TestSuite suite = {
        "stl_comparison",
        tests,
        sizeof(tests) / sizeof(tests[0])
    };
    return &suite;
}
