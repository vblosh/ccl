#include <limits.h>
#include <stddef.h>
#include "containers.h"
#include "ccl_internal.h"
#include "assert.h"
#undef ITERATOR
#undef INTERFACE
#undef DATA_TYPE

#define HASHFUNCTION WHashFunction
#define ITERATOR WDictionaryIterator
#define CHARTYPE wchar_t
#define DATALIST WDataList
#define DATA_TYPE WDictionary
#define INTERFACE WDictionaryInterface
#define EXTERNAL_NAME iWDictionary
#define STRCPY wcscpy
#define STRCMP wcscmp
#define STRLEN wcslen
#define iSTRCOLLECTION iWstrCollection
#define STRCOLLECTION WstrCollection
#define DICT_ERROR_PREFIX "iWDictionary"
#define DICT_MAGIC_NUMBER WDICTIONARY_MAGIC_NUMBER
#define HASHCHAR(ch) (scatter[(unsigned)(ch) & 255u] ^ \
                      ((size_t)(unsigned long)(ch) * (size_t)16777619u))

#include "dictionarygen.c"
