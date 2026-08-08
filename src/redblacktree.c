/* A red/black search tree with byte-sized keys and copied values.
 *
 * The original implementation used an external-leaf representation and a
 * block allocator, but left the public constructor unusable and had several
 * aliasing and ownership bugs.  This implementation keeps the public ABI
 * while using ordinary NULL leaves and one allocator-owned node/value pair.
 */
#include "containers.h"
#include "ccl_internal.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define RB_BLACK 0
#define RB_RED   1
#define RB_ITERATOR_MAGIC 0x52425449u

typedef struct tagRedBlackTreeNode RedBlackTreeNode;
struct tagRedBlackTreeNode {
    unsigned char color;
    RedBlackTreeNode *left;
    RedBlackTreeNode *right;
    RedBlackTreeNode *parent;
    void *data;
    unsigned char key[];
};

struct tagRedBlackTree {
    struct tagRedBlackTreeInterface *VTable;
    unsigned Flags;
    size_t count;
    size_t ElementSize;
    ErrorFunction RaiseError;
    CompareFunction KeyCompareFn;
    size_t KeySize;
    /* Kept for source/binary compatibility with historical private layout. */
    size_t Available;
    RedBlackTreeNode *root;
    RedBlackTreeNode *CurrentBlock;
    RedBlackTreeNode *FreeList;
    ContainerAllocator *Allocator;
    DestructorFunction DestructorFn;
    unsigned timestamp;
};

typedef struct tagRedBlackTreeIterator RedBlackTreeIterator;
struct tagRedBlackTreeIterator {
    Iterator it;
    unsigned magic;
    RedBlackTree *tree;
    ContainerAllocator *Allocator;
    unsigned timestamp;
    size_t index;
    RedBlackTreeNode *current;
    void *buffer;
};

static size_t GetCount(RedBlackTree *tree);
static unsigned GetFlags(RedBlackTree *tree);
static unsigned SetFlags(RedBlackTree *tree, unsigned flags);
static int Add(RedBlackTree *tree, const void *key, const void *data);
static int Insert(RedBlackTree *tree, const void *key, const void *data,
                  void *extra);
static int Clear(RedBlackTree *tree);
static int Remove(RedBlackTree *tree, const void *key, void *extra);
static int Finalize(RedBlackTree *tree);
static int Apply(RedBlackTree *tree,
                 int (*Applyfn)(const void *data, void *arg), void *arg);
static void *Find(RedBlackTree *tree, void *key, void *extra);
static ErrorFunction SetErrorFunction(RedBlackTree *tree, ErrorFunction fn);
static CompareFunction SetCompareFunction(RedBlackTree *tree,
                                           CompareFunction fn);
static size_t Sizeof(RedBlackTree *tree);
static int DefaultCompareFunction(const void *left, const void *right,
                                  CompareInfo *extra);
static Iterator *NewIterator(RedBlackTree *tree);
static int DeleteIterator(Iterator *iterator);
static DestructorFunction SetDestructor(RedBlackTree *tree,
                                        DestructorFunction fn);

static int report_error(RedBlackTree *tree, const char *operation, int code)
{
    ErrorFunction fn = tree != NULL ? tree->RaiseError : iError.RaiseError;
    if (fn != NULL)
        fn(operation, code);
    return code;
}

static int valid_arguments(RedBlackTree *tree, const void *key,
                           const void *data)
{
    return tree != NULL && key != NULL &&
           (data != NULL || tree->ElementSize == 0);
}

static int node_color(const RedBlackTreeNode *node)
{
    return node == NULL ? RB_BLACK : node->color;
}

static void set_color(RedBlackTreeNode *node, int color)
{
    if (node != NULL)
        node->color = (unsigned char)color;
}

static RedBlackTreeNode *node_at(RedBlackTreeNode *node, size_t *index,
                                 size_t wanted)
{
    RedBlackTreeNode *found;
    if (node == NULL)
        return NULL;
    found = node_at(node->left, index, wanted);
    if (found != NULL)
        return found;
    if (*index == wanted)
        return node;
    ++*index;
    return node_at(node->right, index, wanted);
}

static void set_compare_info(RedBlackTree *tree, CompareInfo *info,
                             void *extra)
{
    info->ContainerLeft = tree;
    info->ContainerRight = NULL;
    info->ExtraArgs = extra;
}

static int compare_keys(RedBlackTree *tree, const void *left, const void *right,
                        void *extra)
{
    CompareInfo info;
    set_compare_info(tree, &info, extra);
    return tree->KeyCompareFn(left, right, &info);
}

static RedBlackTreeNode *new_node(RedBlackTree *tree, const void *key,
                                  const void *data)
{
    RedBlackTreeNode *node;

    if (tree->KeySize > SIZE_MAX - offsetof(RedBlackTreeNode, key))
        return NULL;
    node = tree->Allocator->malloc(offsetof(RedBlackTreeNode, key) +
                                   tree->KeySize);
    if (node == NULL)
        return NULL;
    memset(node, 0, offsetof(RedBlackTreeNode, key));
    memcpy(node->key, key, tree->KeySize);
    node->data = tree->Allocator->malloc(tree->ElementSize);
    if (node->data == NULL) {
        tree->Allocator->free(node);
        return NULL;
    }
    memcpy(node->data, data, tree->ElementSize);
    node->color = RB_RED;
    return node;
}

static void destroy_node(RedBlackTree *tree, RedBlackTreeNode *node)
{
    if (node == NULL)
        return;
    if (tree->DestructorFn != NULL)
        tree->DestructorFn(node->data);
    tree->Allocator->free(node->data);
    tree->Allocator->free(node);
}

static void destroy_nodes(RedBlackTree *tree, RedBlackTreeNode *node)
{
    if (node == NULL)
        return;
    destroy_nodes(tree, node->left);
    destroy_nodes(tree, node->right);
    destroy_node(tree, node);
}

static void left_rotate(RedBlackTree *tree, RedBlackTreeNode *x)
{
    RedBlackTreeNode *y = x->right;
    x->right = y->left;
    if (y->left != NULL)
        y->left->parent = x;
    y->parent = x->parent;
    if (x->parent == NULL)
        tree->root = y;
    else if (x == x->parent->left)
        x->parent->left = y;
    else
        x->parent->right = y;
    y->left = x;
    x->parent = y;
}

static void right_rotate(RedBlackTree *tree, RedBlackTreeNode *x)
{
    RedBlackTreeNode *y = x->left;
    x->left = y->right;
    if (y->right != NULL)
        y->right->parent = x;
    y->parent = x->parent;
    if (x->parent == NULL)
        tree->root = y;
    else if (x == x->parent->right)
        x->parent->right = y;
    else
        x->parent->left = y;
    y->right = x;
    x->parent = y;
}

static void insert_fixup(RedBlackTree *tree, RedBlackTreeNode *node)
{
    RedBlackTreeNode *uncle;
    while (node->parent != NULL && node_color(node->parent) == RB_RED) {
        if (node->parent == node->parent->parent->left) {
            uncle = node->parent->parent->right;
            if (node_color(uncle) == RB_RED) {
                set_color(node->parent, RB_BLACK);
                set_color(uncle, RB_BLACK);
                set_color(node->parent->parent, RB_RED);
                node = node->parent->parent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    left_rotate(tree, node);
                }
                set_color(node->parent, RB_BLACK);
                set_color(node->parent->parent, RB_RED);
                right_rotate(tree, node->parent->parent);
            }
        } else {
            uncle = node->parent->parent->left;
            if (node_color(uncle) == RB_RED) {
                set_color(node->parent, RB_BLACK);
                set_color(uncle, RB_BLACK);
                set_color(node->parent->parent, RB_RED);
                node = node->parent->parent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    right_rotate(tree, node);
                }
                set_color(node->parent, RB_BLACK);
                set_color(node->parent->parent, RB_RED);
                left_rotate(tree, node->parent->parent);
            }
        }
    }
    set_color(tree->root, RB_BLACK);
}

static RedBlackTreeNode *lookup_node(RedBlackTree *tree, const void *key,
                                     void *extra)
{
    RedBlackTreeNode *node = tree->root;
    int comparison;
    while (node != NULL) {
        comparison = compare_keys(tree, key, node->key, extra);
        if (comparison == 0)
            return node;
        node = comparison < 0 ? node->left : node->right;
    }
    return NULL;
}

static void transplant(RedBlackTree *tree, RedBlackTreeNode *old_node,
                       RedBlackTreeNode *new_node)
{
    if (old_node->parent == NULL)
        tree->root = new_node;
    else if (old_node == old_node->parent->left)
        old_node->parent->left = new_node;
    else
        old_node->parent->right = new_node;
    if (new_node != NULL)
        new_node->parent = old_node->parent;
}

static RedBlackTreeNode *minimum_node(RedBlackTreeNode *node)
{
    while (node != NULL && node->left != NULL)
        node = node->left;
    return node;
}

/* x may be NULL; parent is therefore passed separately. */
static void delete_fixup(RedBlackTree *tree, RedBlackTreeNode *x,
                         RedBlackTreeNode *parent)
{
    RedBlackTreeNode *sibling;
    while (x != tree->root && node_color(x) == RB_BLACK) {
        if (parent == NULL)
            break;
        if (x == parent->left) {
            sibling = parent->right;
            if (node_color(sibling) == RB_RED) {
                set_color(sibling, RB_BLACK);
                set_color(parent, RB_RED);
                left_rotate(tree, parent);
                sibling = parent->right;
            }
            if (node_color(sibling == NULL ? NULL : sibling->left) == RB_BLACK &&
                node_color(sibling == NULL ? NULL : sibling->right) == RB_BLACK) {
                set_color(sibling, RB_RED);
                x = parent;
                parent = x->parent;
            } else {
                if (node_color(sibling == NULL ? NULL : sibling->right) == RB_BLACK) {
                    set_color(sibling == NULL ? NULL : sibling->left, RB_BLACK);
                    set_color(sibling, RB_RED);
                    if (sibling != NULL)
                        right_rotate(tree, sibling);
                    sibling = parent->right;
                }
                set_color(sibling, node_color(parent));
                set_color(parent, RB_BLACK);
                set_color(sibling == NULL ? NULL : sibling->right, RB_BLACK);
                left_rotate(tree, parent);
                x = tree->root;
                parent = NULL;
            }
        } else {
            sibling = parent->left;
            if (node_color(sibling) == RB_RED) {
                set_color(sibling, RB_BLACK);
                set_color(parent, RB_RED);
                right_rotate(tree, parent);
                sibling = parent->left;
            }
            if (node_color(sibling == NULL ? NULL : sibling->right) == RB_BLACK &&
                node_color(sibling == NULL ? NULL : sibling->left) == RB_BLACK) {
                set_color(sibling, RB_RED);
                x = parent;
                parent = x->parent;
            } else {
                if (node_color(sibling == NULL ? NULL : sibling->left) == RB_BLACK) {
                    set_color(sibling == NULL ? NULL : sibling->right, RB_BLACK);
                    set_color(sibling, RB_RED);
                    if (sibling != NULL)
                        left_rotate(tree, sibling);
                    sibling = parent->left;
                }
                set_color(sibling, node_color(parent));
                set_color(parent, RB_BLACK);
                set_color(sibling == NULL ? NULL : sibling->left, RB_BLACK);
                right_rotate(tree, parent);
                x = tree->root;
                parent = NULL;
            }
        }
    }
    set_color(x, RB_BLACK);
}

static size_t GetCount(RedBlackTree *tree)
{
    return tree == NULL ? 0 : tree->count;
}

static unsigned GetFlags(RedBlackTree *tree)
{
    return tree == NULL ? 0 : tree->Flags;
}

static unsigned SetFlags(RedBlackTree *tree, unsigned flags)
{
    unsigned old;
    if (tree == NULL)
        return 0;
    old = tree->Flags;
    tree->Flags = flags;
    return old;
}

static RedBlackTree *Create(size_t element_size, size_t key_size)
{
    RedBlackTree *tree;
    if (element_size == 0 || key_size == 0 ||
        key_size > SIZE_MAX - offsetof(RedBlackTreeNode, key))
        return NULL;
    tree = CurrentAllocator->malloc(sizeof(*tree));
    if (tree == NULL)
        return NULL;
    memset(tree, 0, sizeof(*tree));
    tree->VTable = &iRedBlackTree;
    tree->ElementSize = element_size;
    tree->KeySize = key_size;
    tree->RaiseError = iError.EmptyErrorFunction;
    tree->KeyCompareFn = DefaultCompareFunction;
    tree->Allocator = CurrentAllocator;
    return tree;
}

static int DefaultCompareFunction(const void *left, const void *right,
                                  CompareInfo *extra)
{
    const RedBlackTree *tree = extra == NULL ? NULL : extra->ContainerLeft;
    if (left == NULL || right == NULL)
        return left == right ? 0 : (left == NULL ? -1 : 1);
    return tree == NULL ? 0 : memcmp(left, right, tree->KeySize);
}

static int Insert(RedBlackTree *tree, const void *key, const void *data,
                  void *extra)
{
    RedBlackTreeNode *parent = NULL;
    RedBlackTreeNode *current;
    RedBlackTreeNode *node;
    int comparison = 0;

    if (!valid_arguments(tree, key, data))
        return report_error(tree, "iRedBlackTree.Insert", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iRedBlackTree.Insert", CONTAINER_ERROR_READONLY);
    current = tree->root;
    while (current != NULL) {
        parent = current;
        comparison = compare_keys(tree, key, current->key, extra);
        if (comparison == 0)
            return -1;
        current = comparison < 0 ? current->left : current->right;
    }
    node = new_node(tree, key, data);
    if (node == NULL) {
        report_error(tree, "iRedBlackTree.Insert", CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    node->parent = parent;
    if (parent == NULL)
        tree->root = node;
    else if (comparison < 0)
        parent->left = node;
    else
        parent->right = node;
    insert_fixup(tree, node);
    ++tree->count;
    ++tree->timestamp;
    return 1;
}

static int Add(RedBlackTree *tree, const void *key, const void *data)
{
    return Insert(tree, key, data, NULL);
}

static int Remove(RedBlackTree *tree, const void *key, void *extra)
{
    RedBlackTreeNode *node;
    RedBlackTreeNode *replacement;
    RedBlackTreeNode *replacement_parent;
    RedBlackTreeNode *successor;
    unsigned char original_color;

    if (tree == NULL || key == NULL)
        return report_error(tree, "iRedBlackTree.Erase", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iRedBlackTree.Erase", CONTAINER_ERROR_READONLY);
    node = lookup_node(tree, key, extra);
    if (node == NULL)
        return 0;
    successor = node;
    original_color = successor->color;
    replacement = NULL;
    replacement_parent = NULL;
    if (node->left == NULL) {
        replacement = node->right;
        replacement_parent = node->parent;
        transplant(tree, node, node->right);
    } else if (node->right == NULL) {
        replacement = node->left;
        replacement_parent = node->parent;
        transplant(tree, node, node->left);
    } else {
        successor = minimum_node(node->right);
        original_color = successor->color;
        replacement = successor->right;
        if (successor->parent == node) {
            replacement_parent = successor;
            if (replacement != NULL)
                replacement->parent = successor;
        } else {
            replacement_parent = successor->parent;
            transplant(tree, successor, successor->right);
            successor->right = node->right;
            successor->right->parent = successor;
        }
        transplant(tree, node, successor);
        successor->left = node->left;
        successor->left->parent = successor;
        successor->color = node->color;
    }
    destroy_node(tree, node);
    --tree->count;
    ++tree->timestamp;
    if (original_color == RB_BLACK)
        delete_fixup(tree, replacement, replacement_parent);
    return 1;
}

static void *Find(RedBlackTree *tree, void *key, void *extra)
{
    RedBlackTreeNode *node;
    if (tree == NULL || key == NULL) {
        report_error(tree, "iRedBlackTree.Find", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    node = lookup_node(tree, key, extra);
    return node == NULL ? NULL : node->data;
}

static int visit_nodes(RedBlackTreeNode *node,
                       int (*fn)(const void *, void *), void *arg)
{
    if (node == NULL)
        return 1;
    if (!visit_nodes(node->left, fn, arg))
        return 0;
    if (!fn(node->data, arg))
        return 0;
    return visit_nodes(node->right, fn, arg);
}

static int Apply(RedBlackTree *tree,
                 int (*Applyfn)(const void *data, void *arg), void *arg)
{
    if (tree == NULL || Applyfn == NULL)
        return report_error(tree, "iRedBlackTree.Apply", CONTAINER_ERROR_BADARG);
    if (tree->root == NULL)
        return 0;
    return visit_nodes(tree->root, Applyfn, arg);
}

static int Clear(RedBlackTree *tree)
{
    if (tree == NULL)
        return report_error(NULL, "iRedBlackTree.Clear", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return report_error(tree, "iRedBlackTree.Clear", CONTAINER_ERROR_READONLY);
    destroy_nodes(tree, tree->root);
    tree->root = NULL;
    tree->count = 0;
    ++tree->timestamp;
    return 1;
}

static int Finalize(RedBlackTree *tree)
{
    ContainerAllocator *allocator;
    if (tree == NULL)
        return report_error(NULL, "iRedBlackTree.Finalize", CONTAINER_ERROR_BADARG);
    allocator = tree->Allocator == NULL ? CurrentAllocator : tree->Allocator;
    destroy_nodes(tree, tree->root);
    tree->root = NULL;
    tree->count = 0;
    allocator->free(tree);
    return 1;
}

static ErrorFunction SetErrorFunction(RedBlackTree *tree, ErrorFunction fn)
{
    ErrorFunction old;
    if (tree == NULL)
        return iError.RaiseError;
    old = tree->RaiseError;
    tree->RaiseError = fn == NULL ? iError.EmptyErrorFunction : fn;
    return old;
}

static CompareFunction SetCompareFunction(RedBlackTree *tree,
                                           CompareFunction fn)
{
    CompareFunction old;
    if (tree == NULL)
        return NULL;
    old = tree->KeyCompareFn;
    if (fn != NULL)
        tree->KeyCompareFn = fn;
    return old;
}

static size_t Sizeof(RedBlackTree *tree)
{
    size_t per_node;
    if (tree == NULL)
        return sizeof(RedBlackTree);
    per_node = offsetof(RedBlackTreeNode, key) + tree->KeySize +
               tree->ElementSize;
    if (tree->count > (SIZE_MAX - sizeof(*tree)) / per_node)
        return SIZE_MAX;
    return sizeof(*tree) + tree->count * per_node;
}

static size_t GetElementSize(RedBlackTree *tree)
{
    return tree == NULL ? 0 : tree->ElementSize;
}

static DestructorFunction SetDestructor(RedBlackTree *tree,
                                        DestructorFunction fn)
{
    DestructorFunction old;
    if (tree == NULL)
        return NULL;
    old = tree->DestructorFn;
    tree->DestructorFn = fn;
    return old;
}

static int iterator_stale(RedBlackTreeIterator *iterator, const char *operation)
{
    if (iterator == NULL || iterator->magic != RB_ITERATOR_MAGIC ||
        iterator->tree == NULL)
        return 1;
    if (iterator->timestamp != iterator->tree->timestamp) {
        report_error(iterator->tree, operation, CONTAINER_ERROR_OBJECT_CHANGED);
        return 1;
    }
    return 0;
}

static void *iterator_value(RedBlackTreeIterator *iterator)
{
    if (iterator->current == NULL)
        return NULL;
    if ((iterator->tree->Flags & CONTAINER_READONLY) &&
        iterator->tree->ElementSize != 0) {
        memcpy(iterator->buffer, iterator->current->data,
               iterator->tree->ElementSize);
        return iterator->buffer;
    }
    return iterator->current->data;
}

static void *IteratorGetFirst(Iterator *base)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    if (iterator_stale(iterator, "iRedBlackTree.GetFirst") ||
        iterator->tree->count == 0)
        return NULL;
    iterator->index = 0;
    iterator->current = node_at(iterator->tree->root, &iterator->index, 0);
    iterator->index = 0;
    return iterator_value(iterator);
}

static void *IteratorGetLast(Iterator *base)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    size_t index;
    if (iterator_stale(iterator, "iRedBlackTree.GetLast") ||
        iterator->tree->count == 0)
        return NULL;
    index = iterator->tree->count - 1;
    iterator->current = node_at(iterator->tree->root, &(size_t){0}, index);
    iterator->index = index;
    return iterator_value(iterator);
}

static void *IteratorGetCurrent(Iterator *base)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    if (iterator_stale(iterator, "iRedBlackTree.GetCurrent"))
        return NULL;
    return iterator_value(iterator);
}

static void *IteratorGetNext(Iterator *base)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    size_t index;
    if (iterator_stale(iterator, "iRedBlackTree.GetNext") ||
        iterator->current == NULL || iterator->index + 1 >= iterator->tree->count)
        return NULL;
    index = iterator->index + 1;
    iterator->current = node_at(iterator->tree->root, &(size_t){0}, index);
    iterator->index = index;
    return iterator_value(iterator);
}

static void *IteratorGetPrevious(Iterator *base)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    size_t index;
    if (iterator_stale(iterator, "iRedBlackTree.GetPrevious") ||
        iterator->current == NULL || iterator->index == 0)
        return NULL;
    index = iterator->index - 1;
    iterator->current = node_at(iterator->tree->root, &(size_t){0}, index);
    iterator->index = index;
    return iterator_value(iterator);
}

static void *IteratorSeek(Iterator *base, size_t index)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    if (iterator_stale(iterator, "iRedBlackTree.Seek") ||
        index >= iterator->tree->count)
        return NULL;
    iterator->current = node_at(iterator->tree->root, &(size_t){0}, index);
    iterator->index = index;
    return iterator_value(iterator);
}

static size_t IteratorGetPosition(Iterator *base)
{
    RedBlackTreeIterator *iterator = (RedBlackTreeIterator *)base;
    if (iterator_stale(iterator, "iRedBlackTree.GetPosition"))
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

static Iterator *NewIterator(RedBlackTree *tree)
{
    RedBlackTreeIterator *iterator;
    if (tree == NULL) {
        report_error(NULL, "iRedBlackTree.NewIterator", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    iterator = tree->Allocator->calloc(1, sizeof(*iterator));
    if (iterator == NULL) {
        report_error(tree, "iRedBlackTree.NewIterator", CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    if ((tree->Flags & CONTAINER_READONLY) && tree->ElementSize != 0) {
        iterator->buffer = tree->Allocator->malloc(tree->ElementSize);
        if (iterator->buffer == NULL) {
            tree->Allocator->free(iterator);
            report_error(tree, "iRedBlackTree.NewIterator", CONTAINER_ERROR_NOMEMORY);
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
    iterator->magic = RB_ITERATOR_MAGIC;
    iterator->tree = tree;
    iterator->Allocator = tree->Allocator;
    iterator->timestamp = tree->timestamp;
    iterator->index = (size_t)-1;
    return &iterator->it;
}

static int DeleteIterator(Iterator *base)
{
    RedBlackTreeIterator *iterator;
    if (base == NULL)
        return CONTAINER_ERROR_BADARG;
    iterator = (RedBlackTreeIterator *)base;
    if (iterator->magic != RB_ITERATOR_MAGIC)
        return CONTAINER_ERROR_WRONG_ITERATOR;
    if (iterator->buffer != NULL)
        iterator->Allocator->free(iterator->buffer);
    iterator->magic = 0;
    iterator->Allocator->free(iterator);
    return 1;
}

RedBlackTreeInterface iRedBlackTree = {
    Create,
    GetElementSize,
    GetCount,
    GetFlags,
    SetFlags,
    Add,
    Insert,
    Clear,
    Remove,
    Finalize,
    Apply,
    Find,
    SetErrorFunction,
    SetCompareFunction,
    Sizeof,
    DefaultCompareFunction,
    NewIterator,
    DeleteIterator,
    SetDestructor
};
