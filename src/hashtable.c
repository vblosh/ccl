/*
 Some algorithms for this code are from the Apache Runtime Library.
 */
#include "containers.h"
#include "ccl_internal.h"

#include <stddef.h>
#include <stdint.h>

#define HASH_ENTRY_ALIGNMENT 64u

static const guid HashTableGuid = {0x3a3d3aab, 0xb14a, 0x4249,
{0x98,0x2,0xbd,0xdc,0xa5,0x62,0x59,0x75}
};


#define INITIAL_MAX 15 /* tunable == 2^n - 1 */
static unsigned int DefaultHashFunction(const char *char_key, size_t *klen);
static HashTable * Merge(Pool *p, const HashTable *overlay, const HashTable *base,
                void * (*merger)(Pool *p, const void *key, size_t klen,
                                const void *h1_val, const void *h2_val,
                                const void *data),
                const void *data);
typedef int (ApplyCallback)(void *rec, const void *key,size_t klen,const void *value);
static HashIndex *first(HashIndex *hi);
static HashIndex *next(HashIndex *hi);
/*
 * Hash creation functions.
 */
static int NullPtrError(const char *fnName)
{
    char buf[256];
    snprintf(buf,sizeof(buf),"iHashTable.%s",fnName);
    iError.RaiseError(buf,CONTAINER_ERROR_BADARG);
    return CONTAINER_ERROR_BADARG;
}

static int entry_size(size_t element_size, size_t *result)
{
    const size_t prefix = offsetof(HashEntry, val);

    if (element_size > SIZE_MAX - prefix)
        return 0;
    *result = prefix + element_size;
    return 1;
}

static int entry_stride(size_t element_size, size_t *result)
{
    size_t size;
    const size_t alignment = HASH_ENTRY_ALIGNMENT;

    if (!entry_size(element_size, &size) ||
        size > SIZE_MAX - (alignment - 1))
        return 0;
    *result = (size + alignment - 1) & ~(alignment - 1);
    return 1;
}

static HashEntry *aligned_entry(void *raw)
{
    uintptr_t address = (uintptr_t)raw + offsetof(HashEntry, val) +
                        HASH_ENTRY_ALIGNMENT - 1u;
    address &= ~(uintptr_t)(HASH_ENTRY_ALIGNMENT - 1u);
    return (HashEntry *)(address - offsetof(HashEntry, val));
}

static int table_error(HashTable *ht, const char *name, int code)
{
    ErrorFunction fn = (ht != NULL && ht->RaiseError != NULL)
        ? ht->RaiseError : iError.RaiseError;
    if (fn != NULL)
        fn(name, code);
    return code;
}

static int readonly_error(HashTable *ht, const char *name)
{
    return table_error(ht, name, CONTAINER_ERROR_READONLY);
}


static HashEntry **alloc_array(HashTable *ht, size_t max)
{
    if (ht == NULL || ht->pool == NULL || max == UINT_MAX)
        return NULL;
    return iPool.Calloc(ht->pool, max + 1, sizeof(*ht->array));
}

/* Decode ULE128 string */

static int decode_ule128(FILE *stream, size_t *val)
{
    size_t i = 0;
    const size_t bits = sizeof(size_t) * CHAR_BIT;
    const size_t groups = (bits + 6) / 7;

    if (stream == NULL || val == NULL)
        return -1;
    *val = 0;
    for (;;) {
        int c = fgetc(stream);
        size_t shift;
        unsigned char payload;

        if (c == EOF || i >= groups)
            return -1;
        payload = (unsigned char)c & 0x7f;
        shift = i * 7;
        if (shift >= bits || (shift == bits - (bits % 7 ? bits % 7 : 7) &&
                              payload >= (unsigned char)(1u << (bits % 7 ? bits % 7 : 7))))
            return -1;
        *val |= ((size_t)payload) << shift;
        i++;
        if (!(c & 0x80))
            return (int)i;
    }
}

static int encode_ule128(FILE *stream,size_t val)
{
    int i=0;

    if (val == 0) {
        if (fputc(0, stream) == EOF)
            return EOF;
        i=1;
    }
    else while (val) {
        size_t c = val&0x7f;
        val >>= 7;
        if (val)
            c |= 0x80;
        if (fputc((int)c,stream) == EOF)
            return EOF;
        i++;
    }
    return i;
}

static HashTable * Create(size_t ElementSize)
{
    HashTable *ht;
    Pool *pool;
    size_t storage_size;

    if (!entry_size(ElementSize, &storage_size)) {
        iError.RaiseError("iHashTable.Create", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    pool = iPool.Create(NULL);
    if (pool == NULL) {
        iError.RaiseError("iHashTable.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    ht = iPool.Calloc(pool, 1, sizeof(HashTable));
    if (ht == NULL) {
        iPool.Finalize(pool);
        iError.RaiseError("iHashTable.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    ht->pool = pool;
    ht->max = INITIAL_MAX;
    ht->array = alloc_array(ht, ht->max);
    if (ht->array == NULL) {
        iPool.Finalize(pool);
        iError.RaiseError("iHashTable.Create", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    ht->Hash = DefaultHashFunction;
    ht->VTable = &iHashTable;
    ht->ElementSize = ElementSize;
    ht->RaiseError = iError.RaiseError;
    ht->Allocator = CurrentAllocator;
    ht->iterator.ht = ht;
    return ht;
}

static HashTable *Init(HashTable *ht,size_t ElementSize)
{
    iError.RaiseError("iHashTable.Init",CONTAINER_ERROR_NOTIMPLEMENTED);
    return NULL;
}
static GeneralHashFunction SetHashFunction(HashTable *ht, GeneralHashFunction Hash)
{
    GeneralHashFunction old;
    if (ht == NULL)
        return NULL;
    old = ht->Hash;
    if (Hash)
        ht->Hash = Hash;
    return old;
}

/*
 * Resizing a hash table
 */

static int resize_table(HashTable *ht,size_t newsize,int update_timestamp)
{
    HashEntry **new_array;
    unsigned int new_max;
    unsigned int i;
    HashEntry *entry;

    if (ht == NULL || ht->array == NULL)
        return table_error(ht, "iHashTable.Resize", CONTAINER_ERROR_BADARG);
    if (ht->Flags & CONTAINER_READONLY)
        return readonly_error(ht, "iHashTable.Resize");

    if (newsize == 0) {
        if (ht->max > (UINT_MAX - 1u) / 2u)
            return table_error(ht, "iHashTable.Resize", CONTAINER_ERROR_BADARG);
        new_max = ht->max * 2u + 1u;
    } else {
        size_t buckets;
        size_t power = 1;

        /* Accept either the historical mask (2^n-1) or a bucket count. */
        if (newsize != SIZE_MAX && (newsize & (newsize + 1)) == 0) {
            buckets = newsize + 1;
        } else {
            buckets = newsize;
        }
        if (buckets == 0)
            buckets = 1;
        while (power < buckets) {
            if (power > ((size_t)UINT_MAX + 1u) / 2u)
                return table_error(ht, "iHashTable.Resize", CONTAINER_ERROR_BADARG);
            power <<= 1;
        }
        if (power - 1 > UINT_MAX)
            return table_error(ht, "iHashTable.Resize", CONTAINER_ERROR_BADARG);
        new_max = (unsigned int)(power - 1);
    }

    if (new_max == ht->max)
        return 1;
    new_array = alloc_array(ht, new_max);
    if (new_array == NULL)
        return table_error(ht, "iHashTable.Resize", CONTAINER_ERROR_NOMEMORY);

    for (i = 0; i <= ht->max; ++i) {
        entry = ht->array[i];
        while (entry != NULL) {
            HashEntry *next_entry = entry->next;
            unsigned int bucket = entry->hash & new_max;
            entry->next = new_array[bucket];
            new_array[bucket] = entry;
            entry = next_entry;
        }
    }
    ht->array = new_array;
    ht->max = new_max;
    if (update_timestamp)
        ht->timestamp++;
    return 1;
}

static int Resize(HashTable *ht,size_t newsize)
{
    return resize_table(ht, newsize, 1);
}

static unsigned int DefaultHashFunction(const char *char_key, size_t *klen)
{
    unsigned int hash = 0;
    const unsigned char *key = (const unsigned char *)char_key;
    const unsigned char *p;
    size_t i;

    /*
    * This is the popular `times 33' hash algorithm which is used by
    * perl and also appears in Berkeley DB. This is one of the best
    * known hash functions for strings because it is both computed
    * very fast and distributes very well.
    *
    * The originator may be Dan Bernstein but the code in Berkeley DB
    * cites Chris Torek as the source. The best citation I have found
    * is "Chris Torek, Hash function for text in C, Usenet message
    * <27038@mimsy.umd.edu> in comp.lang.c , October, 1990." in Rich
    * Salz's USENIX 1992 paper about INN which can be found at
    * <http://citeseer.nj.nec.com/salz92internetnews.html>.
    *
    * The magic of number 33, i.e. why it works better than many other
    * constants, prime or not, has never been adequately explained by
    * anyone. So I try an explanation: if one experimentally tests all
    * multipliers between 1 and 256 (as I did while writing a low-level
    * data structure library some time ago) one detects that even
    * numbers are not useable at all. The remaining 128 odd numbers
    * (except for the number 1) work more or less all equally well.
    * They all distribute in an acceptable way and this way fill a hash
    * table with an average percent of approx. 86%.
    *
    * If one compares the chi^2 values of the variants (see
    * Bob Jenkins ``Hashing Frequently Asked Questions'' at
    * http://burtleburtle.net/bob/hash/hashfaq.html for a description
    * of chi^2), the number 33 not even has the best value. But the
    * number 33 and a few other equally good numbers like 17, 31, 63,
    * 127 and 129 have nevertheless a great advantage to the remaining
    * numbers in the large set of possible multipliers: their multiply
    * operation can be replaced by a faster operation based on just one
    * shift plus either a single addition or subtraction operation. And
    * because a hash function has to both distribute good _and_ has to
    * be very fast to compute, those few numbers should be preferred.
    *
    *                  -- Ralf S. Engelschall <rse@engelschall.com>
    */

    if (*klen == (size_t)-1) {
        for (p = key; *p; p++) {
            hash = hash * 33 + *p;
        }
        *klen = p - key;
    }
    else {
        for (p = key, i = *klen; i; i--, p++) {
            hash = hash * 33 + *p;
        }
    }

    return hash;
}


/*
 * This is where we keep the details of the hash function and control
 * the maximum collision rate.
 *
 * If val is non-NULL it creates and initializes a new hash entry if
 * there isn't already one there; it returns an updatable pointer so
 * that hash entries can be removed.
 */

static HashEntry **find_entry(HashTable *ht,const void *key,size_t klen,const void *val)
{
    HashEntry **hashTablePointer, *he;
    unsigned int hash;
    size_t size;

    if (ht == NULL || ht->array == NULL || key == NULL || ht->Hash == NULL)
        return NULL;
    hash = ht->Hash(key, &klen);

    /* scan linked list */
    for (hashTablePointer = &ht->array[hash & ht->max], he = *hashTablePointer;
        he; hashTablePointer = &he->next, he = *hashTablePointer) {
        if (he->hash == hash
            && he->klen == klen
            && memcmp(he->key, key, klen) == 0)
            break;
    }
    if (he || !val)
        return hashTablePointer;

    /* add a new entry for non-NULL values */
    if ((he = ht->free) != NULL)
        ht->free = he->next;
    else {
        if (!entry_size(ht->ElementSize, &size) ||
            size > SIZE_MAX - (HASH_ENTRY_ALIGNMENT - 1u))
            return NULL;
        {
            void *raw = iPool.Alloc(ht->pool,
                                    size + HASH_ENTRY_ALIGNMENT - 1u);
            he = raw != NULL ? aligned_entry(raw) : NULL;
        }
    }
    if (he == NULL) {
        table_error(ht, "iHashTable.Add", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    he->next = NULL;
    he->hash = hash;
    he->key  = key;
    he->klen = klen;
    memcpy(he->val,val,ht->ElementSize);
    *hashTablePointer = he;
    ht->count++;
    return hashTablePointer;
}

static int Replace(HashTable *ht,const void *key,size_t klen,const void *val)
{
    HashEntry **hep, *he;
    unsigned int hash;

    if (ht == NULL ||val == NULL || key == NULL || klen == 0) {
        return table_error(ht, "iHashTable.Replace",CONTAINER_ERROR_BADARG);
    }
    if (ht->Flags & CONTAINER_READONLY) {
        return readonly_error(ht, "iHashTable.Replace");
    }
    if (ht->array == NULL || ht->Hash == NULL) {
        return table_error(ht, "iHashTable.Replace",CONTAINER_ERROR_BADARG);
    }
    hash = ht->Hash(key, &klen);

    /* scan linked list */
    for (hep = &ht->array[hash & ht->max], he = *hep;
        he; hep = &he->next, he = *hep) {
        if (he->hash == hash
            && he->klen == klen
            && memcmp(he->key, key, klen) == 0) {
            memcpy(he->val,val,ht->ElementSize);
            ht->timestamp++;
            return 1;
        }
    }
    return 0;
}

static int Add(HashTable *ht,const void *key, size_t klen, const void *val)
{
    HashEntry **entry;
    size_t old_count;
    unsigned old_timestamp;

    if (ht == NULL || key == NULL || klen == 0 || val == NULL) {
        return table_error(ht, "iHashTable.Add",CONTAINER_ERROR_BADARG);
    }
    if (ht->Flags & CONTAINER_READONLY)
        return readonly_error(ht, "iHashTable.Add");

    old_count = ht->count;
    old_timestamp = ht->timestamp;
    entry = find_entry(ht,key,klen,val);
    if (entry == NULL)
        return CONTAINER_ERROR_NOMEMORY;
    if (old_count == ht->count)
        return 0;
    ht->timestamp++;
    if (ht->count > ht->max && resize_table(ht, 0, 0) < 0) {
        HashEntry *new_entry = *entry;
        *entry = new_entry->next;
        new_entry->next = ht->free;
        ht->free = new_entry;
        --ht->count;
        ht->timestamp = old_timestamp;
        return CONTAINER_ERROR_NOMEMORY;
    }
    return 1;
}

static void *GetElement(const HashTable *ht,const void *key, size_t klen)
{
    HashEntry **v;

    if (ht == NULL || key == NULL || klen == 0) {
        if (ht != NULL)
            table_error((HashTable *)ht, "iHashTable.GetElement", CONTAINER_ERROR_BADARG);
        else
            iError.RaiseError("iHashTable.GetElement", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    v = find_entry((HashTable *)ht,key, klen, NULL);
    if (v != NULL && *v != NULL)
        return (void *)(*v)->val;
    return NULL;
}

static int Contains(const HashTable *ht,const void *Key,size_t klen)
{
    if (ht == NULL)  {
        return NullPtrError("Contains");
    }
    if (Key == NULL || klen == 0) {
        table_error((HashTable *)ht, "Contains",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    return GetElement(ht,Key,klen) ? 1 : 0;
}


static size_t GetElementSize(const HashTable *l)
{
    if (l) {
        return l->ElementSize;
    }
    iError.NullPtrError("GetElementSize");
    return 0;
}


static HashTable *Copy( const HashTable *orig,Pool *pool)
{
    HashTable *ht;
    HashEntry *new_vals;
    size_t entry_bytes, total_entry_bytes, array_bytes, total_bytes;
    size_t base_offset;
    unsigned int i;
    size_t j = 0;

    if (orig == NULL) {
        iError.RaiseError("iHashTable.Copy",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (pool == NULL)
        pool = orig->pool;
    if (pool == NULL || !entry_stride(orig->ElementSize, &entry_bytes) ||
        orig->max == UINT_MAX ||
        (size_t)(orig->max + 1u) > SIZE_MAX / sizeof(*ht->array)) {
        iError.RaiseError("iHashTable.Copy",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    array_bytes = (size_t)(orig->max + 1u) * sizeof(*ht->array);
    if (orig->count > SIZE_MAX / entry_bytes) {
        iError.RaiseError("iHashTable.Copy",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    total_entry_bytes = (size_t)orig->count * entry_bytes;
    if (array_bytes > SIZE_MAX - sizeof(HashTable)) {
        iError.RaiseError("iHashTable.Copy",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    base_offset = sizeof(HashTable) + array_bytes;
    if (total_entry_bytes > SIZE_MAX - base_offset ||
        total_entry_bytes + base_offset >
            SIZE_MAX - (HASH_ENTRY_ALIGNMENT - 1u)) {
        iError.RaiseError("iHashTable.Copy",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    total_bytes = base_offset + total_entry_bytes + HASH_ENTRY_ALIGNMENT - 1u;
    ht = iPool.Alloc(pool, total_bytes);
    if (ht == NULL) {
        iError.RaiseError("iHashTable.Copy",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    memset(ht, 0, sizeof(*ht));
    ht->VTable = &iHashTable;
    ht->pool = pool;
    ht->array = (HashEntry **)((char *)ht + sizeof(HashTable));
    ht->iterator.ht = ht;
    ht->count = orig->count;
    ht->max = orig->max;
    ht->Hash = orig->Hash;
    ht->Flags = orig->Flags;
    ht->RaiseError = orig->RaiseError;
    ht->timestamp = 0;
    ht->ElementSize = orig->ElementSize;
    ht->Allocator = orig->Allocator ? orig->Allocator : CurrentAllocator;
    ht->DestructorFn = orig->DestructorFn;

    new_vals = aligned_entry((char *)ht + base_offset);
    for (i = 0; i <= ht->max; i++) {
        HashEntry **new_entry = &(ht->array[i]);
        HashEntry *orig_entry = orig->array[i];
        while (orig_entry) {
            HashEntry *copy = (HashEntry *)((char *)new_vals + j * entry_bytes);
            *new_entry = copy;
            copy->hash = orig_entry->hash;
            copy->key = orig_entry->key;
            copy->klen = orig_entry->klen;
            memcpy(copy->val, orig_entry->val, ht->ElementSize);
            new_entry = &copy->next;
            orig_entry = orig_entry->next;
            j++;
        }
        *new_entry = NULL;
    }
    return ht;
}

static int Remove(HashTable *ht,const void *key,size_t klen)
{
    HashEntry **hep;
    HashEntry *old;

    if (ht == NULL || key == NULL || klen == 0)
        return table_error(ht, "iHashTable.Remove", CONTAINER_ERROR_BADARG);
    if (ht->Flags & CONTAINER_READONLY)
        return readonly_error(ht, "iHashTable.Remove");
    hep = find_entry(ht,key,klen,NULL);
    if (hep == NULL || *hep == NULL)
        return 0;
    old = *hep;
    *hep = old->next;
    if (ht->DestructorFn)
        ht->DestructorFn(old->val);
    old->next = ht->free;
    ht->free = old;
    --ht->count;
    ht->timestamp++;
    return 1;
}

static int Clear(HashTable *ht)
{
    unsigned int i;

    if (ht == NULL)
        return NullPtrError("Clear");
    if (ht->Flags & CONTAINER_READONLY)
        return readonly_error(ht, "Clear");
    for (i = 0; i <= ht->max; ++i) {
        HashEntry *entry = ht->array[i];
        ht->array[i] = NULL;
        while (entry != NULL) {
            HashEntry *next_entry = entry->next;
            if (ht->DestructorFn)
                ht->DestructorFn(entry->val);
            entry->next = ht->free;
            ht->free = entry;
            if (ht->count != 0)
                --ht->count;
            ht->timestamp++;
            entry = next_entry;
        }
    }
    return 1;
}

static int Finalize(HashTable *ht)
{
    int result;

    if (ht == NULL)
        return NullPtrError("Finalize");
    result = Clear(ht);
    iPool.Finalize(ht->pool);
    return result;
}

static HashTable* Overlay(Pool *p, const HashTable *overlay, const HashTable *base)
{
    return Merge(p, overlay, base, NULL, NULL);
}

static HashTable * Merge(Pool *p, const HashTable *overlay, const HashTable *base,
                                        void * (*merger)(Pool *p,
                                                    const void *key,
                                                    size_t klen,
                                                    const void *h1_val,
                                                    const void *h2_val,
                                                    const void *data),
                                        const void *data)
{
    HashTable *res;
    HashEntry *new_vals = NULL;
    HashEntry *iter;
    HashEntry *ent;
    size_t stride, capacity, array_bytes, total_bytes;
    size_t j = 0;
    unsigned int i, k;

    if (p == NULL || overlay == NULL || base == NULL ||
        base->ElementSize != overlay->ElementSize || base->Hash == NULL) {
        iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (!entry_stride(base->ElementSize, &stride) ||
        base->max == UINT_MAX || overlay->max == UINT_MAX ||
        base->count > SIZE_MAX - overlay->count) {
        iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    capacity = (size_t)base->count + overlay->count;
    res = iPool.Alloc(p, sizeof(HashTable));
    if (res == NULL) {
        iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    memset(res, 0, sizeof(*res));
    res->VTable = &iHashTable;
    res->pool = p;
    res->Hash = base->Hash;
    res->max = (overlay->max > base->max) ? overlay->max : base->max;
    if (capacity > res->max) {
        while (capacity > res->max) {
            if (res->max > (UINT_MAX - 1u) / 2u) {
                iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_BADARG);
                return NULL;
            }
            res->max = res->max * 2u + 1u;
        }
    }
    res->ElementSize = base->ElementSize;
    res->Flags = base->Flags;
    res->RaiseError = base->RaiseError;
    res->Allocator = base->Allocator ? base->Allocator : CurrentAllocator;
    res->DestructorFn = base->DestructorFn;
    res->iterator.ht = res;
    res->array = alloc_array(res, res->max);
    if (res->array == NULL) {
        iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (capacity > 0) {
        if (capacity > SIZE_MAX / stride) {
            iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_BADARG);
            return NULL;
        }
        array_bytes = capacity * stride;
        if (array_bytes > SIZE_MAX - (HASH_ENTRY_ALIGNMENT - 1u)) {
            iError.RaiseError("iHashTable.Merge", CONTAINER_ERROR_BADARG);
            return NULL;
        }
        total_bytes = array_bytes + HASH_ENTRY_ALIGNMENT - 1u;
        {
            void *raw = iPool.Alloc(p, total_bytes);
            new_vals = raw != NULL ? aligned_entry(raw) : NULL;
        }
        if (new_vals == NULL) {
            iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_NOMEMORY);
            return NULL;
        }
    }

    for (k = 0; k <= base->max; k++) {
        for (iter = base->array[k]; iter; iter = iter->next) {
            HashEntry *copy = (HashEntry *)((char *)new_vals + j * stride);
            i = iter->hash & res->max;
            copy->klen = iter->klen;
            copy->key = iter->key;
            copy->hash = iter->hash;
            memcpy(copy->val, iter->val, res->ElementSize);
            copy->next = res->array[i];
            res->array[i] = copy;
            res->count++;
            j++;
        }
    }
    for (k = 0; k <= overlay->max; k++) {
        for (iter = overlay->array[k]; iter; iter = iter->next) {
            size_t key_len = iter->klen;
            unsigned int hash = res->Hash(iter->key, &key_len);
            i = hash & res->max;
            for (ent = res->array[i]; ent; ent = ent->next) {
                if (ent->hash == hash && ent->klen == key_len &&
                    memcmp(ent->key, iter->key, key_len) == 0)
                    break;
            }
            if (ent != NULL) {
                void *merged = merger ? merger(p, iter->key, key_len,
                                                iter->val, ent->val, data)
                                      : (void *)iter->val;
                if (merged == NULL) {
                    iError.RaiseError("iHashTable.Merge",CONTAINER_ERROR_BADARG);
                    return NULL;
                }
                memcpy(ent->val, merged, res->ElementSize);
            } else {
                HashEntry *copy = (HashEntry *)((char *)new_vals + j * stride);
                copy->klen = key_len;
                copy->key = iter->key;
                copy->hash = hash;
                memcpy(copy->val, iter->val, res->ElementSize);
                copy->next = res->array[i];
                res->array[i] = copy;
                res->count++;
                j++;
            }
        }
    }
    return res;
}

/* This is basically the following...
 * for every element in hash table {
 *    comp elemeny.key, element.value
 * }
 *
 * Like with apr_table_do, the comp callback is called for each and every
 * element of the hash table.
 */
static int Search(HashTable *ht,ApplyCallback *comp, void *rec)
{
    HashIndex  hix;
    HashIndex *hi;
    int rv, dorv  = 1;

    if (ht == NULL || comp == NULL)
        return table_error(ht, "iHashTable.Search", CONTAINER_ERROR_BADARG);

    hix.ht    = (HashTable *)ht;
    hix.index = 0;
    hix.This  = NULL;
    hix.next  = NULL;

    hi = next(&hix);
    if (hi) {
        /* Scan the entire table */
        do {
            rv = (*comp)(rec, hi->This->key, hi->This->klen, hi->This->val);
        } while (rv && (hi = next(hi)));

        if (rv == 0) {
            dorv = 0;
        }
    }
    return dorv;
}
static int Apply(HashTable *ht,int (*Applyfn)(void *Key,size_t klen,void *data,void *arg), void *arg)
{
    HashIndex  hix;
    HashIndex *hi;
    int rv, dorv  = 1;

    if (ht == NULL || Applyfn == NULL)
        return table_error(ht, "iHashTable.Apply", CONTAINER_ERROR_BADARG);

    hix.ht    = (HashTable *)ht;
    hix.index = 0;
    hix.This  = NULL;
    hix.next  = NULL;

    hi = next(&hix);
    if (hi) {
        /* Scan the entire table */
        do {
            rv = (*Applyfn)((void *)hi->This->key, hi->This->klen,(void *) hi->This->val,arg);
        } while (rv && (hi = next(hi)));

        if (rv == 0) {
            dorv = 0;
        }
    }
    return dorv;
}
static ErrorFunction SetErrorFunction(HashTable *ht,ErrorFunction fn)
{
    ErrorFunction old;
    if (ht == NULL) return iError.RaiseError;
    old = ht->RaiseError;
    ht->RaiseError = (fn) ? fn : iError.EmptyErrorFunction;
    return old;
}

static size_t Size(const HashTable *AL)
{
    if (AL == NULL) {
        iError.NullPtrError("Size");
        return 0;
    }
    return AL->count;
}
static size_t Sizeof(const HashTable *HT)
{
    size_t stride;
    if (HT == NULL)
        return sizeof(HashTable);
    if (!entry_stride(HT->ElementSize, &stride) || HT->count > SIZE_MAX / stride)
        return sizeof(HashTable);
    return sizeof(HashTable) + HT->count * stride;
}
static unsigned GetFlags(const HashTable *AL)
{
    if (AL == NULL) {
        iError.NullPtrError("GetFlags");
        return 0;
    }
    return AL->Flags;
}
static unsigned SetFlags(HashTable *AL,unsigned newval)
{
    unsigned oldval;
    if (AL == NULL) {
        iError.NullPtrError("SetFlags");
        return 0;
    }
    oldval = AL->Flags;
    AL->Flags = newval;
    return oldval;
}
static int DefaultSaveFunction(const void *element,void *arg, FILE *Outfile)
{
    size_t *pLength = (size_t *)arg;
    size_t len;

    if (element == NULL || pLength == NULL || Outfile == NULL)
        return 0;
    len = *pLength;
    return len == fwrite(element,1,len,Outfile);
}

static int DefaultLoadFunction(void *element,void *arg, FILE *Infile)
{
    size_t len;

    if (arg == NULL || Infile == NULL)
        return 0;
    len = *(size_t *)arg;
    if (len == 0)
        return 1;
    if (element == NULL)
        return 0;
    return len == fread(element,1,len,Infile);
}

static int Save(const HashTable *HT,FILE *stream, SaveFunction saveFn,void *arg)
{
    HashIndex  hix;
    HashIndex *hi;
    int rv;
    int retval=1;
    size_t elemsiz;

    if (HT == NULL || stream == NULL) {
        return NullPtrError("Save");
    }
    if (saveFn == NULL) {
        saveFn = DefaultSaveFunction;
        elemsiz = HT->ElementSize;
        arg = &elemsiz;
    }
    if (fwrite(&HashTableGuid,sizeof(guid),1,stream) != 1)
        return EOF;
    if (fwrite(HT,1,sizeof(HashTable),stream) != sizeof(HashTable))
        return EOF;

    hix.ht    = (HashTable *)HT;
    hix.index = 0;
    hix.This  = NULL;
    hix.next  = NULL;

    hi = next(&hix);
    if (hi) {
        /* Scan the entire table */
        do {
            rv = encode_ule128(stream, hi->This->klen);
            if (rv > 0)
                rv = (hi->This->klen == fwrite(hi->This->key,1,
                                                hi->This->klen,stream));
            if (rv > 0)
                rv = (int)saveFn(hi->This->val,arg,stream);
        } while (rv > 0 && (hi = next(hi)));

        if (rv <= 0) {
            retval = (int)rv;
        }
    }
    return retval;
}

static HashTable *Load(FILE *stream, ReadFunction readFn,void *arg)
{
    size_t i,len,keybuflen;
    HashTable *result=NULL,HT;
    char *keybuf=NULL,*valbuf=NULL;
    guid Guid;

    if (stream == NULL) {
        iError.RaiseError("iHashTable.Load", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (readFn == NULL) {
        readFn = DefaultLoadFunction;
        arg = &HT.ElementSize;
    }

    if (fread(&Guid,sizeof(guid),1,stream) != 1) {
        iError.RaiseError("iHashTable.Load",CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (memcmp(&Guid,&HashTableGuid,sizeof(guid))) {
        iError.RaiseError("iHashTable.Load",CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    if (fread(&HT,1,sizeof(HashTable),stream) != sizeof(HashTable)) {
        iError.RaiseError("HashTable.Load",CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    {
        size_t ignored;
        if (!entry_size(HT.ElementSize, &ignored)) {
            iError.RaiseError("iHashTable.Load", CONTAINER_ERROR_BADARG);
            return NULL;
        }
    }
    result = iHashTable.Create(HT.ElementSize);
    if (result == NULL)
        return NULL;
    keybuflen = 1;
    keybuf = malloc(keybuflen);
    valbuf = malloc(HT.ElementSize != 0 ? HT.ElementSize : 1);
    if (keybuf == NULL || valbuf == NULL) {
        iError.RaiseError("iHashTable.Load", CONTAINER_ERROR_NOMEMORY);
        goto fail;
    }
    for (i=0; i< HT.count; i++) {
        if (decode_ule128(stream, &len) <= 0) {
            iError.RaiseError("iHashTable.Load",CONTAINER_ERROR_FILE_READ);
            goto fail;
        }
        if (keybuflen < len) {
            void *tmp = realloc(keybuf,len);
            if (tmp == NULL) {
                iError.RaiseError("iHashTable.Load",CONTAINER_ERROR_NOMEMORY);
                goto fail;
            }
            keybuf = tmp;
            keybuflen = len;
        }
        if (len == 0 || fread(keybuf,1,len,stream) != len) {
            iError.RaiseError("iHashTable.Load",CONTAINER_ERROR_FILE_READ);
            goto fail;
        }
        if (readFn(valbuf,arg,stream) <= 0) {
            iError.RaiseError("iHashTable.Load",CONTAINER_ERROR_FILE_READ);
            goto fail;
        }
        {
            void *durable_key = iPool.Alloc(result->pool, len);
            if (durable_key == NULL) {
                iError.RaiseError("iHashTable.Load", CONTAINER_ERROR_NOMEMORY);
                goto fail;
            }
            memcpy(durable_key, keybuf, len);
            if (iHashTable.Add(result,durable_key,len,valbuf) < 0)
                goto fail;
        }
    }
    result->Flags = HT.Flags;
    if (keybuf) free(keybuf);
    if (valbuf) free(valbuf);
    return result;

fail:
    if (keybuf) free(keybuf);
    if (valbuf) free(valbuf);
    if (result != NULL)
        iPool.Finalize(result->pool);
    return NULL;
}

/* ------------------------------------------------------------------------------ */
/*                                Iterators                                       */
/* ------------------------------------------------------------------------------ */

static HashIndex * next(HashIndex *hi)
{
    if (hi == NULL || hi->ht == NULL || hi->ht->array == NULL)
        return NULL;
    hi->This = hi->next;
    while (!hi->This) {
        if (hi->index > hi->ht->max)
            return NULL;

        hi->This = hi->ht->array[hi->index++];
    }
    hi->next = hi->This->next;
    return hi;
}

static void *GetNext(Iterator *it)
{
    struct HashTableIterator *d;
    HashIndex *hi;

    if (it == NULL)
        return NULL;
    d = (struct HashTableIterator *)it;
    if (d->ht == NULL)
        return NULL;
    if (d->timestamp != d->ht->timestamp) {
        table_error(d->ht, "iHashTable.GetNext", CONTAINER_ERROR_OBJECT_CHANGED);
        d->Current = NULL;
        return NULL;
    }
    hi = next(&d->hi);
    d->Current = hi;
    if (hi) {
        return (void *)hi->This->val;
    }
    return NULL;
}

static HashIndex *first(HashIndex *hi)
{
    hi->index = 0;
    hi->This = NULL;
    hi->next = NULL;
    return next(hi);
}

static void *GetFirst(Iterator *it)
{
    HashIndex *hi;
    struct HashTableIterator *d;

    if (it == NULL)
        return NULL;
    d = (struct HashTableIterator *)it;
    if (d->ht == NULL)
        return NULL;
    if (d->timestamp != d->ht->timestamp) {
        table_error(d->ht, "iHashTable.GetFirst", CONTAINER_ERROR_OBJECT_CHANGED);
        d->Current = NULL;
        return NULL;
    }

    hi = first(&d->hi);
    d->Current = hi;
    if (hi == NULL)
        return NULL;
    return hi->This->val;
}

static void *GetCurrent(Iterator *it)
{
    struct HashTableIterator *d;

    if (it == NULL)
        return NULL;
    d = (struct HashTableIterator *)it;
    if (d->ht == NULL || d->timestamp != d->ht->timestamp ||
        d->Current == NULL || d->Current->This == NULL) {
        if (d->ht != NULL && d->timestamp != d->ht->timestamp)
            table_error(d->ht, "iHashTable.GetCurrent", CONTAINER_ERROR_OBJECT_CHANGED);
        return NULL;
    }
    return d->Current->This->val;
}

static int ReplaceWithIterator(Iterator *it, void *data,int direction)
{
    struct HashTableIterator *li;
    int result;
    HashIndex current;

    if (it == NULL) {
        return NullPtrError("Replace");
    }
    li = (struct HashTableIterator *)it;
    (void)direction;
    if (li->ht == NULL || li->Current == NULL || li->Current->This == NULL)
        return 0;
    if (li->timestamp != li->ht->timestamp) {
        table_error(li->ht, "Replace",CONTAINER_ERROR_OBJECT_CHANGED);
        return CONTAINER_ERROR_OBJECT_CHANGED;
    }
    if (li->ht->Flags & CONTAINER_READONLY) {
        return readonly_error(li->ht, "Replace");
    }
    if (li->ht->count == 0)
        return 0;
    current = *li->Current;
    GetNext(it);
    if (data == NULL)
        result = Remove(li->ht, current.This->key,current.This->klen);
    else {
        memcpy(current.This->val,data,li->ht->ElementSize);
        li->ht->timestamp++;
        result = 1;
    }
    if (result >= 0) {
        li->timestamp = li->ht->timestamp;
    }
    return result;
}

static Iterator *NewIterator(HashTable *ht)
{
    struct HashTableIterator *result;

    if (ht == NULL || ht->Allocator == NULL || ht->Allocator->malloc == NULL) {
        table_error(ht, "InitIterator",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    result = ht->Allocator->malloc(sizeof(struct HashTableIterator));
    if (result == NULL)
        return NULL;
    result->it.GetNext = GetNext;
    result->it.GetPrevious = GetNext;
    result->it.GetFirst = GetFirst;
    result->it.GetCurrent = GetCurrent;
    result->it.Replace = ReplaceWithIterator;
    result->Magic = HASHTABLE_MAGIC_NUMBER;
    result->timestamp = ht->timestamp;
    result->Current = NULL;
    result->ht = ht;
    result->hi.ht = ht;
    result->hi.index = 0;
    result->hi.This = NULL;
    result->hi.next = NULL;
    return &result->it;
}

static int InitIterator(HashTable *ht,void *buf)
{
    struct HashTableIterator *result;
    if (ht == NULL || buf == NULL) {
        table_error(ht, "InitIterator",CONTAINER_ERROR_BADARG);
        return CONTAINER_ERROR_BADARG;
    }
    result = buf;
    result->it.GetNext = GetNext;
    result->it.GetPrevious = GetNext;
    result->it.GetFirst = GetFirst;
    result->it.GetCurrent = GetCurrent;
    result->it.Replace = ReplaceWithIterator;
    result->Magic = 0;
    result->timestamp = ht->timestamp;
    result->Current = NULL;
    result->ht = ht;
    result->hi.ht = ht;
    result->hi.index = 0;
    result->hi.This = NULL;
    result->hi.next = NULL;
    return 1;
}


static size_t SizeofIterator(const HashTable *ht)
{
	return sizeof(struct HashTableIterator);
}

static int DeleteIterator(Iterator *it)
{
    struct HashTableIterator *d;
    HashTable *ht;

    if (it == NULL)
        return CONTAINER_ERROR_BADARG;
    d = (struct HashTableIterator *)it;
    ht = d->ht;
    if (d->Magic == HASHTABLE_MAGIC_NUMBER && ht != NULL &&
        ht->Allocator != NULL && ht->Allocator->free != NULL)
        ht->Allocator->free(d);
    return 1;
}

static DestructorFunction SetDestructor(HashTable *cb,DestructorFunction fn)
{
    DestructorFunction oldfn;
    if (cb == NULL)
        return NULL;
    oldfn = cb->DestructorFn;
    if (fn)
        cb->DestructorFn = fn;
    return oldfn;
}


HashTableInterface iHashTable = {
Size,
GetFlags,
SetFlags,
Clear,
Contains,
Create,
Init,
Sizeof,
GetElementSize,
Add,
GetElement,
Search,
Remove,
Finalize,
Apply,
SetErrorFunction,
Resize,
Replace,
Copy,
SetHashFunction,
Overlay,
Merge,
NewIterator,
InitIterator,
DeleteIterator,
SizeofIterator,
Save,
Load,
SetDestructor,
};
