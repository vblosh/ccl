#include "test_support.h"

#include "containers.h"
#include "ccl_internal.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Quiet error handler used during error injection tests */
static void *quiet_error(const char *name, int code, ...)
{
    (void)name;
    (void)code;
    return NULL;
}

/* Helper to write text content to a disk file */
static int write_text_file(const char *path, const char *content)
{
    FILE *f;

    if (path == NULL)
        return -1;

    f = fopen(path, "w");
    if (f == NULL)
        return -1;

    if (content != NULL && *content != '\0') {
        if (fputs(content, f) == EOF) {
            fclose(f);
            remove(path);
            return -1;
        }
    }
    fclose(f);
    return 0;
}

/* Helper to verify entire stream content matches expected string */
static int verify_stream_content(FILE *stream, const char *expected)
{
    char buf[8192];
    size_t nread;
    size_t exp_len;

    if (stream == NULL || expected == NULL)
        return -1;

    rewind(stream);
    nread = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[nread] = '\0';
    exp_len = strlen(expected);

    if (nread != exp_len || strcmp(buf, expected) != 0)
        return -1;

    return 0;
}

/****************************************************************************
 * §7.1 Unique: Deduplicating lines of text using Dictionary with           *
 * ElementSize = 0 as a hash set.                                            *
 ****************************************************************************/

static int app_unique(FILE *in, FILE *out)
{
    char buf[8192];
    Dictionary *dict;
    int r;

    if (in == NULL || out == NULL)
        return -1;

    dict = iDictionary.Create(0, 500);
    if (dict == NULL)
        return -1;

    while (fgets(buf, sizeof(buf), in) != NULL) {
        r = iDictionary.Add(dict, buf, NULL);
        if (r > 0) {
            if (fputs(buf, out) == EOF) {
                iDictionary.Finalize(dict);
                return -1;
            }
        } else if (r < 0) {
            iDictionary.Finalize(dict);
            return -1;
        }
    }

    iDictionary.Finalize(dict);
    return 0;
}

static int test_unique(void)
{
    FILE *in = NULL;
    FILE *out = NULL;
    Dictionary *dict = NULL;

    /* 1. Mixed duplicates: verify first occurrence order preserved and duplicates skipped */
    {
        const char *input =
            "apple\n"
            "banana\n"
            "apple\n"
            "cherry\n"
            "banana\n"
            "date\n"
            "cherry\n"
            "apple\n";
        const char *expected =
            "apple\n"
            "banana\n"
            "cherry\n"
            "date\n";

        in = ccl_test_tmpfile();
        out = ccl_test_tmpfile();
        TEST_REQUIRE(in != NULL && out != NULL);
        TEST_REQUIRE(fputs(input, in) != EOF);
        rewind(in);

        TEST_REQUIRE(app_unique(in, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(in);
        fclose(out);
        in = NULL;
        out = NULL;
    }

    /* 2. All unique lines */
    {
        const char *input =
            "alpha\n"
            "beta\n"
            "gamma\n"
            "delta\n";
        const char *expected =
            "alpha\n"
            "beta\n"
            "gamma\n"
            "delta\n";

        in = ccl_test_tmpfile();
        out = ccl_test_tmpfile();
        TEST_REQUIRE(in != NULL && out != NULL);
        TEST_REQUIRE(fputs(input, in) != EOF);
        rewind(in);

        TEST_REQUIRE(app_unique(in, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(in);
        fclose(out);
        in = NULL;
        out = NULL;
    }

    /* 3. All duplicate lines */
    {
        const char *input =
            "repeat\n"
            "repeat\n"
            "repeat\n"
            "repeat\n";
        const char *expected =
            "repeat\n";

        in = ccl_test_tmpfile();
        out = ccl_test_tmpfile();
        TEST_REQUIRE(in != NULL && out != NULL);
        TEST_REQUIRE(fputs(input, in) != EOF);
        rewind(in);

        TEST_REQUIRE(app_unique(in, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(in);
        fclose(out);
        in = NULL;
        out = NULL;
    }

    /* 4. Empty file */
    {
        in = ccl_test_tmpfile();
        out = ccl_test_tmpfile();
        TEST_REQUIRE(in != NULL && out != NULL);

        TEST_REQUIRE(app_unique(in, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, "") == 0);

        fclose(in);
        fclose(out);
        in = NULL;
        out = NULL;
    }

    /* 5. Direct verification of Dictionary as hash set (ElementSize = 0) */
    dict = iDictionary.Create(0, 500);
    TEST_REQUIRE(dict != NULL);
    TEST_REQUIRE(iDictionary.GetElementSize(dict) == 0);
    TEST_REQUIRE(iDictionary.Add(dict, "line1\n", NULL) == 1);
    TEST_REQUIRE(iDictionary.Add(dict, "line1\n", NULL) == 0); /* duplicate skipped */
    TEST_REQUIRE(iDictionary.Add(dict, "line2\n", NULL) == 1);
    TEST_REQUIRE(iDictionary.Size(dict) == 2);
    TEST_REQUIRE(iDictionary.Finalize(dict) == 1);
    dict = NULL;

    /* 6. Error handling with NULL arguments */
    TEST_REQUIRE(app_unique(NULL, NULL) == -1);

    return 0;

cleanup:
    if (dict != NULL)
        iDictionary.Finalize(dict);
    if (in != NULL)
        fclose(in);
    if (out != NULL)
        fclose(out);
    return -1;
}

/****************************************************************************
 * §7.2 Paste: Merging two files line-by-line separated by tab using        *
 * strCollection iterators.                                                 *
 ****************************************************************************/

static int app_paste(const char *path1, const char *path2, FILE *out)
{
    strCollection *file1 = NULL;
    strCollection *file2 = NULL;
    Iterator *it1 = NULL;
    Iterator *it2 = NULL;
    char *line1 = NULL;
    char *line2 = NULL;
    int result = -1;

    if (path1 == NULL || path2 == NULL || out == NULL)
        return -1;

    file1 = istrCollection.CreateFromFile(path1);
    file2 = istrCollection.CreateFromFile(path2);
    if (file1 == NULL || file2 == NULL)
        goto cleanup;

    it1 = istrCollection.NewIterator(file1);
    it2 = istrCollection.NewIterator(file2);
    if (it1 == NULL || it2 == NULL)
        goto cleanup;

    line2 = (char *)it2->GetFirst(it2);
    line1 = (char *)it1->GetFirst(it1);
    for (; line1 != NULL; line1 = (char *)it1->GetNext(it1)) {
        if (line2 != NULL) {
            fprintf(out, "%s\t%s\n", line1, line2);
            line2 = (char *)it2->GetNext(it2);
        } else {
            fprintf(out, "%s\n", line1);
        }
    }
    result = 0;

cleanup:
    if (it1 != NULL)
        istrCollection.DeleteIterator(it1);
    if (it2 != NULL)
        istrCollection.DeleteIterator(it2);
    if (file1 != NULL)
        istrCollection.Finalize(file1);
    if (file2 != NULL)
        istrCollection.Finalize(file2);
    return result;
}

static int test_paste(void)
{
    const char *f1_path = "test_applications_paste_f1.txt";
    const char *f2_path = "test_applications_paste_f2.txt";
    FILE *out = NULL;

    /* 1. Equal line counts */
    {
        const char *f1_content = "alpha\nbeta\ngamma\n";
        const char *f2_content = "one\ntwo\nthree\n";
        const char *expected =
            "alpha\tone\n"
            "beta\ttwo\n"
            "gamma\tthree\n";

        TEST_REQUIRE(write_text_file(f1_path, f1_content) == 0);
        TEST_REQUIRE(write_text_file(f2_path, f2_content) == 0);

        out = ccl_test_tmpfile();
        TEST_REQUIRE(out != NULL);
        TEST_REQUIRE(app_paste(f1_path, f2_path, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(out);
        out = NULL;
        remove(f1_path);
        remove(f2_path);
    }

    /* 2. File 1 longer than File 2 (prints remaining line1 without tab) */
    {
        const char *f1_content = "alpha\nbeta\ngamma\ndelta\n";
        const char *f2_content = "one\ntwo\n";
        const char *expected =
            "alpha\tone\n"
            "beta\ttwo\n"
            "gamma\n"
            "delta\n";

        TEST_REQUIRE(write_text_file(f1_path, f1_content) == 0);
        TEST_REQUIRE(write_text_file(f2_path, f2_content) == 0);

        out = ccl_test_tmpfile();
        TEST_REQUIRE(out != NULL);
        TEST_REQUIRE(app_paste(f1_path, f2_path, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(out);
        out = NULL;
        remove(f1_path);
        remove(f2_path);
    }

    /* 3. File 2 longer than File 1 (stops when file1 exhausted) */
    {
        const char *f1_content = "alpha\nbeta\n";
        const char *f2_content = "one\ntwo\nthree\nfour\n";
        const char *expected =
            "alpha\tone\n"
            "beta\ttwo\n";

        TEST_REQUIRE(write_text_file(f1_path, f1_content) == 0);
        TEST_REQUIRE(write_text_file(f2_path, f2_content) == 0);

        out = ccl_test_tmpfile();
        TEST_REQUIRE(out != NULL);
        TEST_REQUIRE(app_paste(f1_path, f2_path, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(out);
        out = NULL;
        remove(f1_path);
        remove(f2_path);
    }

    /* 4. Empty file handling */
    /* 4a. File 1 empty, File 2 non-empty (stops immediately, file1 exhausted) */
    {
        const char *f1_content = "";
        const char *f2_content = "one\ntwo\n";
        const char *expected = "";

        TEST_REQUIRE(write_text_file(f1_path, f1_content) == 0);
        TEST_REQUIRE(write_text_file(f2_path, f2_content) == 0);

        out = ccl_test_tmpfile();
        TEST_REQUIRE(out != NULL);
        TEST_REQUIRE(app_paste(f1_path, f2_path, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(out);
        out = NULL;
        remove(f1_path);
        remove(f2_path);
    }

    /* 4b. File 1 non-empty, File 2 empty (prints line1 without tab) */
    {
        const char *f1_content = "alpha\nbeta\n";
        const char *f2_content = "";
        const char *expected = "alpha\nbeta\n";

        TEST_REQUIRE(write_text_file(f1_path, f1_content) == 0);
        TEST_REQUIRE(write_text_file(f2_path, f2_content) == 0);

        out = ccl_test_tmpfile();
        TEST_REQUIRE(out != NULL);
        TEST_REQUIRE(app_paste(f1_path, f2_path, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(out);
        out = NULL;
        remove(f1_path);
        remove(f2_path);
    }

    /* 4c. Both files empty */
    {
        const char *f1_content = "";
        const char *f2_content = "";
        const char *expected = "";

        TEST_REQUIRE(write_text_file(f1_path, f1_content) == 0);
        TEST_REQUIRE(write_text_file(f2_path, f2_content) == 0);

        out = ccl_test_tmpfile();
        TEST_REQUIRE(out != NULL);
        TEST_REQUIRE(app_paste(f1_path, f2_path, out) == 0);
        TEST_REQUIRE(verify_stream_content(out, expected) == 0);

        fclose(out);
        out = NULL;
        remove(f1_path);
        remove(f2_path);
    }

    /* 5. Non-existent file handling exercising cleanup */
    {
        ErrorFunction saved = iError.SetErrorFunction(quiet_error);
        /* Both non-existent */
        TEST_REQUIRE(app_paste("non_existent_file_1.txt", "non_existent_file_2.txt", out) == -1);

        /* First exists, second non-existent (tests cleanup of file1) */
        TEST_REQUIRE(write_text_file(f1_path, "only first\n") == 0);
        TEST_REQUIRE(app_paste(f1_path, "non_existent_file_2.txt", out) == -1);
        remove(f1_path);

        iError.SetErrorFunction(saved);
    }

    /* 6. Argument validation */
    TEST_REQUIRE(app_paste(NULL, f2_path, out) == -1);
    TEST_REQUIRE(app_paste(f1_path, NULL, out) == -1);
    TEST_REQUIRE(app_paste(f1_path, f2_path, NULL) == -1);

    return 0;

cleanup:
    if (out != NULL)
        fclose(out);
    remove(f1_path);
    remove(f2_path);
    return -1;
}

/****************************************************************************
 * §7.3 Mapcar & Mapcon:                                                    *
 * - Mapcar for List via Apply using struct MapcarArgs and DoAbsValue       *
 * - Generic Mapcar for SequentialContainer via Iterator                     *
 * - Generic Mapcon combining two SequentialContainers pairwise using DoAdd *
 ****************************************************************************/

struct MapcarArgs {
    void *(*fn)(void *);
    List *Result;
};

static void *DoAbsValue(void *element)
{
    static int result;
    result = *(int *)element;
    if (result < 0)
        result = -result;
    return &result;
}

static int Applyfn(void *element, void *arg)
{
    struct MapcarArgs *args = (struct MapcarArgs *)arg;
    void *result = args->fn(element);
    return iList.Add(args->Result, result);
}

static List *mapcar_list(List *li, void *(*fn)(void *))
{
    struct MapcarArgs args;

    if (li == NULL || fn == NULL)
        return NULL;

    args.fn = fn;
    args.Result = iList.Create(iList.GetElementSize(li));
    if (args.Result == NULL)
        return NULL;

    iList.Apply(li, Applyfn, (void *)&args);
    return args.Result;
}

static int generic_mapcar(SequentialContainer *src,
                          void *(*fn)(void *),
                          SequentialContainer *result)
{
    Iterator *it;
    int r = 1;
    void *obj;

    if (src == NULL || fn == NULL || result == NULL)
        return CONTAINER_ERROR_BADARG;

    it = iSequentialContainer.NewIterator(src);
    if (it == NULL)
        return CONTAINER_ERROR_NOMEMORY;

    for (obj = it->GetFirst(it); obj != NULL; obj = it->GetNext(it)) {
        void *tmp = fn(obj);
        r = iSequentialContainer.Add(result, tmp);
        if (r < 0)
            break;
    }

    iSequentialContainer.DeleteIterator(it);
    return r;
}

static void *DoAdd(void *element1, void *element2)
{
    static int result;
    result = *(int *)element1 + *(int *)element2;
    return &result;
}

static int generic_mapcon(SequentialContainer *src1,
                          SequentialContainer *src2,
                          void *(*fn)(void *, void *),
                          SequentialContainer *result)
{
    Iterator *it1 = NULL;
    Iterator *it2 = NULL;
    int r = 1;
    void *obj1;
    void *obj2;

    if (src1 == NULL || src2 == NULL || fn == NULL || result == NULL)
        return CONTAINER_ERROR_BADARG;

    if (iSequentialContainer.GetElementSize(src1) !=
        iSequentialContainer.GetElementSize(src2)) {
        return CONTAINER_ERROR_INCOMPATIBLE;
    }

    it1 = iSequentialContainer.NewIterator(src1);
    if (it1 == NULL)
        return CONTAINER_ERROR_NOMEMORY;

    it2 = iSequentialContainer.NewIterator(src2);
    if (it2 == NULL) {
        iSequentialContainer.DeleteIterator(it1);
        return CONTAINER_ERROR_NOMEMORY;
    }

    for (obj1 = it1->GetFirst(it1), obj2 = it2->GetFirst(it2);
         obj1 != NULL && obj2 != NULL;
         obj1 = it1->GetNext(it1), obj2 = it2->GetNext(it2)) {
        void *tmp = fn(obj1, obj2);
        r = iSequentialContainer.Add(result, tmp);
        if (r < 0)
            break;
    }

    iSequentialContainer.DeleteIterator(it1);
    iSequentialContainer.DeleteIterator(it2);
    return r;
}

static int test_mapcar_list(void)
{
    List *li = NULL;
    List *newList = NULL;
    List *emptyList = NULL;
    List *newEmpty = NULL;
    int tab[] = {3, -4, 2, -5, -6};
    int expected[] = {3, 4, 2, 5, 6};
    size_t i;

    li = iList.Create(sizeof(int));
    TEST_REQUIRE(li != NULL);
    for (i = 0; i < 5; ++i) {
        TEST_REQUIRE(iList.Add(li, &tab[i]) == 1);
    }
    TEST_REQUIRE(iList.Size(li) == 5);

    newList = mapcar_list(li, DoAbsValue);
    TEST_REQUIRE(newList != NULL);
    TEST_REQUIRE(iList.Size(newList) == 5);
    TEST_REQUIRE(iList.GetElementSize(newList) == sizeof(int));
    for (i = 0; i < 5; ++i) {
        int *val = (int *)iList.GetElement(newList, i);
        TEST_REQUIRE(val != NULL && *val == expected[i]);
    }

    /* Empty list handling */
    emptyList = iList.Create(sizeof(int));
    TEST_REQUIRE(emptyList != NULL);
    newEmpty = mapcar_list(emptyList, DoAbsValue);
    TEST_REQUIRE(newEmpty != NULL);
    TEST_REQUIRE(iList.Size(newEmpty) == 0);

    /* Bad arguments */
    TEST_REQUIRE(mapcar_list(NULL, DoAbsValue) == NULL);
    TEST_REQUIRE(mapcar_list(li, NULL) == NULL);

    iList.Finalize(newEmpty);
    iList.Finalize(emptyList);
    iList.Finalize(newList);
    iList.Finalize(li);
    return 0;

cleanup:
    if (newEmpty != NULL)
        iList.Finalize(newEmpty);
    if (emptyList != NULL)
        iList.Finalize(emptyList);
    if (newList != NULL)
        iList.Finalize(newList);
    if (li != NULL)
        iList.Finalize(li);
    return -1;
}

static int test_mapcar_generic(void)
{
    Vector *src_vec = NULL;
    Vector *dst_vec = NULL;
    List *src_list = NULL;
    Vector *dst_vec2 = NULL;
    List *dst_list = NULL;
    Vector *empty_src = NULL;
    Vector *empty_dst = NULL;
    int vec_vals[] = {10, -20, 30, -40, 50};
    int expected_vec[] = {10, 20, 30, 40, 50};
    int list_vals[] = {-1, -2, 3, -4};
    int expected_list[] = {1, 2, 3, 4};
    size_t i;
    int rc;

    /* 1. Vector -> Vector */
    src_vec = iVector.Create(sizeof(int), 5);
    dst_vec = iVector.Create(sizeof(int), 5);
    TEST_REQUIRE(src_vec != NULL && dst_vec != NULL);
    for (i = 0; i < 5; ++i) {
        TEST_REQUIRE(iVector.Add(src_vec, &vec_vals[i]) == 1);
    }
    rc = generic_mapcar((SequentialContainer *)src_vec, DoAbsValue,
                        (SequentialContainer *)dst_vec);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(dst_vec) == 5);
    for (i = 0; i < 5; ++i) {
        int *val = (int *)iVector.GetElement(dst_vec, i);
        TEST_REQUIRE(val != NULL && *val == expected_vec[i]);
    }

    /* 2. Cross-container: List -> Vector */
    src_list = iList.Create(sizeof(int));
    dst_vec2 = iVector.Create(sizeof(int), 4);
    TEST_REQUIRE(src_list != NULL && dst_vec2 != NULL);
    for (i = 0; i < 4; ++i) {
        TEST_REQUIRE(iList.Add(src_list, &list_vals[i]) == 1);
    }
    rc = generic_mapcar((SequentialContainer *)src_list, DoAbsValue,
                        (SequentialContainer *)dst_vec2);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(dst_vec2) == 4);
    for (i = 0; i < 4; ++i) {
        int *val = (int *)iVector.GetElement(dst_vec2, i);
        TEST_REQUIRE(val != NULL && *val == expected_list[i]);
    }

    /* 3. Cross-container: Vector -> List */
    dst_list = iList.Create(sizeof(int));
    TEST_REQUIRE(dst_list != NULL);
    rc = generic_mapcar((SequentialContainer *)src_vec, DoAbsValue,
                        (SequentialContainer *)dst_list);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iList.Size(dst_list) == 5);
    for (i = 0; i < 5; ++i) {
        int *val = (int *)iList.GetElement(dst_list, i);
        TEST_REQUIRE(val != NULL && *val == expected_vec[i]);
    }

    /* 4. Empty container */
    empty_src = iVector.Create(sizeof(int), 0);
    empty_dst = iVector.Create(sizeof(int), 0);
    TEST_REQUIRE(empty_src != NULL && empty_dst != NULL);
    rc = generic_mapcar((SequentialContainer *)empty_src, DoAbsValue,
                        (SequentialContainer *)empty_dst);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(empty_dst) == 0);

    /* 5. Argument validation */
    TEST_REQUIRE(generic_mapcar(NULL, DoAbsValue, (SequentialContainer *)dst_vec) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(generic_mapcar((SequentialContainer *)src_vec, NULL, (SequentialContainer *)dst_vec) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(generic_mapcar((SequentialContainer *)src_vec, DoAbsValue, NULL) == CONTAINER_ERROR_BADARG);

    iVector.Finalize(empty_dst);
    iVector.Finalize(empty_src);
    iList.Finalize(dst_list);
    iVector.Finalize(dst_vec2);
    iList.Finalize(src_list);
    iVector.Finalize(dst_vec);
    iVector.Finalize(src_vec);
    return 0;

cleanup:
    if (empty_dst != NULL)
        iVector.Finalize(empty_dst);
    if (empty_src != NULL)
        iVector.Finalize(empty_src);
    if (dst_list != NULL)
        iList.Finalize(dst_list);
    if (dst_vec2 != NULL)
        iVector.Finalize(dst_vec2);
    if (src_list != NULL)
        iList.Finalize(src_list);
    if (dst_vec != NULL)
        iVector.Finalize(dst_vec);
    if (src_vec != NULL)
        iVector.Finalize(src_vec);
    return -1;
}

static int test_mapcon_generic(void)
{
    Vector *v1 = NULL;
    Vector *v2 = NULL;
    Vector *res = NULL;
    Vector *v_long = NULL;
    Vector *v_short = NULL;
    Vector *res_short = NULL;
    Vector *v_empty = NULL;
    Vector *res_empty = NULL;
    List *l1 = NULL;
    Vector *res_cross = NULL;
    Vector *v_double = NULL;
    Vector *res_incompat = NULL;
    int data1[] = {1, 2, 3};
    int data2[] = {10, 20, 30};
    int data_long[] = {1, 2, 3, 4, 5};
    int data_short[] = {100, 200};
    double d_data[] = {1.5, 2.5};
    size_t i;
    int rc;

    /* 1. Equal lengths: Vector + Vector */
    v1 = iVector.Create(sizeof(int), 3);
    v2 = iVector.Create(sizeof(int), 3);
    res = iVector.Create(sizeof(int), 3);
    TEST_REQUIRE(v1 != NULL && v2 != NULL && res != NULL);
    for (i = 0; i < 3; ++i) {
        TEST_REQUIRE(iVector.Add(v1, &data1[i]) == 1);
        TEST_REQUIRE(iVector.Add(v2, &data2[i]) == 1);
    }
    rc = generic_mapcon((SequentialContainer *)v1, (SequentialContainer *)v2,
                        DoAdd, (SequentialContainer *)res);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(res) == 3);
    for (i = 0; i < 3; ++i) {
        int *val = (int *)iVector.GetElement(res, i);
        TEST_REQUIRE(val != NULL && *val == data1[i] + data2[i]);
    }

    /* 2. Length handling: stops on shortest */
    /* Case A: src1 longer than src2 */
    v_long = iVector.Create(sizeof(int), 5);
    v_short = iVector.Create(sizeof(int), 2);
    res_short = iVector.Create(sizeof(int), 5);
    TEST_REQUIRE(v_long != NULL && v_short != NULL && res_short != NULL);
    for (i = 0; i < 5; ++i) {
        TEST_REQUIRE(iVector.Add(v_long, &data_long[i]) == 1);
    }
    for (i = 0; i < 2; ++i) {
        TEST_REQUIRE(iVector.Add(v_short, &data_short[i]) == 1);
    }
    rc = generic_mapcon((SequentialContainer *)v_long, (SequentialContainer *)v_short,
                        DoAdd, (SequentialContainer *)res_short);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(res_short) == 2);
    for (i = 0; i < 2; ++i) {
        int *val = (int *)iVector.GetElement(res_short, i);
        TEST_REQUIRE(val != NULL && *val == data_long[i] + data_short[i]);
    }

    /* Case B: src2 longer than src1 */
    iVector.Clear(res_short);
    rc = generic_mapcon((SequentialContainer *)v_short, (SequentialContainer *)v_long,
                        DoAdd, (SequentialContainer *)res_short);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(res_short) == 2);
    for (i = 0; i < 2; ++i) {
        int *val = (int *)iVector.GetElement(res_short, i);
        TEST_REQUIRE(val != NULL && *val == data_short[i] + data_long[i]);
    }

    /* Case C: empty container */
    v_empty = iVector.Create(sizeof(int), 0);
    res_empty = iVector.Create(sizeof(int), 0);
    TEST_REQUIRE(v_empty != NULL && res_empty != NULL);
    rc = generic_mapcon((SequentialContainer *)v_long, (SequentialContainer *)v_empty,
                        DoAdd, (SequentialContainer *)res_empty);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(res_empty) == 0);

    /* 3. Cross-container: List + Vector -> Vector */
    l1 = iList.Create(sizeof(int));
    res_cross = iVector.Create(sizeof(int), 3);
    TEST_REQUIRE(l1 != NULL && res_cross != NULL);
    for (i = 0; i < 3; ++i) {
        TEST_REQUIRE(iList.Add(l1, &data1[i]) == 1);
    }
    rc = generic_mapcon((SequentialContainer *)l1, (SequentialContainer *)v2,
                        DoAdd, (SequentialContainer *)res_cross);
    TEST_REQUIRE(rc >= 0);
    TEST_REQUIRE(iVector.Size(res_cross) == 3);
    for (i = 0; i < 3; ++i) {
        int *val = (int *)iVector.GetElement(res_cross, i);
        TEST_REQUIRE(val != NULL && *val == data1[i] + data2[i]);
    }

    /* 4. Incompatible element sizes (returns CONTAINER_ERROR_INCOMPATIBLE) */
    v_double = iVector.Create(sizeof(double), 2);
    res_incompat = iVector.Create(sizeof(int), 2);
    TEST_REQUIRE(v_double != NULL && res_incompat != NULL);
    for (i = 0; i < 2; ++i) {
        TEST_REQUIRE(iVector.Add(v_double, &d_data[i]) == 1);
    }
    rc = generic_mapcon((SequentialContainer *)v1, (SequentialContainer *)v_double,
                        DoAdd, (SequentialContainer *)res_incompat);
    TEST_REQUIRE(rc == CONTAINER_ERROR_INCOMPATIBLE);
    TEST_REQUIRE(iVector.Size(res_incompat) == 0);

    /* 5. Argument validation */
    TEST_REQUIRE(generic_mapcon(NULL, (SequentialContainer *)v2, DoAdd, (SequentialContainer *)res) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(generic_mapcon((SequentialContainer *)v1, NULL, DoAdd, (SequentialContainer *)res) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(generic_mapcon((SequentialContainer *)v1, (SequentialContainer *)v2, NULL, (SequentialContainer *)res) == CONTAINER_ERROR_BADARG);
    TEST_REQUIRE(generic_mapcon((SequentialContainer *)v1, (SequentialContainer *)v2, DoAdd, NULL) == CONTAINER_ERROR_BADARG);

    iVector.Finalize(res_incompat);
    iVector.Finalize(v_double);
    iVector.Finalize(res_cross);
    iList.Finalize(l1);
    iVector.Finalize(res_empty);
    iVector.Finalize(v_empty);
    iVector.Finalize(res_short);
    iVector.Finalize(v_short);
    iVector.Finalize(v_long);
    iVector.Finalize(res);
    iVector.Finalize(v2);
    iVector.Finalize(v1);
    return 0;

cleanup:
    if (res_incompat != NULL)
        iVector.Finalize(res_incompat);
    if (v_double != NULL)
        iVector.Finalize(v_double);
    if (res_cross != NULL)
        iVector.Finalize(res_cross);
    if (l1 != NULL)
        iList.Finalize(l1);
    if (res_empty != NULL)
        iVector.Finalize(res_empty);
    if (v_empty != NULL)
        iVector.Finalize(v_empty);
    if (res_short != NULL)
        iVector.Finalize(res_short);
    if (v_short != NULL)
        iVector.Finalize(v_short);
    if (v_long != NULL)
        iVector.Finalize(v_long);
    if (res != NULL)
        iVector.Finalize(res);
    if (v2 != NULL)
        iVector.Finalize(v2);
    if (v1 != NULL)
        iVector.Finalize(v1);
    return -1;
}

/****************************************************************************
 * §7.4 Word offsets:                                                       *
 * - Compute word end offsets into ValArrayInt from text lines              *
 * - Store in Vector of struct WordOffsetPair { char *txt; positions; }    *
 * - Iterate with nested iterators (outer iVector, inner iValArrayInt)      *
 * - Clean up every inner ValArrayInt, iterators, and containers            *
 ****************************************************************************/

struct WordOffsetPair {
    char *txt;
    ValArrayInt *positions;
};

static int isWordSeparator(int c)
{
    return ispunct((unsigned char)c) || isspace((unsigned char)c);
}

static ValArrayInt *GetPositions(char *txt)
{
    char *p = txt;
    unsigned position = 0;
    ValArrayInt *result;

    if (txt == NULL)
        return NULL;

    result = iValArrayInt.Create(20);
    if (result == NULL)
        return NULL;

    while (*p && isWordSeparator(*p))
        p++;
    while (*p) {
        if (isWordSeparator(*p)) {
            position = (unsigned)(p - txt);
            iValArrayInt.Add(result, (int)position);
            while (isWordSeparator(*p))
                p++;
        } else {
            p++;
        }
    }
    if (iValArrayInt.Size(result) > 0)
        iValArrayInt.Add(result, (int)(p - txt));

    return result;
}

static int test_word_offsets(void)
{
    const char *test_path = "test_applications_word_offsets.txt";
    const char *empty_path = "test_applications_empty.txt";
    strCollection *myData = NULL;
    Vector *Map = NULL;
    Iterator *it = NULL;
    Iterator *itPositions = NULL;
    struct WordOffsetPair Current;
    struct WordOffsetPair *pPair = NULL;
    size_t siz;
    size_t i;

    /* Diverse lines exercising punctuation, spaces, empty line, and normal text */
    const char *file_content =
        "Hello, world!\n"
        "  C  containers  library  \n"
        "\n"
        "simple test\n";

    static const int expected_line0[] = {5, 12, 13};
    static const int expected_line1[] = {3, 15, 24, 26};
    /* Line 2 has 0 offsets */
    static const int expected_line3[] = {6, 11};

    static const int *const expected_offsets[] = {
        expected_line0,
        expected_line1,
        NULL,
        expected_line3,
    };
    static const size_t expected_counts[] = {3, 4, 0, 2};
    static const char *const expected_texts[] = {
        "Hello, world!",
        "  C  containers  library  ",
        "",
        "simple test",
    };

    TEST_REQUIRE(write_text_file(test_path, file_content) == 0);

    myData = istrCollection.CreateFromFile(test_path);
    TEST_REQUIRE(myData != NULL);
    siz = istrCollection.Size(myData);
    TEST_REQUIRE(siz == 4);

    Map = iVector.Create(sizeof(struct WordOffsetPair), 50);
    TEST_REQUIRE(Map != NULL);

    for (i = 0; i < siz; ++i) {
        Current.txt = istrCollection.GetElement(myData, i);
        Current.positions = GetPositions(Current.txt);
        TEST_REQUIRE(Current.positions != NULL);
        TEST_REQUIRE(iVector.Add(Map, &Current) == 1);
    }
    TEST_REQUIRE(iVector.Size(Map) == 4);

    /* Iterate with nested iterators (outer iVector, inner iValArrayInt) */
    it = iVector.NewIterator(Map);
    TEST_REQUIRE(it != NULL);

    size_t pair_idx = 0;
    for (pPair = (struct WordOffsetPair *)it->GetFirst(it);
         pPair != NULL;
         pPair = (struct WordOffsetPair *)it->GetNext(it)) {
        int *pPos;
        size_t pos_idx = 0;

        TEST_REQUIRE(pair_idx < 4);
        TEST_REQUIRE(strcmp(pPair->txt, expected_texts[pair_idx]) == 0);
        TEST_REQUIRE(iValArrayInt.Size(pPair->positions) == expected_counts[pair_idx]);

        itPositions = iValArrayInt.NewIterator(pPair->positions);
        TEST_REQUIRE(itPositions != NULL);

        for (pPos = (int *)itPositions->GetFirst(itPositions);
             pPos != NULL;
             pPos = (int *)itPositions->GetNext(itPositions)) {
            TEST_REQUIRE(pos_idx < expected_counts[pair_idx]);
            TEST_REQUIRE(*pPos == expected_offsets[pair_idx][pos_idx]);
            pos_idx++;
        }
        TEST_REQUIRE(pos_idx == expected_counts[pair_idx]);

        iValArrayInt.DeleteIterator(itPositions);
        itPositions = NULL;
        pair_idx++;
    }
    TEST_REQUIRE(pair_idx == 4);

    iVector.DeleteIterator(it);
    it = NULL;

    /* Clean up inner ValArrayInt instances and containers */
    for (i = 0; i < iVector.Size(Map); ++i) {
        struct WordOffsetPair *pair = (struct WordOffsetPair *)iVector.GetElement(Map, i);
        if (pair != NULL && pair->positions != NULL) {
            iValArrayInt.Finalize(pair->positions);
            pair->positions = NULL;
        }
    }
    iVector.Finalize(Map);
    Map = NULL;
    istrCollection.Finalize(myData);
    myData = NULL;
    remove(test_path);

    /* Empty file test */
    TEST_REQUIRE(write_text_file(empty_path, "") == 0);
    myData = istrCollection.CreateFromFile(empty_path);
    TEST_REQUIRE(myData != NULL);
    TEST_REQUIRE(istrCollection.Size(myData) == 0);

    Map = iVector.Create(sizeof(struct WordOffsetPair), 10);
    TEST_REQUIRE(Map != NULL);
    TEST_REQUIRE(iVector.Size(Map) == 0);

    it = iVector.NewIterator(Map);
    TEST_REQUIRE(it != NULL);
    TEST_REQUIRE(it->GetFirst(it) == NULL);
    iVector.DeleteIterator(it);
    it = NULL;

    iVector.Finalize(Map);
    Map = NULL;
    istrCollection.Finalize(myData);
    myData = NULL;
    remove(empty_path);

    /* NULL handling in GetPositions */
    TEST_REQUIRE(GetPositions(NULL) == NULL);

    return 0;

cleanup:
    if (itPositions != NULL)
        iValArrayInt.DeleteIterator(itPositions);
    if (it != NULL)
        iVector.DeleteIterator(it);
    if (Map != NULL) {
        size_t map_sz = iVector.Size(Map);
        for (i = 0; i < map_sz; ++i) {
            struct WordOffsetPair *p = (struct WordOffsetPair *)iVector.GetElement(Map, i);
            if (p != NULL && p->positions != NULL) {
                iValArrayInt.Finalize(p->positions);
                p->positions = NULL;
            }
        }
        iVector.Finalize(Map);
    }
    if (myData != NULL)
        istrCollection.Finalize(myData);
    remove(test_path);
    remove(empty_path);
    return -1;
}

/****************************************************************************
 * Test Suite definition and export                                         *
 ****************************************************************************/

static const TestCase applications_tests[] = {
    {"§7.1 unique deduplication", test_unique},
    {"§7.2 paste line merging", test_paste},
    {"§7.3 mapcar for list via apply", test_mapcar_list},
    {"§7.3 mapcar generic for sequential containers", test_mapcar_generic},
    {"§7.3 mapcon generic pairwise combination", test_mapcon_generic},
    {"§7.4 word offsets with nested iterators", test_word_offsets},
};

static const TestSuite applications_suite = {
    "applications",
    applications_tests,
    sizeof(applications_tests) / sizeof(applications_tests[0]),
};

const TestSuite *ccl_get_test_suite(void)
{
    return &applications_suite;
}
