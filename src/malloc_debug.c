/*
  This is a thin layer based on malloc/free. It checks for
  (1) freeing a block that wasn't allocated with this utility
  (2) freeing twice a block
  (3) freeing a block that contains a memory overwrite past its limit

  Block layout:
  |----------|-----------|------------------ ... ------------------|-------|
  |Signature |   Size    |       user memory (aligned size)        | MAGIC |
  +----------+-----------+----------------------------------------+-------+
   lower addresses                                      higher addresses

  The allocation registry is deliberately separate from the user block.
  Free must be able to reject an arbitrary pointer without first reading
  from it, and must be able to identify a second free without reading freed
  storage. Registry records are ordinary libc allocations and therefore do
  not recurse through this allocator.
*/
#include "containers.h"

#define SIGNATURE ((size_t)0xdeadbeef)
#define MAGIC     ((size_t)0xbeefdead)
#define DEBUG_HEADER_WORDS 2
#define DEBUG_TRAILER_WORDS 1
#define DEBUG_OVERHEAD_WORDS \
    (DEBUG_HEADER_WORDS + DEBUG_TRAILER_WORDS)

static size_t AllocatedMemory;

typedef struct DebugAllocation DebugAllocation;
struct DebugAllocation {
    void *base;
    void *user;
    size_t total_size;
    DebugAllocation *next;
};

/* A bounded history is sufficient to identify ordinary repeated frees and,
   unlike heap-allocated tombstones, cannot itself leak at process exit. */
#define FREED_HISTORY_CAPACITY 256
static uintptr_t FreedAllocations[FREED_HISTORY_CAPACITY];
static size_t FreedAllocationCount;
static size_t FreedAllocationCursor;
static DebugAllocation *LiveAllocations;

static int checked_layout(size_t requested, size_t *aligned,
                          size_t *total)
{
    const size_t alignment = sizeof(size_t);
    const size_t overhead = DEBUG_OVERHEAD_WORDS * sizeof(size_t);

    if (requested > SIZE_MAX - (alignment - 1))
        return 0;
    *aligned = (requested + (alignment - 1)) & ~(alignment - 1);
    if (*aligned > SIZE_MAX - overhead)
        return 0;
    *total = *aligned + overhead;
    return 1;
}

static int was_freed(const void *user)
{
    uintptr_t address = (uintptr_t)user;
    size_t i;

    for (i = 0; i < FreedAllocationCount; ++i) {
        if (FreedAllocations[i] == address)
            return 1;
    }
    return 0;
}

static void forget_freed(const void *user)
{
    uintptr_t address = (uintptr_t)user;
    size_t i;

    for (i = 0; i < FreedAllocationCount; ++i) {
        if (FreedAllocations[i] == address)
            FreedAllocations[i] = 0;
    }
}

static void remember_freed(const void *user)
{
    uintptr_t address = (uintptr_t)user;

    if (FreedAllocationCount < FREED_HISTORY_CAPACITY) {
        FreedAllocations[FreedAllocationCount++] = address;
    } else {
        FreedAllocations[FreedAllocationCursor] = address;
        FreedAllocationCursor =
            (FreedAllocationCursor + 1) % FREED_HISTORY_CAPACITY;
    }
}

static DebugAllocation *find_live(void *user, DebugAllocation **previous)
{
    DebugAllocation *record = LiveAllocations;
    DebugAllocation *prior = NULL;

    while (record != NULL) {
        if (record->user == user) {
            if (previous != NULL)
                *previous = prior;
            return record;
        }
        prior = record;
        record = record->next;
    }
    if (previous != NULL)
        *previous = NULL;
    return NULL;
}

static int allocation_is_intact(const DebugAllocation *record)
{
    size_t *header = (size_t *)record->base;
    size_t *footer;

    if (header[0] != SIGNATURE || header[1] != record->total_size)
        return CONTAINER_ERROR_BADPOINTER;
    footer = (size_t *)((char *)record->base +
                        record->total_size - sizeof(size_t));
    if (*footer != MAGIC)
        return CONTAINER_ERROR_BUFFEROVERFLOW;
    return 0;
}

static void *Malloc(size_t size)
{
    char *base;
    char *user;
    size_t aligned;
    size_t total;
    size_t *header;
    size_t *footer;
    DebugAllocation *record;

    if (!checked_layout(size, &aligned, &total))
        return NULL;
    if (AllocatedMemory > SIZE_MAX - total)
        return NULL;

    base = (char *)malloc(total);
    if (base == NULL)
        return NULL;
    record = (DebugAllocation *)malloc(sizeof(*record));
    if (record == NULL) {
        free(base);
        return NULL;
    }

    user = base + DEBUG_HEADER_WORDS * sizeof(size_t);
    header = (size_t *)base;
    header[0] = SIGNATURE;
    header[1] = total;
    memset(user, 0, aligned);
    footer = (size_t *)(base + total - sizeof(size_t));
    *footer = MAGIC;

    record->base = base;
    record->user = user;
    record->total_size = total;
    record->next = LiveAllocations;
    LiveAllocations = record;
    forget_freed(user);
    AllocatedMemory += total;
    return user;
}

static void Free(void *pp)
{
    DebugAllocation *record;
    DebugAllocation *previous;
    int integrity;

    if (pp == NULL)
        return;

    /* Registry lookup is the ownership check. Do not inspect pp before it
       succeeds: pp may point to foreign memory or to an already freed block. */
    record = find_live(pp, &previous);
    if (record == NULL) {
        if (was_freed(pp)) {
            iError.RaiseError("Free", CONTAINER_ERROR_BADPOINTER);
            return;
        }
        iError.RaiseError("Free", CONTAINER_ERROR_BADPOINTER);
        return;
    }

    integrity = allocation_is_intact(record);
    if (integrity != 0) {
        iError.RaiseError("Free", integrity);
        return;
    }

    if (previous != NULL)
        previous->next = record->next;
    else
        LiveAllocations = record->next;
    remember_freed(record->user);
    if (AllocatedMemory >= record->total_size)
        AllocatedMemory -= record->total_size;
    else
        AllocatedMemory = 0;
    memset(record->base, 66, record->total_size);
    free(record->base);
    free(record);
}

static void *Realloc(void *ptr, size_t newsize)
{
    DebugAllocation *record;
    size_t aligned_newsize;
    size_t oldsize;
    size_t copy_size;
    size_t total_newsize;
    void *result;
    int integrity;

    if (ptr == NULL)
        return Malloc(newsize);

    record = find_live(ptr, NULL);
    if (record == NULL) {
        iError.RaiseError("Realloc", CONTAINER_ERROR_BADPOINTER);
        return NULL;
    }
    integrity = allocation_is_intact(record);
    if (integrity != 0) {
        iError.RaiseError("Realloc", integrity);
        return NULL;
    }
    if (!checked_layout(newsize, &aligned_newsize, &total_newsize))
        return NULL;

    oldsize = record->total_size -
              DEBUG_OVERHEAD_WORDS * sizeof(size_t);
    if (oldsize == aligned_newsize)
        return ptr;

    result = Malloc(newsize);
    if (result != NULL) {
        copy_size = oldsize < aligned_newsize ? oldsize : aligned_newsize;
        memcpy(result, ptr, copy_size);
        Free(ptr);
    }
    return result;
}

static void *Calloc(size_t n, size_t siz)
{
    size_t total_size;

    if (siz != 0 && n > SIZE_MAX / siz)
        return NULL;
    total_size = n * siz;
    return Malloc(total_size);
}

ContainerAllocator iDebugMalloc = {
    Malloc,
    Free,
    Realloc,
    Calloc,
};
