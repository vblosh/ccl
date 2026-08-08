/*
The algorithms of this code have been adapted from the Apache runtime library.
*/
#include <stdint.h>
#include "containers.h"
#include <stdlib.h>
#include <string.h>
#define MAX_INDEX	20
#define ALIGN(size, boundary) (((size) + ((boundary) - 1)) & ~((boundary) - 1))
#define ALIGN_DEFAULT(size) ALIGN(size, 8)

#ifdef TEST
/** The fundamental pool type */
typedef struct Pool Pool;
#endif
#define POOL_DEBUG_VERSION 1
/*
* Note The max_free_index and current_free_index fields are not really
* indices, but quantities of BOUNDARY_SIZE big memory blocks.
*/

struct MemoryNode_t {
    struct MemoryNode_t *next;            /**< next memnode */
    struct MemoryNode_t **ref;            /**< reference to self */
    uint32_t   index;           /**< size */
    uint32_t   free_index;      /**< how much free */
    char          *first_avail;     /**< pointer to first free memory */
    char          *endp;            /**< pointer to end of free memory */
};

typedef struct MemoryNode_t MemoryNode_t;
typedef struct {
    /** largest used index into free[], always < MAX_INDEX */
    uint32_t        max_index;
    /** Total size (in BOUNDARY_SIZE multiples) of unused memory before
     * blocks are given back. @see SetMaxFree().
     * @note Initialized to 0,
     * which means to never give back blocks.
     */
    uint32_t        max_free_index;
    /**
     * Memory size (in BOUNDARY_SIZE multiples) that currently must be freed
     * before blocks are given back. Range: 0..max_free_index
     */
    uint32_t        current_free_index;
    Pool         *owner;
    /**
     * Lists of free nodes. Slot 0 is used for oversized nodes,
     * and the slots 1..MAX_INDEX-1 contain nodes of sizes
     * (i+1) * BOUNDARY_SIZE. Example for BOUNDARY_INDEX == 12:
     * slot  0: nodes larger than 81920
     * slot  1: size  8192
     * slot  2: size 12288
     * ...
     * slot 19: size 81920
     */
    MemoryNode_t      *free[MAX_INDEX];
} Allocator;


/** Debug version of newPool. */
static Pool *newPool_debug( const char *file_line);
/**
 * Debug version of PoolClear.
 * @param p See: PoolClear.
 * @param file_line Where the function is called from.
 *        This is usually __FILE_LINE__.
 * @remark Only available when POOL_DEBUG_VERSION is defined.
 *         Call this directly if you have you PoolClear
 *         calls in a wrapper function and wish to override
 *         the file_line argument to reflect the caller of
 *         your wrapper function.  If you do not have
 *         PoolClear in a wrapper, trust the macro
 *         and don't call PoolDestroy_clear directly.
 */
static void PoolClear_debug(Pool *p, const char *file_line);

#define TOSTRING_HELPER(n) #n
#define TOSTRING(n) TOSTRING_HELPER(n)
#define __FILE_LINE__ __FILE__ ":" TOSTRING(__LINE__)
#define newPool()  newPool_debug( __FILE_LINE__)

#define PoolClear(p)  PoolClear_debug(p, __FILE_LINE__)

/**
 * Debug version of PoolDestroy.
 * @param p See: PoolDestroy.
 * @param file_line Where the function is called from.
 *        This is usually __FILE_LINE__.
 * @remark Only available when POOL_DEBUG_VERSION is defined.
 *         Call this directly if you have you PoolDestroy
 *         calls in a wrapper function and wish to override
 *         the file_line argument to reflect the caller of
 *         your wrapper function.  If you do not have
 *         PoolDestroy in a wrapper, trust the macro
 *         and don't call PoolDestroy_debug directly.
 */
static void PoolDestroy_debug(Pool *p, const char *file_line);
#define PoolDestroy(p)  PoolDestroy_debug(p, __FILE_LINE__)

/**
 * Debug version of PoolAlloc
 * @param p See: PoolAlloc
 * @param size See: PoolAlloc
 * @param file_line Where the function is called from.
 *        This is usually __FILE_LINE__.
 * @return See: PoolAlloc
 */
static void * PoolAlloc_debug(Pool *p, size_t size, const char *file_line);
#define PoolAlloc(p, size) PoolAlloc_debug(p, size, __FILE_LINE__)

/**
 * Debug version of PoolCalloc
 * @param p See: PoolCalloc
 * @param size See: PoolCalloc
 * @param file_line Where the function is called from.
 *        This is usually __FILE_LINE__.
 * @return See: PoolCalloc
 */
static void * PoolCalloc_debug(Pool *p, size_t n,size_t size, const char *file_line);
#define PoolCalloc(p, n, size) PoolCalloc_debug(p, n, size, __FILE_LINE__)


/**
 * Report the number of bytes currently in the pool
 * @param p The pool to inspect
 * @return The number of bytes
 */
static size_t Sizeof(Pool *p);

/** The base size of a memory node - aligned.  */
#define MEMORYNODE_SIZE ALIGN_DEFAULT(sizeof(MemoryNode_t))

/**
 * Destroy an allocator
 * @param allocator The allocator to be destroyed
 * @remark Any memnodes not given back to the allocator prior to destroying
 *         will _not_ be free()d.
 */
static void destroyAllocator(Allocator *allocator);

/*
 * Magic numbers
 */

#define MIN_ALLOC 8192
#define BOUNDARY_INDEX 12
#define BOUNDARY_SIZE (1 << BOUNDARY_INDEX)

/*
 * Debug level
 */

#define POOL_DEBUG_VERSION_GENERAL  0x01
#define POOL_DEBUG_VERSION_VERBOSE  0x02
#define POOL_DEBUG_VERSION_LIFETIME 0x04
#define POOL_DEBUG_VERSION_OWNER    0x08
#define POOL_DEBUG_VERSION_VERBOSE_ALLOC 0x10

#define POOL_DEBUG_VERSION_VERBOSE_ALL (POOL_DEBUG_VERSION_VERBOSE|POOL_DEBUG_VERSION_VERBOSE_ALLOC)
/*
 * Structures
 */

typedef struct debug_node_t debug_node_t;
struct debug_node_t {
    debug_node_t *next;
    uint32_t  index;
    void         *beginp[64];
    void         *endp[64];
};
#define SIZEOF_DEBUG_NODE_T ALIGN_DEFAULT(sizeof(debug_node_t))

/* The ref field in the Pool struct holds a
 * pointer to the pointer referencing this pool.
 */
struct Pool {
    Allocator      *allocator;
    const char           *tag;
    debug_node_t         *nodes;
    const char           *file_line;
    uint32_t          creation_flags;
    unsigned int          stat_alloc;
    unsigned int          stat_total_alloc;
    unsigned int          stat_clear;
};

#define SIZEOF_POOL_T       ALIGN_DEFAULT(sizeof(Pool))

static void destroyAllocator(Allocator *allocator)
{
    uint32_t idx;
    MemoryNode_t *node, **ref;

    for (idx = 0; idx < MAX_INDEX; idx++) {
        ref = &allocator->free[idx];
        while ((node = *ref) != NULL) {
            *ref = node->next;
            free(node);
        }
    }
}


/*
 * Debug helper functions
 */

#include <stdio.h>
#if (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE_ALL)
static void Log(Pool *pool, const char *event, const char *file_line, int deref)
{
        if (deref) {
            fprintf(stderr,
                "POOL DEBUG: "
                "[%lu"
                "] "
                "%7s "
                "(%10lu) "
                "0x%pp \"%s\" "
                "<%s> "
                "(%u/%u/%u) "
                "\n",
                0UL,
                event,
                (unsigned long)Sizeof(pool),
                pool, pool->tag,
                file_line,
                pool->stat_alloc, pool->stat_total_alloc, pool->stat_clear);
        }
        else {
            fprintf(stderr,
                "POOL DEBUG: "
                "[%lu"
                "] "
                "%7s "
                "                                   "
                "0x%pp "
                "<%s> "
                "\n",
                0UL,
                event,
                pool,
                file_line);
        }
}
#endif /* (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE_ALL) */

static void CheckIntegrity(Pool *pool)
{
    (void)pool;
}


/*
 * Memory allocation (debug)
 */

static void *pool_alloc_debug(Pool *pool, size_t size,const char *file_line)
{
    debug_node_t *node;
    void *mem;

    (void)file_line;

    if ((mem = iDebugMalloc.malloc(size)) == NULL) {
        return NULL;
    }
    node = pool->nodes;
    if (node == NULL || node->index == 64) {
        if ((node = calloc(1,SIZEOF_DEBUG_NODE_T)) == NULL) {
            iDebugMalloc.free(mem);
            return NULL;
        }
        node->next = pool->nodes;
        pool->nodes = node;
    }

    node->beginp[node->index] = mem;
    node->endp[node->index] = (char *)mem + size;
    node->index++;

    pool->stat_alloc++;
    pool->stat_total_alloc++;

    return mem;
}

static void * PoolAlloc_debug(Pool *pool, size_t size, const char *file_line)
{
    void *mem;

    CheckIntegrity(pool);

    mem = pool_alloc_debug(pool, size,file_line);

#if (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE_ALLOC)
    Log(pool, "PALLOC", file_line, 1);
#endif /* (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE_ALLOC) */

    return mem;
}

static void * PoolCalloc_debug(Pool *pool, size_t n,size_t size, const char *file_line)
{
    void *mem;
    size_t total_size;

    CheckIntegrity(pool);

    if (size != 0 && n > SIZE_MAX / size)
        return NULL;
    total_size = n * size;
    mem = pool_alloc_debug(pool, total_size,file_line);
    if (mem)
        memset(mem, 0, total_size);

#if (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE_ALLOC)
    Log(pool, "PCALLOC", file_line, 1);
#endif /* (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE_ALLOC) */

    return mem;
}


/*
 * Pool creation/destruction (debug)
 */

#define POOL_POISON_BYTE 'A'

static void pool_clear_debug(Pool *pool, const char *file_line)
{
    debug_node_t *node;
    uint32_t idx;

    (void)file_line;

    /* Free the blocks, scribbling over them first to help highlight
     * use-after-free issues. */
    while ((node = pool->nodes) != NULL) {
        pool->nodes = node->next;

        for (idx = 0; idx < node->index; idx++) {
            memset(node->beginp[idx], POOL_POISON_BYTE,
                   (char *)node->endp[idx] - (char *)node->beginp[idx]);
            iDebugMalloc.free(node->beginp[idx]);
        }

        memset(node, POOL_POISON_BYTE, SIZEOF_DEBUG_NODE_T);
        free(node);
    }

    pool->stat_alloc = 0;
    pool->stat_clear++;
}

static void PoolClear_debug(Pool *pool, const char *file_line)
{
    CheckIntegrity(pool);

#if (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE)
    Log(pool, "CLEAR", file_line, 1);
#endif /* (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE) */

    pool_clear_debug(pool, file_line);
}

static void PoolDestroy_debug(Pool *pool, const char *file_line)
{
    CheckIntegrity(pool);

#if (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE)
    Log(pool, "DESTROY", file_line, 1);
#endif /* (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE) */

    pool_clear_debug(pool, file_line);

    /* The allocator is bookkeeping owned by this pool.  It is allocated
     * with libc calloc, so release it with libc free after draining it. */
    if (pool->allocator != NULL) {
        destroyAllocator(pool->allocator);
        free(pool->allocator);
        pool->allocator = NULL;
    }

    /* Free the pool itself */
    free(pool);

}

static Pool *newPool_debug( const char *file_line)
{
    Pool *pool;
    Allocator *pool_allocator;

    if ((pool = calloc(1,SIZEOF_POOL_T)) == NULL) {
         return NULL;
    }

    pool->tag = file_line;
    pool->file_line = file_line;

     if ((pool_allocator = calloc(1,sizeof(Allocator))) == NULL) {
        free(pool); /* Was missing! */
        return NULL;
    }
    pool_allocator->owner = pool;
    pool->allocator = pool_allocator;

#if (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE)
    Log(pool, "CREATE", file_line, 1);
#endif /* (POOL_DEBUG_VERSION & POOL_DEBUG_VERSION_VERBOSE) */

    return pool;
}

/*
 * Debug functions
 */

static int FindPoolFromData(Pool *pool, void *data)
{
    void **pmem = (void **)data;
    debug_node_t *node;
    uint32_t idx;
    uintptr_t address;

    if (pool == NULL || pmem == NULL || *pmem == NULL)
        return 0;

    address = (uintptr_t)*pmem;

    node = pool->nodes;

    while (node) {
        for (idx = 0; idx < node->index; idx++) {
             if ((uintptr_t)node->beginp[idx] <= address
                 && (uintptr_t)node->endp[idx] > address) {
                 *pmem = pool;
                 return 1;
             }
        }

        node = node->next;
    }

    return 0;
}

static void SetMaxSize(Pool *pool,size_t in_size)
{
    uint32_t max_free_index;
    uint32_t size = (uint32_t)in_size;
	Allocator *allocator = pool->allocator;

    max_free_index = ALIGN(size, BOUNDARY_SIZE) >> BOUNDARY_INDEX;
    allocator->current_free_index += max_free_index;
    allocator->current_free_index -= allocator->max_free_index;
    allocator->max_free_index = max_free_index;
    if (allocator->current_free_index > max_free_index)
		allocator->current_free_index = max_free_index;

}

static size_t Sizeof(Pool *pool)
{
    size_t size = 0;
    debug_node_t *node;
    uint32_t idx;

	if (pool == NULL)
		return sizeof(Pool);
    node = pool->nodes;
    while (node) {
        for (idx = 0; idx < node->index; idx++) {
            size += (char *)node->endp[idx] - (char *)node->beginp[idx];
        }
        node = node->next;
    }
    return size;
}
PoolAllocatorDebugInterface iPoolDebug = {
	newPool_debug,
	PoolAlloc_debug,
	PoolCalloc_debug,
	PoolClear_debug,
	PoolDestroy_debug,
	FindPoolFromData,
	SetMaxSize,
	Sizeof,
};


#ifdef TEST
int main(void)
{
	Pool *pool;
	void *mem;
	pool = newPool();
	mem = PoolAlloc(pool,1024);
	memset(mem,0,1024);
	PoolDestroy(pool);

	return 0;
}
#endif
