/*
 * AVL search trees whose values are also their keys.
 *
 * The public interface is deliberately small and is kept compatible with the
 * historical implementation.  Nodes own a byte-for-byte copy of each value;
 * the allocator, comparator, destructor, and error callback are instance
 * properties even though the interface object itself is shared.
 */
#include "containers.h"
#include "ccl_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct tagBinarySearchTreeNode {
    char hidden;
    signed char factor; /* LEFT, BALANCED, or RIGHT. */
    struct tagBinarySearchTreeNode *left;
    struct tagBinarySearchTreeNode *right;
    char data[MINIMUM_ARRAY_INDEX];
} BinarySearchTreeNode;

#define LEFT       1
#define BALANCED   0
#define RIGHT     -1
#define SEARCHTREE_ITERATOR_MAGIC 0x53545249u

typedef struct tagBinarySearchTreeIterator BinarySearchTreeIterator;

struct tagBinarySearchTree {
    struct tagBinarySearchTreeInterface *VTable;
    unsigned Flags;
    size_t count;
    size_t ElementSize;
    ErrorFunction RaiseError;
    BinarySearchTreeNode *root;
    ContainerAllocator *Allocator;
    DestructorFunction DestructorFn;
    CompareFunction CompareFn;
    unsigned timestamp;
};

struct tagBinarySearchTreeIterator {
    Iterator it;
    unsigned magic;
    BinarySearchTree *tree;
    ContainerAllocator *Allocator;
    unsigned timestamp;
    size_t index;
    BinarySearchTreeNode *current;
    void *buffer;
};

static size_t GetCount(BinarySearchTree *tree);
static unsigned GetFlags(BinarySearchTree *tree);
static unsigned SetFlags(BinarySearchTree *tree, unsigned flags);
static int Clear(BinarySearchTree *tree);
static int Contains(BinarySearchTree *tree, void *data);
static int Remove(BinarySearchTree *tree, const void *data, void *extra);
static int Finalize(BinarySearchTree *tree);
static int Apply(BinarySearchTree *tree,
                 int (*Applyfn)(const void *data, void *arg), void *arg);
static int Equal(const BinarySearchTree *left, const BinarySearchTree *right);
static int Add(BinarySearchTree *tree, const void *data);
static int Insert(BinarySearchTree *tree, const void *data, void *extra);
static ErrorFunction SetErrorFunction(BinarySearchTree *tree, ErrorFunction fn);
static CompareFunction SetCompareFunction(BinarySearchTree *tree,
                                           CompareFunction fn);
static size_t Sizeof(BinarySearchTree *tree);
static int DefaultCompareFunction(const void *left, const void *right,
                                  CompareInfo *extra);
static BinarySearchTree *Merge(BinarySearchTree *left, BinarySearchTree *right,
                               const void *data);
static Iterator *NewIterator(BinarySearchTree *tree);
static int DeleteIterator(Iterator *iterator);
static DestructorFunction SetDestructor(BinarySearchTree *tree,
                                        DestructorFunction fn);

static int report_error(BinarySearchTree *tree, const char *operation, int code)
{
    ErrorFunction fn = tree != NULL ? tree->RaiseError : iError.RaiseError;
    if (fn != NULL)
        fn(operation, code);
    return code;
}

static int valid_data(const BinarySearchTree *tree, const void *data)
{
    return tree != NULL && (data != NULL || tree->ElementSize == 0);
}

static BinarySearchTree *create_with_allocator(size_t element_size,
                                                ContainerAllocator *allocator)
{
    BinarySearchTree *tree;

    if (allocator == NULL)
        return NULL;
    tree = allocator->malloc(sizeof(*tree));
    if (tree == NULL)
        return NULL;
    memset(tree, 0, sizeof(*tree));
    tree->VTable = &iBinarySearchTree;
    tree->ElementSize = element_size;
    tree->RaiseError = iError.RaiseError;
    tree->Allocator = allocator;
    tree->CompareFn = DefaultCompareFunction;
    return tree;
}

static BinarySearchTree *Create(size_t element_size)
{
    return create_with_allocator(element_size, CurrentAllocator);
}

static size_t GetCount(BinarySearchTree *tree)
{
    return tree != NULL ? tree->count : 0;
}

static unsigned GetFlags(BinarySearchTree *tree)
{
    return tree != NULL ? tree->Flags : 0;
}

static unsigned SetFlags(BinarySearchTree *tree, unsigned flags)
{
    unsigned old;
    if (tree == NULL)
        return 0;
    old = tree->Flags;
    tree->Flags = flags;
    return old;
}

static BinarySearchTreeNode *new_tree_node(BinarySearchTree *tree,
                                            const void *data)
{
    BinarySearchTreeNode *node;
    size_t bytes;

    if (tree->ElementSize > SIZE_MAX - sizeof(*node))
        return NULL;
    bytes = sizeof(*node) + tree->ElementSize;
    node = tree->Allocator->malloc(bytes);
    if (node == NULL)
        return NULL;
    memset(node, 0, sizeof(*node));
    if (tree->ElementSize != 0)
        memcpy(node->data, data, tree->ElementSize);
    ++tree->count;
    return node;
}

static int node_height(const BinarySearchTreeNode *node)
{
    int left_height;
    int right_height;
    if (node == NULL)
        return 0;
    left_height = node_height(node->left);
    right_height = node_height(node->right);
    return (left_height > right_height ? left_height : right_height) + 1;
}

static void update_factor(BinarySearchTreeNode *node)
{
    int left_height;
    int right_height;
    if (node == NULL)
        return;
    left_height = node_height(node->left);
    right_height = node_height(node->right);
    node->factor = (signed char)(left_height > right_height ? LEFT :
                                left_height < right_height ? RIGHT : BALANCED);
}

/* These names describe the direction of the actual rotation. */
static BinarySearchTreeNode *rotate_right(BinarySearchTreeNode *node)
{
    BinarySearchTreeNode *child = node->left;
    node->left = child->right;
    child->right = node;
    update_factor(node);
    update_factor(child);
    return child;
}

static BinarySearchTreeNode *rotate_left(BinarySearchTreeNode *node)
{
    BinarySearchTreeNode *child = node->right;
    node->right = child->left;
    child->left = node;
    update_factor(node);
    update_factor(child);
    return child;
}

static BinarySearchTreeNode *rebalance(BinarySearchTreeNode *node)
{
    int left_height;
    int right_height;

    if (node == NULL)
        return NULL;
    left_height = node_height(node->left);
    right_height = node_height(node->right);
    if (left_height - right_height > 1) {
        if (node_height(node->left->left) < node_height(node->left->right))
            node->left = rotate_left(node->left);
        return rotate_right(node);
    }
    if (right_height - left_height > 1) {
        if (node_height(node->right->right) < node_height(node->right->left))
            node->right = rotate_right(node->right);
        return rotate_left(node);
    }
    update_factor(node);
    return node;
}

static int compare_values(const BinarySearchTree *tree, const void *left,
                          const void *right, void *extra)
{
    CompareInfo info;
    info.ContainerLeft = tree;
    info.ContainerRight = NULL;
    info.ExtraArgs = extra;
    return tree->CompareFn(left, right, &info);
}

static BinarySearchTreeNode *insert_node(BinarySearchTree *tree,
                                         BinarySearchTreeNode *node,
                                         const void *data, void *extra,
                                         int *result)
{
    int comparison;

    if (node == NULL) {
        node = new_tree_node(tree, data);
        if (node == NULL) {
            *result = -1;
            return NULL;
        }
        *result = 0;
        return node;
    }

    comparison = compare_values(tree, data, node->data, extra);
    if (comparison < 0) {
        BinarySearchTreeNode *child = insert_node(tree, node->left, data,
                                                   extra, result);
        if (*result < 0)
            return node;
        node->left = child;
    } else if (comparison > 0) {
        BinarySearchTreeNode *child = insert_node(tree, node->right, data,
                                                   extra, result);
        if (*result < 0)
            return node;
        node->right = child;
    } else if (!node->hidden) {
        *result = 1;
        return node;
    } else {
        if (tree->ElementSize != 0)
            memcpy(node->data, data, tree->ElementSize);
        node->hidden = 0;
        *result = 0;
        return node;
    }
    return rebalance(node);
}

static void destroy_nodes(BinarySearchTree *tree, BinarySearchTreeNode *node)
{
    if (node == NULL)
        return;
    destroy_nodes(tree, node->left);
    destroy_nodes(tree, node->right);
    if (tree->DestructorFn != NULL)
        tree->DestructorFn(node->data);
    if (tree->Allocator != NULL && tree->Allocator->free != NULL)
        tree->Allocator->free(node);
}

static BinarySearchTreeNode *detach_min(BinarySearchTreeNode *node,
                                         BinarySearchTreeNode **minimum)
{
    if (node->left == NULL) {
        *minimum = node;
        return node->right;
    }
    node->left = detach_min(node->left, minimum);
    return rebalance(node);
}

static BinarySearchTreeNode *delete_node(BinarySearchTree *tree,
                                         BinarySearchTreeNode *node,
                                         const void *data, void *extra,
                                         int *removed)
{
    int comparison;

    if (node == NULL)
        return NULL;
    comparison = compare_values(tree, data, node->data, extra);
    if (comparison < 0) {
        BinarySearchTreeNode *child = delete_node(tree, node->left, data,
                                                   extra, removed);
        if (!*removed)
            return node;
        node->left = child;
        return rebalance(node);
    }
    if (comparison > 0) {
        BinarySearchTreeNode *child = delete_node(tree, node->right, data,
                                                   extra, removed);
        if (!*removed)
            return node;
        node->right = child;
        return rebalance(node);
    }

    *removed = 1;
    if (node->left == NULL || node->right == NULL) {
        BinarySearchTreeNode *replacement = node->left != NULL ? node->left
                                                                  : node->right;
        if (tree->DestructorFn != NULL)
            tree->DestructorFn(node->data);
        tree->Allocator->free(node);
        --tree->count;
        return replacement;
    }
    {
        BinarySearchTreeNode *successor = NULL;
        BinarySearchTreeNode *right = detach_min(node->right, &successor);
        successor->left = node->left;
        successor->right = right;
        if (tree->DestructorFn != NULL)
            tree->DestructorFn(node->data);
        tree->Allocator->free(node);
        --tree->count;
        return rebalance(successor);
    }
}

static int Clear(BinarySearchTree *tree)
{
    if (tree == NULL)
        return report_error(NULL, "iBinarySearchTree.Clear", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iBinarySearchTree.Clear", CONTAINER_ERROR_READONLY);
    destroy_nodes(tree, tree->root);
    tree->root = NULL;
    tree->count = 0;
    ++tree->timestamp;
    return 1;
}

static int Finalize(BinarySearchTree *tree)
{
    ContainerAllocator *allocator;
    if (tree == NULL)
        return report_error(NULL, "iBinarySearchTree.Finalize", CONTAINER_ERROR_BADARG);
    allocator = tree->Allocator != NULL ? tree->Allocator : CurrentAllocator;
    destroy_nodes(tree, tree->root);
    tree->root = NULL;
    tree->count = 0;
    allocator->free(tree);
    return 1;
}

static int Add(BinarySearchTree *tree, const void *data)
{
    int result = -1;
    if (tree == NULL || !valid_data(tree, data))
        return report_error(tree, "iBinarySearchTree.Add", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iBinarySearchTree.Add", CONTAINER_ERROR_READONLY);
    tree->root = insert_node(tree, tree->root, data, NULL, &result);
    if (result == 0)
        ++tree->timestamp;
    if (result < 0)
        report_error(tree, "iBinarySearchTree.Add", CONTAINER_ERROR_NOMEMORY);
    return result;
}

static int Insert(BinarySearchTree *tree, const void *data, void *extra)
{
    int result = -1;
    if (tree == NULL || !valid_data(tree, data))
        return report_error(tree, "iBinarySearchTree.Insert", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iBinarySearchTree.Insert", CONTAINER_ERROR_READONLY);
    tree->root = insert_node(tree, tree->root, data, extra, &result);
    if (result == 0)
        ++tree->timestamp;
    if (result < 0)
        report_error(tree, "iBinarySearchTree.Insert", CONTAINER_ERROR_NOMEMORY);
    return result;
}

static int Remove(BinarySearchTree *tree, const void *data, void *extra)
{
    int removed = 0;
    if (tree == NULL || !valid_data(tree, data))
        return report_error(tree, "iBinarySearchTree.Erase", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iBinarySearchTree.Erase", CONTAINER_ERROR_READONLY);
    tree->root = delete_node(tree, tree->root, data, extra, &removed);
    if (removed)
        ++tree->timestamp;
    return removed;
}

static void *lookup(BinarySearchTree *tree, BinarySearchTreeNode *node,
                    void *data)
{
    while (node != NULL) {
        int comparison = compare_values(tree, data, node->data, NULL);
        if (comparison < 0)
            node = node->left;
        else if (comparison > 0)
            node = node->right;
        else
            return node->hidden ? NULL : node->data;
    }
    return NULL;
}

static void *Find(BinarySearchTree *tree, void *data)
{
    if (tree == NULL || !valid_data(tree, data)) {
        report_error(tree, "iBinarySearchTree.Find", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    return lookup(tree, tree->root, data);
}

static int Contains(BinarySearchTree *tree, void *data)
{
    if (tree == NULL || !valid_data(tree, data))
        return report_error(tree, "iBinarySearchTree.Contains", CONTAINER_ERROR_BADARG);
    return Find(tree, data) != NULL ? 1 : CONTAINER_ERROR_NOTFOUND;
}

static int visit_nodes(BinarySearchTreeNode *node,
                       int (*Applyfn)(const void *data, void *arg), void *arg)
{
    if (node == NULL)
        return 1;
    if (!visit_nodes(node->left, Applyfn, arg))
        return 0;
    if (!Applyfn(node->data, arg))
        return 0;
    return visit_nodes(node->right, Applyfn, arg);
}

static int Apply(BinarySearchTree *tree,
                 int (*Applyfn)(const void *data, void *arg), void *arg)
{
    if (tree == NULL || Applyfn == NULL)
        return report_error(tree, "iBinarySearchTree.Apply", CONTAINER_ERROR_BADARG);
    if (tree->root == NULL)
        return 0;
    return visit_nodes(tree->root, Applyfn, arg);
}

static ErrorFunction SetErrorFunction(BinarySearchTree *tree, ErrorFunction fn)
{
    ErrorFunction old;
    if (tree == NULL)
        return iError.RaiseError;
    old = tree->RaiseError;
    tree->RaiseError = fn != NULL ? fn : iError.EmptyErrorFunction;
    return old;
}

static size_t Sizeof(BinarySearchTree *tree)
{
    if (tree == NULL)
        return sizeof(BinarySearchTree);
    return sizeof(*tree) + tree->count * (sizeof(BinarySearchTreeNode) +
                                          tree->ElementSize);
}

static int DefaultCompareFunction(const void *left, const void *right,
                                  CompareInfo *extra)
{
    const BinarySearchTree *tree = extra != NULL ? extra->ContainerLeft : NULL;
    size_t element_size = tree != NULL ? tree->ElementSize : 0;
    if (element_size == 0)
        return 0;
    if (left == NULL || right == NULL)
        return left == right ? 0 : (left == NULL ? -1 : 1);
    return memcmp(left, right, element_size);
}

static CompareFunction SetCompareFunction(BinarySearchTree *tree,
                                           CompareFunction fn)
{
    CompareFunction old;
    if (tree == NULL)
        return NULL;
    old = tree->CompareFn;
    if (fn != NULL)
        tree->CompareFn = fn;
    return old;
}

static int compare_headers(const BinarySearchTree *left,
                           const BinarySearchTree *right)
{
    return left != NULL && right != NULL &&
           left->count == right->count &&
           left->ElementSize == right->ElementSize &&
           left->CompareFn == right->CompareFn;
}

static int compare_nodes(const BinarySearchTree *tree,
                         const BinarySearchTreeNode *left,
                         const BinarySearchTreeNode *right)
{
    if (left == NULL || right == NULL)
        return left == right;
    if (left->hidden != right->hidden || left->factor != right->factor)
        return 0;
    if (compare_values(tree, left->data, right->data, NULL) != 0)
        return 0;
    return compare_nodes(tree, left->left, right->left) &&
           compare_nodes(tree, left->right, right->right);
}

static int Equal(const BinarySearchTree *left, const BinarySearchTree *right)
{
    if (!compare_headers(left, right))
        return 0;
    if (left->root == NULL || right->root == NULL)
        return left->root == right->root;
    return compare_nodes(left, left->root, right->root);
}

static BinarySearchTreeNode *node_at(BinarySearchTreeNode *node, size_t *index,
                                     size_t wanted)
{
    BinarySearchTreeNode *result;
    if (node == NULL)
        return NULL;
    result = node_at(node->left, index, wanted);
    if (result != NULL)
        return result;
    if (*index == wanted)
        return node;
    ++*index;
    return node_at(node->right, index, wanted);
}

static BinarySearchTreeNode *iterator_node(BinarySearchTreeIterator *iterator,
                                            size_t index)
{
    size_t current = 0;
    return node_at(iterator->tree->root, &current, index);
}

static int iterator_stale(BinarySearchTreeIterator *iterator,
                           const char *operation)
{
    if (iterator == NULL || iterator->magic != SEARCHTREE_ITERATOR_MAGIC)
        return 1;
    if (iterator->timestamp != iterator->tree->timestamp) {
        iterator->tree->RaiseError(operation, CONTAINER_ERROR_OBJECT_CHANGED);
        return 1;
    }
    return 0;
}

static void *iterator_value(BinarySearchTreeIterator *iterator)
{
    if (iterator->current == NULL)
        return NULL;
    if (iterator->tree->Flags & CONTAINER_READONLY) {
        if (iterator->tree->ElementSize != 0)
            memcpy(iterator->buffer, iterator->current->data,
                   iterator->tree->ElementSize);
        return iterator->buffer;
    }
    return iterator->current->data;
}

static void *IteratorGetFirst(Iterator *base)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.GetFirst") ||
        iterator->tree->count == 0)
        return NULL;
    iterator->index = 0;
    iterator->current = iterator_node(iterator, iterator->index);
    return iterator_value(iterator);
}

static void *IteratorGetLast(Iterator *base)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.GetLast") ||
        iterator->tree->count == 0)
        return NULL;
    iterator->index = iterator->tree->count - 1;
    iterator->current = iterator_node(iterator, iterator->index);
    return iterator_value(iterator);
}

static void *IteratorGetCurrent(Iterator *base)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.GetCurrent"))
        return NULL;
    return iterator_value(iterator);
}

static void *IteratorGetNext(Iterator *base)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.GetNext") ||
        iterator->current == NULL || iterator->index + 1 >= iterator->tree->count)
        return NULL;
    ++iterator->index;
    iterator->current = iterator_node(iterator, iterator->index);
    return iterator_value(iterator);
}

static void *IteratorGetPrevious(Iterator *base)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.GetPrevious") ||
        iterator->current == NULL || iterator->index == 0)
        return NULL;
    --iterator->index;
    iterator->current = iterator_node(iterator, iterator->index);
    return iterator_value(iterator);
}

static void *IteratorSeek(Iterator *base, size_t index)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.Seek") ||
        index >= iterator->tree->count)
        return NULL;
    iterator->index = index;
    iterator->current = iterator_node(iterator, index);
    return iterator_value(iterator);
}

static size_t IteratorGetPosition(Iterator *base)
{
    BinarySearchTreeIterator *iterator = (BinarySearchTreeIterator *)base;
    if (iterator_stale(iterator, "iBinarySearchTree.GetPosition"))
        return (size_t)-1;
    return iterator->index;
}

static int IteratorReplace(Iterator *base, void *data, int direction)
{
    (void)base;
    (void)data;
    (void)direction;
    return CONTAINER_ERROR_NOTIMPLEMENTED;
}

static Iterator *NewIterator(BinarySearchTree *tree)
{
    BinarySearchTreeIterator *iterator;
    if (tree == NULL) {
        report_error(NULL, "iBinarySearchTree.NewIterator", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    iterator = tree->Allocator->calloc(1, sizeof(*iterator));
    if (iterator == NULL) {
        report_error(tree, "iBinarySearchTree.NewIterator", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if (tree->ElementSize != 0 && (tree->Flags & CONTAINER_READONLY)) {
        iterator->buffer = tree->Allocator->malloc(tree->ElementSize);
        if (iterator->buffer == NULL) {
            tree->Allocator->free(iterator);
            report_error(tree, "iBinarySearchTree.NewIterator", CONTAINER_ERROR_NOMEMORY);
            return NULL;
        }
    }
    iterator->it.GetNext = IteratorGetNext;
    iterator->it.GetPrevious = IteratorGetPrevious;
    iterator->it.GetFirst = IteratorGetFirst;
    iterator->it.GetCurrent = IteratorGetCurrent;
    iterator->it.GetLast = IteratorGetLast;
    iterator->it.Seek = IteratorSeek;
    iterator->it.GetPosition = IteratorGetPosition;
    iterator->it.Replace = IteratorReplace;
    iterator->magic = SEARCHTREE_ITERATOR_MAGIC;
    iterator->tree = tree;
    iterator->Allocator = tree->Allocator;
    iterator->timestamp = tree->timestamp;
    iterator->index = (size_t)-1;
    return &iterator->it;
}

static int DeleteIterator(Iterator *base)
{
    BinarySearchTreeIterator *iterator;
    if (base == NULL)
        return CONTAINER_ERROR_BADARG;
    iterator = (BinarySearchTreeIterator *)base;
    if (iterator->magic != SEARCHTREE_ITERATOR_MAGIC)
        return CONTAINER_ERROR_WRONG_ITERATOR;
    if (iterator->buffer != NULL)
        iterator->Allocator->free(iterator->buffer);
    iterator->magic = 0;
    iterator->Allocator->free(iterator);
    return 1;
}

static BinarySearchTree *Merge(BinarySearchTree *left, BinarySearchTree *right,
                               const void *data)
{
    BinarySearchTree *merge;
    if (left == NULL || right == NULL ||
        left->ElementSize != right->ElementSize ||
        left->CompareFn != right->CompareFn ||
        left->Allocator != right->Allocator ||
        left->DestructorFn != right->DestructorFn ||
        !valid_data(left, data))
        return NULL;
    if ((left->Flags & CONTAINER_READONLY) || (right->Flags & CONTAINER_READONLY))
        return NULL;
    merge = create_with_allocator(left->ElementSize, left->Allocator);
    if (merge == NULL)
        return NULL;
    merge->Flags = left->Flags;
    merge->RaiseError = left->RaiseError;
    merge->CompareFn = left->CompareFn;
    merge->DestructorFn = left->DestructorFn;
    merge->root = new_tree_node(merge, data);
    if (merge->root == NULL) {
        merge->Allocator->free(merge);
        return NULL;
    }
    merge->root->left = left->root;
    merge->root->right = right->root;
    update_factor(merge->root);
    merge->count += left->count + right->count;
    left->root = NULL;
    left->count = 0;
    right->root = NULL;
    right->count = 0;
    ++left->timestamp;
    ++right->timestamp;
    ++merge->timestamp;
    return merge;
}

static DestructorFunction SetDestructor(BinarySearchTree *tree,
                                        DestructorFunction fn)
{
    DestructorFunction old;
    if (tree == NULL)
        return NULL;
    old = tree->DestructorFn;
    tree->DestructorFn = fn;
    return old;
}

BinarySearchTreeInterface iBinarySearchTree = {
    GetCount,
    GetFlags,
    SetFlags,
    Clear,
    Contains,
    Remove,
    Finalize,
    Apply,
    Equal,
    Add,
    Insert,
    SetErrorFunction,
    SetCompareFunction,
    Sizeof,
    DefaultCompareFunction,
    Merge,
    NewIterator,
    DeleteIterator,
    Create,
    SetDestructor,
};
