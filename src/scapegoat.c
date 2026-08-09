#include "containers.h"
#include "ccl_internal.h"


static const guid TreeMapGuid = {0xb8fda2f4, 0x2d4b, 0x4033,
{0xa6,0x49,0x31,0x63,0x48,0x9f,0x27,0x18}
};

#include <limits.h>
#ifndef _MSC_VER
#include <stdbool.h>
#include <stdint.h>
#else
#include "stdint.h"
#if _MSC_VER <= 1500
#define inline
#endif
#endif

static void rebalance_subtree (TreeMap *, struct Node *, size_t);
static struct Node **down_link (TreeMap *, struct Node *);
static struct Node *sibling (struct Node *p);
static size_t count_nodes_in_subtree (const struct Node *);

static int floor_log2 (size_t);
static size_t calculate_h_alpha (size_t);
static TreeMap *CreateWithAllocator(size_t ElementSize,const ContainerAllocator *m);


/* Inserts the given NODE into BT.
   Returns a null pointer if successful.
   Returns the existing node already in BT equal to NODE, on
   failure. */
static struct Node *insert(TreeMap *bt, struct Node *node,
                           CompareInfo *compare_info)
{
  size_t depth = 0;

  node->down[0] = NULL;
  node->down[1] = NULL;

  if (bt->root == NULL) {
      bt->root = node;
      node->up = NULL;
    }
  else {
      struct Node *p = bt->root;
      for (;;) {
          int cmp, dir;

          cmp = bt->compare(node->data, p->data, compare_info);
          if (cmp == 0)
            return p;
          depth++;

          dir = cmp > 0;
          if (p->down[dir] == NULL)
            {
              p->down[dir] = node;
              node->up = p;
              break;
            }
          p = p->down[dir];
        }
    }

  bt->count++;
  if (bt->count > bt->max_size)
    bt->max_size = bt->count;
  if (depth > calculate_h_alpha (bt->count)) {
      /* We use the "alternative" method of finding a scapegoat
         node described by Galperin and Rivest. */
      struct Node *s = node;
      size_t size = 1;
      size_t i;

      for (i = 1; ; i++)
        if (i < depth) {
            size += 1 + count_nodes_in_subtree (sibling (s));
            s = s->up;
            if (i > calculate_h_alpha (size)) {
                rebalance_subtree (bt, s, size);
                break;
              }
          }
        else {
            rebalance_subtree (bt, bt->root, bt->count);
            bt->max_size = bt->count;
            break;
          }
    }
    bt->timestamp++;
  return NULL;
}

/* Deletes P from BT. */
static void Delete(TreeMap *bt, struct Node *p)
{
  if (bt == NULL || p == NULL)
    return;
  struct Node **q = down_link (bt, p);
  struct Node *r = p->down[1];
  if (r == NULL) {
      *q = p->down[0];
      if (*q)
        (*q)->up = p->up;
    }
  else if (r->down[0] == NULL) {
      r->down[0] = p->down[0];
      *q = r;
      r->up = p->up;
      if (r->down[0] != NULL)
        r->down[0]->up = r;
    }
  else {
      struct Node *s = r->down[0];
      while (s->down[0] != NULL)
        s = s->down[0];
      r = s->up;
      r->down[0] = s->down[1];
      s->down[0] = p->down[0];
      s->down[1] = p->down[1];
      *q = s;
      if (s->down[0] != NULL)
        s->down[0]->up = s;
      s->down[1]->up = s;
      s->up = p->up;
      if (r->down[0] != NULL)
        r->down[0]->up = r;
    }
  bt->count--;

  /* We approximate .707 as .75 here.  This is conservative: it
     will cause us to do a little more rebalancing than strictly
     necessary to maintain the scapegoat tree's height
     invariant. */
  if (bt->count < bt->max_size * 3 / 4 && bt->count > 0)
    {
      rebalance_subtree (bt, bt->root, bt->count);
      bt->max_size = bt->count;
    }
	if (bt->DestructorFn)
		bt->DestructorFn(p->data);
    iHeap.FreeObject(bt->Heap,p);
    bt->timestamp++;
}

/* Returns the node with minimum value in BT, or a null pointer
   if BT is empty. */
static struct Node *bt_first (const TreeMap *bt)
{
  struct Node *p = bt->root;
  if (p != NULL)
    while (p->down[0] != NULL)
      p = p->down[0];
  return p;
}

/* Returns the node with maximum value in BT, or a null pointer
   if BT is empty. */
static struct Node *bt_last (const TreeMap *bt)
{
  struct Node *p = bt->root;
  if (p != NULL)
    while (p->down[1] != NULL)
      p = p->down[1];
  return p;
}
/* Searches BT for a node equal to TARGET.
   Returns the node if found, or a null pointer otherwise. */
static struct Node *find (const TreeMap *bt, const void *data,
                          CompareInfo *compare_info)
{
  const struct Node *p;
  int cmp;

  for (p = bt->root; p != NULL; p = p->down[cmp > 0])
    {
      cmp = bt->compare (data, p->data, compare_info);
      if (cmp == 0)
        return (struct Node *) p;
    }

  return NULL;
}

/* Returns the node in BT following P in in-order.
   If P is null, returns the minimum node in BT.
   Returns a null pointer if P is the maximum node in BT or if P
   is null and BT is empty. */
static struct Node *bt_next (const TreeMap *bt, const struct Node *p)
{
  if (p == NULL)
    return bt_first (bt);
  else if (p->down[1] == NULL)
    {
      struct Node *q;
      for (q = p->up; ; p = q, q = q->up)
        if (q == NULL || p == q->down[0])
          return q;
    }
  else
    {
      p = p->down[1];
      while (p->down[0] != NULL)
        p = p->down[0];
      return (struct Node *)p;
    }
}

/* Returns the node in BT preceding P in in-order.
   If P is null, returns the maximum node in BT.
   Returns a null pointer if P is the minimum node in BT or if P
   is null and BT is empty. */
static struct Node *bt_prev (const TreeMap *bt, const struct Node *p)
{
  if (p == NULL)
    return bt_last (bt);
  else if (p->down[0] == NULL)
    {
      struct Node *q;
      for (q = p->up; ; p = q, q = q->up)
        if (q == NULL || p == q->down[1])
          return q;
    }
  else
    {
      p = p->down[0];
      while (p->down[1] != NULL)
        p = p->down[1];
      return (struct Node *)p;
    }
}

/* Tree rebalancing.

   This algorithm is from Q. F. Stout and B. L. Warren, "Tree
   Rebalancing in Optimal Time and Space", CACM 29(1986):9,
   pp. 902-908.  It uses O(N) time and O(1) space to rebalance a
   subtree that contains N nodes. */

static void tree_to_vine (struct Node **);
static void vine_to_tree (struct Node **, size_t count);

/* Rebalances the subtree in BT rooted at SUBTREE, which contains
   exactly COUNT nodes. */
static void rebalance_subtree (TreeMap *bt, struct Node *subtree, size_t count)
{
    if (subtree == NULL) {
        iError.RaiseError("rebalance_subtree",CONTAINER_ERROR_BADARG);
    }
    else {
  struct Node *up = subtree->up;
  struct Node **q = down_link (bt, subtree);
  tree_to_vine (q);
  vine_to_tree (q, count);
  if (q) (*q)->up = up;
  else {
      iError.RaiseError("rebalance_subtree",CONTAINER_INTERNAL_ERROR);
  }
    }
}

/* Converts the subtree rooted at *Q into a vine (a binary search
   tree in which all the right links are null), and updates *Q to
   point to the new root of the subtree. */
static void tree_to_vine (struct Node **q)
{
  struct Node *p = *q;
  while (p != NULL)
    if (p->down[1] == NULL)
      {
        q = &p->down[0];
        p = *q;
      }
    else
      {
        struct Node *r = p->down[1];
        p->down[1] = r->down[0];
        r->down[0] = p;
        p = r;
        *q = r;
      }
}

/* Performs a compression transformation COUNT times, starting at
   *Q, and updates *Q to point to the new root of the subtree. */
static void compress (struct Node **q, size_t count)
{
  while (count--)
    {
      struct Node *red = *q;
      struct Node *black = red->down[0];

      *q = black;
      red->down[0] = black->down[1];
      black->down[1] = red;
      red->up = black;
      if (red->down[0] != NULL)
        red->down[0]->up = red;
      q = &black->down[0];
    }
}

/* Converts the vine rooted at *Q, which contains exactly COUNT
   nodes, into a balanced tree, and updates *Q to point to the
   new root of the balanced tree. */
static void vine_to_tree (struct Node **q, size_t count)
{
  size_t leaf_nodes;
  size_t count_plus_one;

  if (q == NULL || *q == NULL || count == 0)
    return;
  if (count == SIZE_MAX)
    return;
  count_plus_one = count + 1;
  leaf_nodes = count_plus_one -
               (((size_t)1) << floor_log2 (count_plus_one));
  size_t vine_nodes = count - leaf_nodes;

  compress (q, leaf_nodes);
  while (vine_nodes > 1)
    {
      vine_nodes /= 2;
      compress (q, vine_nodes);
    }
  while ((*q)->down[0] != NULL)
    {
      (*q)->down[0]->up = *q;
      q = &(*q)->down[0];
    }
}

static int Equal(TreeMap *t1,TreeMap *t2)
{
    struct Node *pt1,*pt2;
    CompareInfo cInfo;
    if (t1 == t2)
    	return 1;
    if (t1 == NULL || t2 == NULL)
    	return  0;
    if (t1->count != t2->count)
    	return 0;
    if (t1->Allocator != t2->Allocator)
    	return 0;
    if (t1->compare != t2->compare)
    	return 0;
    if (t1->ElementSize != t2->ElementSize)
    	return 0;
    if (t1->Flags != t2->Flags)
    	return 0;
    if (t1->compare == NULL)
        return 0;
    cInfo.ExtraArgs = NULL;
    cInfo.ContainerLeft = t1;
    cInfo.ContainerRight = t2;
    pt1 = bt_first(t1);
    pt2 = bt_first(t2);
    while (pt1 && pt2) {
        if (t1->compare(pt1->data,pt2->data,&cInfo))
    		break;
    	pt1 = bt_next(t1,pt1);
    	pt2 = bt_next(t2,pt2);
    }
    return pt1 == NULL && pt2 == NULL;
}

static TreeMap *Copy(TreeMap *src)
{
    TreeMap *result;
    struct Node *pSrc;

    if (src == NULL) {
    	iError.RaiseError("Copy",CONTAINER_ERROR_BADARG);
    	return NULL;
    }
    pSrc = bt_first(src);
    result = CreateWithAllocator(src->ElementSize,src->Allocator);
    if (result == NULL)
        return NULL;
    result->compare = src->compare;
    while (pSrc) {
	    if (iTreeMap.Add(result,pSrc->data,NULL) < 0) {
            iTreeMap.Finalize(result);
            return NULL;
        }
	    pSrc = bt_next(src,pSrc);
    }
    result->Flags = src->Flags;
    return result;
}

/* Other binary tree helper functions. */

/* Returns the address of the pointer that points down to P
   within BT. */
static struct Node **down_link (TreeMap *bt, struct Node *p)
{
  struct Node *q = p->up;
  return q != NULL ? &q->down[q->down[0] != p] : &bt->root;
}

/* Returns node P's sibling; that is, the other child of its
   parent.  P must not be the root. */
static struct Node *sibling (struct Node *p)
{
  struct Node *q = p->up;
  return q->down[q->down[0] == p];
}

/* Returns the number of nodes in the given SUBTREE. */
/* This is an in-order traversal modified to iterate only the
 nodes in SUBTREE. */
static size_t count_nodes_in_subtree (const struct Node *subtree)
{
    size_t count;
    const struct Node *p;

    if (subtree == NULL)
    	return 0;
    count = 0;
    p = subtree;
    while (p->down[0] != NULL)
        p = p->down[0];
    for (;;) {
    	count++;
    	if (p->down[1] != NULL) {
    		p = p->down[1];
    		while (p->down[0] != NULL)
    			p = p->down[0];
    	}
    	else {
    		for (;;) {
    			const struct Node *q;
    			if (p == subtree)
    				goto done;
    			q = p;
    			p = p->up;
    			if (p->down[0] == q)
    				break;
    		}
    	}
    }
 done:
  return count;
}

static size_t Size(TreeMap *tree)
{
    return tree != NULL ? tree->count : 0;
}
/* Arithmetic. */

/* Returns the number of high-order 0-bits in X.
   Undefined if X is zero. */
static size_t count_leading_zeros (size_t x)
{
  /* This algorithm is from _Hacker's Delight_ section 5.3. */
  size_t y;
  size_t n;

#define COUNT_STEP(BITS) y = x >> BITS; if (y != 0){n -= BITS; x = y; }

  n = sizeof (size_t) * CHAR_BIT;
#if SIZE_MAX >> 31 >> 31 >> 2
  COUNT_STEP (64);
#endif
#if SIZE_MAX >> 31 >> 1
  COUNT_STEP (32);
#endif
  COUNT_STEP (16);
  COUNT_STEP (8);
  COUNT_STEP (4);
  COUNT_STEP (2);
  y = x >> 1;
  return y != 0 ? n - 2 : n - x;
}

/* Returns floor(log2(x)).
   Undefined if X is zero. */
static int floor_log2 (size_t x)
{
  return sizeof (size_t) * CHAR_BIT - 1 - count_leading_zeros (x);
}

/* Returns floor(pow(sqrt(2), x * 2 + 1)).
   Defined for X from 0 up to the number of bits in size_t minus
   1. */
static size_t pow_sqrt2 (int x)
{
  /* These constants are sqrt(2) multiplied by 2**63 or 2**31,
     respectively, and then rounded to nearest. */
#if SIZE_MAX >> 31 >> 1
  return (UINT64_C(0xb504f333f9de6484) >> (63 - x)) + 1;
#else
  return (0xb504f334 >> (31 - x)) + 1;
#endif
}

/* Returns floor(log(n)/log(sqrt(2))).
   Undefined if N is 0. */
static size_t calculate_h_alpha (size_t n)
{
  size_t log2 = floor_log2 (n);

  /* The correct answer is either 2 * log2 or one more.  So we
     see if n >= pow(sqrt(2), 2 * log2 + 1) and if so, add 1. */
  return (2 * log2) + (n >= pow_sqrt2 (log2));
}

static unsigned GetFlags(TreeMap *t)
{
    return t != NULL ? t->Flags : 0;
}

static unsigned SetFlags(TreeMap *t,unsigned newFlags)
{
    if (t == NULL)
        return 0;
    unsigned oldFlags = t->Flags;
    t->Flags = newFlags;
    return oldFlags;
}

static int tree_error(TreeMap *tree, const char *operation, int code)
{
    ErrorFunction fn = tree != NULL ? tree->RaiseError : iError.RaiseError;
    if (fn != NULL)
        fn(operation, code);
    return code;
}

static int valid_tree_data(const TreeMap *tree, const void *data)
{
    return tree != NULL && (tree->ElementSize == 0 || data != NULL);
}

static void init_compare_info(CompareInfo *info, const TreeMap *left,
                              const TreeMap *right, void *extra)
{
    info->ExtraArgs = extra;
    info->ContainerLeft = left;
    info->ContainerRight = right;
}

static int Add(TreeMap *tree, void *Data,void *ExtraArgs)
{
    struct Node *p;
    CompareInfo cInfo;

    if (!valid_tree_data(tree, Data))
        return tree_error(tree, "TreeMap.Add", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return tree_error(tree, "TreeMap.Add", CONTAINER_ERROR_READONLY);
    init_compare_info(&cInfo, tree, tree, ExtraArgs);
    p = iHeap.NewObject(tree->Heap);
    if (p) {
        if (tree->ElementSize != 0)
            memcpy(p->data, Data, tree->ElementSize);
    }
    else {
	    tree_error(tree, "TreeMap.Add",CONTAINER_ERROR_NOMEMORY);
    	return CONTAINER_ERROR_NOMEMORY;
    }
    if (insert(tree, p, &cInfo) != NULL)
        iHeap.FreeObject(tree->Heap, p);
    return 1;
}

static int AddRange(TreeMap *tree,size_t n, void *Data,void *ExtraArgs)
{
    struct Node *p;
    CompareInfo cInfo;
	unsigned char *cursor = (unsigned char *)Data;

	if (tree == NULL || (n != 0 && !valid_tree_data(tree, Data)))
		return tree_error(tree, "TreeMap.AddRange", CONTAINER_ERROR_BADARG);
	if (tree->Flags & CONTAINER_READONLY)
		return tree_error(tree, "TreeMap.AddRange", CONTAINER_ERROR_READONLY);
	init_compare_info(&cInfo, tree, tree, ExtraArgs);
	while (n > 0) {
		p = iHeap.NewObject(tree->Heap);
		if (p) {
			if (tree->ElementSize != 0)
				memcpy(p->data, cursor, tree->ElementSize);
		}
		else {
			tree_error(tree, "TreeMap.AddRange",CONTAINER_ERROR_NOMEMORY);
			return CONTAINER_ERROR_NOMEMORY;
		}
		if (insert(tree, p, &cInfo) != NULL)
			iHeap.FreeObject(tree->Heap, p);
		if (tree->ElementSize != 0)
			cursor += tree->ElementSize;
		n--;
	}
    return 1;
}


static int Insert(TreeMap *tree, const void *Data, void *ExtraArgs)
{
    struct Node *p;
    CompareInfo cInfo;

    if (!valid_tree_data(tree, Data))
        return tree_error(tree, "TreeMap.Insert", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return tree_error(tree, "TreeMap.Insert", CONTAINER_ERROR_READONLY);
    init_compare_info(&cInfo, tree, tree, ExtraArgs);
    p = iHeap.NewObject(tree->Heap);
    if (p == NULL)
	{
	    tree_error(tree, "TreeMap.Insert", CONTAINER_ERROR_NOMEMORY);
    	return 0;
	}
    if (tree->ElementSize != 0)
	    memcpy(p->data, Data, tree->ElementSize);
    {
        struct Node *existing = insert(tree, p, &cInfo);
        if (existing != NULL) {
	        if (tree->DestructorFn)
	        tree->DestructorFn(existing->data);
	        if (tree->ElementSize != 0)
	        memcpy(existing->data, Data, tree->ElementSize);
	        iHeap.FreeObject(tree->Heap, p);
	        tree->timestamp++;
    	return 1;
        }
    }
    return 0;

}

static void *GetElement(TreeMap *tree,const void *data,void *ExtraArgs)
{
    struct Node *p;
    CompareInfo cInfo;

    if (!valid_tree_data(tree, data)) {
        tree_error(tree, "TreeMap.GetElement", CONTAINER_ERROR_BADARG);
        return NULL;
    }
    init_compare_info(&cInfo, tree, tree, ExtraArgs);
    p = find(tree, data, &cInfo);
    if (p) {
    	return p->data;
    }
    return NULL;
}

static int Erase(TreeMap *tree, const void * element,void *ExtraArgs)
{
    struct Node *n;
    CompareInfo cInfo;
	
    if (!valid_tree_data(tree, element))
        return tree_error(tree, "TreeMap.Erase", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return tree_error(tree, "TreeMap.Erase", CONTAINER_ERROR_READONLY);
    init_compare_info(&cInfo, tree, tree, ExtraArgs);
    n = find(tree, element, &cInfo);
    if (n == NULL)
    	return 0;
    Delete(tree,n);
    return 1;
}

#define TREEMAP_ITERATOR_BORROWED 1UL

static int iterator_stale(struct TreeMapIterator *trav, const char *operation)
{
    if (trav == NULL || trav->bst_table == NULL)
        return tree_error(NULL, operation, CONTAINER_ERROR_BADARG);
    if (trav->timestamp != trav->bst_table->timestamp) {
        tree_error(trav->bst_table, operation, CONTAINER_ERROR_OBJECT_CHANGED);
        return CONTAINER_ERROR_OBJECT_CHANGED;
    }
    return 0;
}

/* Returns the next data item in inorder within the tree being traversed. */
static void *GetNext(Iterator *itrav)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)itrav;
    if (iterator_stale(trav, "GetNext"))
        return NULL;
    trav->bst_node = bt_next(trav->bst_table, trav->bst_node);
    return trav->bst_node != NULL ? trav->bst_node->data : NULL;
}

static void *GetPrevious(Iterator *itrav)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)itrav;
    if (iterator_stale(trav, "GetPrevious"))
        return NULL;
    trav->bst_node = bt_prev(trav->bst_table, trav->bst_node);
    return trav->bst_node != NULL ? trav->bst_node->data : NULL;
}

static void *GetFirst(Iterator *itrav)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)itrav;
    if (iterator_stale(trav, "GetFirst"))
        return NULL;
    trav->bst_node = bt_first(trav->bst_table);
    return trav->bst_node != NULL ? trav->bst_node->data : NULL;
}

static void *GetLast(Iterator *itrav)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)itrav;
    if (iterator_stale(trav, "GetLast"))
        return NULL;
    trav->bst_node = bt_last(trav->bst_table);
    return trav->bst_node != NULL ? trav->bst_node->data : NULL;
}

static void *GetCurrent(Iterator *it)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)it;
    if (iterator_stale(trav, "GetCurrent"))
        return NULL;
    return trav->bst_node != NULL ? trav->bst_node->data : NULL;
}

static void *SeekIterator(Iterator *it, size_t position)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)it;
    size_t i;
    if (iterator_stale(trav, "Seek") || position >= trav->bst_table->count)
        return NULL;
    trav->bst_node = bt_first(trav->bst_table);
    for (i = 0; i < position && trav->bst_node != NULL; ++i)
        trav->bst_node = bt_next(trav->bst_table, trav->bst_node);
    return trav->bst_node != NULL ? trav->bst_node->data : NULL;
}

static size_t GetPosition(Iterator *it)
{
    struct TreeMapIterator *trav = (struct TreeMapIterator *)it;
    struct Node *node;
    size_t position = 0;
    if (iterator_stale(trav, "GetPosition"))
        return (size_t)-1;
    if (trav->bst_node == NULL)
        return (size_t)-1;
    node = bt_first(trav->bst_table);
    while (node != NULL && node != trav->bst_node) {
        ++position;
        node = bt_next(trav->bst_table, node);
    }
    return node == trav->bst_node ? position : (size_t)-1;
}

static int ReplaceWithIterator(Iterator *it, void *data, int direction)
{
    struct TreeMapIterator *li = (struct TreeMapIterator *)it;
    TreeMap *tree;
    struct Node *pos;
    struct Node *existing;
    struct Node *replacement;
    CompareInfo cInfo;
    void *owned_data = NULL;
    (void)direction;

    if (li == NULL || li->bst_table == NULL)
        return tree_error(NULL, "Replace", CONTAINER_ERROR_BADARG);
    tree = li->bst_table;
    if (iterator_stale(li, "Replace"))
        return CONTAINER_ERROR_OBJECT_CHANGED;
    if (tree->Flags & CONTAINER_READONLY)
        return tree_error(tree, "Replace", CONTAINER_ERROR_READONLY);
    if (li->bst_node == NULL)
        return tree_error(tree, "Replace", CONTAINER_ERROR_BADARG);
    if (data != NULL && !valid_tree_data(tree, data))
        return tree_error(tree, "Replace", CONTAINER_ERROR_BADARG);
    pos = li->bst_node;
    if (data == NULL) {
        li->bst_node = bt_next(tree, pos);
        Delete(tree, pos);
        li->timestamp = tree->timestamp;
        return 1;
    }

    /* A borrowed pointer to the current value must survive its destructor
       when replacement is used with an owning payload. */
    if (data == pos->data && tree->ElementSize != 0) {
        owned_data = tree->Allocator->malloc(tree->ElementSize);
        if (owned_data == NULL)
            return tree_error(tree, "Replace", CONTAINER_ERROR_NOMEMORY);
        memcpy(owned_data, data, tree->ElementSize);
        data = owned_data;
    }

    init_compare_info(&cInfo, tree, tree, NULL);
    existing = find(tree, data, &cInfo);
    if (existing == pos) {
        if (tree->DestructorFn)
            tree->DestructorFn(pos->data);
        if (tree->ElementSize != 0)
            memcpy(pos->data, data, tree->ElementSize);
        if (owned_data != NULL)
            tree->Allocator->free(owned_data);
        tree->timestamp++;
        li->timestamp = tree->timestamp;
        return 1;
    }
    if (existing != NULL) {
        li->bst_node = bt_next(tree, pos);
        Delete(tree, pos);
        if (owned_data != NULL)
            tree->Allocator->free(owned_data);
        li->timestamp = tree->timestamp;
        return 1;
    }
    replacement = iHeap.NewObject(tree->Heap);
    if (replacement == NULL) {
        if (owned_data != NULL)
            tree->Allocator->free(owned_data);
        return tree_error(tree, "Replace", CONTAINER_ERROR_NOMEMORY);
    }
    if (tree->ElementSize != 0)
        memcpy(replacement->data, data, tree->ElementSize);
    Delete(tree, pos);
    if (insert(tree, replacement, &cInfo) != NULL) {
        iHeap.FreeObject(tree->Heap, replacement);
        li->bst_node = NULL;
    } else {
        li->bst_node = bt_next(tree, replacement);
    }
    if (owned_data != NULL)
        tree->Allocator->free(owned_data);
    li->timestamp = tree->timestamp;
    return 1;
}

static void initialize_iterator(struct TreeMapIterator *result,
                                TreeMap *tree, unsigned long flags)
{
    memset(result, 0, sizeof(*result));
    result->it.GetNext = GetNext;
    result->it.GetPrevious = GetPrevious;
    result->it.GetFirst = GetFirst;
    result->it.GetLast = GetLast;
    result->it.GetCurrent = GetCurrent;
    result->it.Seek = SeekIterator;
    result->it.GetPosition = GetPosition;
    result->it.Replace = ReplaceWithIterator;
    result->bst_table = tree;
    result->timestamp = tree->timestamp;
    result->Flags = flags;
}

static Iterator *NewIterator(TreeMap *tree)
{
    struct TreeMapIterator *result;
    if (tree == NULL)
        return NULL;
    result = tree->Allocator->malloc(sizeof(*result));
    if (result == NULL)
        return NULL;
    initialize_iterator(result, tree, 0);
    return &result->it;
}

static int InitIterator(TreeMap *tree,void *buf)
{
    struct TreeMapIterator *result = buf;
    if (tree == NULL || result == NULL)
        return tree_error(tree, "InitIterator", CONTAINER_ERROR_BADARG);
    initialize_iterator(result, tree, TREEMAP_ITERATOR_BORROWED);
    return 1;
}

static int DeleteIterator(Iterator *it)
{
    struct TreeMapIterator *itbb = (struct TreeMapIterator *)it;
    if (itbb == NULL || itbb->bst_table == NULL)
        return CONTAINER_ERROR_BADARG;
    if (!(itbb->Flags & TREEMAP_ITERATOR_BORROWED))
        itbb->bst_table->Allocator->free(it);
    return 1;
}

static size_t SizeofIterator(TreeMap *tree)
{
	(void)tree;
	return sizeof(struct TreeMapIterator);
}
static CompareFunction SetCompareFunction(TreeMap *l,CompareFunction fn)
{
    CompareFunction oldfn;

    if (l == NULL)
        return NULL;
    oldfn = l->compare;

    if (fn != NULL && l->count == 0)
    	l->compare = fn;
    else if (fn != NULL && l->count != 0)
        tree_error(l, "SetCompareFunction", CONTAINER_ERROR_NOT_EMPTY);
    return oldfn;
}

static ErrorFunction SetErrorFunction(TreeMap *tree,ErrorFunction fn)
{
    ErrorFunction old;
    if (tree == NULL) return iError.RaiseError;
    old = tree->RaiseError;
    tree->RaiseError = (fn) ? fn : iError.EmptyErrorFunction;
    return old;
}

static size_t Sizeof(TreeMap *tree)
{
    size_t result;
    size_t stride;
    if (tree == NULL)
        return sizeof(TreeMap);
    stride = sizeof(struct Node);
    if (tree->ElementSize > SIZE_MAX - stride)
        return (size_t)CONTAINER_ERROR_NOMEMORY;
    stride += tree->ElementSize;
    if (tree->count != 0 && stride > (SIZE_MAX - sizeof(TreeMap)) / tree->count)
        return (size_t)CONTAINER_ERROR_NOMEMORY;
    result = sizeof(TreeMap) + tree->count * stride;
    return result;
}

static int Clear(TreeMap *tree)
{
    struct Node *node;
    if (tree == NULL)
        return tree_error(NULL, "iTree.Clear", CONTAINER_ERROR_BADARG);
    if (tree->Flags & CONTAINER_READONLY)
        return tree_error(tree, "iTree.Clear", CONTAINER_ERROR_READONLY);
    if (tree->DestructorFn) {
        node = bt_first(tree);
        while (node != NULL) {
            tree->DestructorFn(node->data);
            node = bt_next(tree, node);
        }
    }
    iHeap.Clear( tree->Heap);
    /* heap.Clear historically leaves its free-list pointer stale; the tree
       may be reused after Clear, so discard that pointer before the next
       allocation. */
    tree->Heap->FreeList = NULL;
    tree->count = 0;
    tree->root = NULL;
    tree->max_size=0;            /* Max size since last complete rebalance. */
    tree->aux = NULL;
    tree->timestamp++;
    return 1;
}

static int Finalize(TreeMap *tree)
{
    if (tree == NULL) return CONTAINER_ERROR_BADARG;
    if (tree->DestructorFn) {
        struct Node *node = bt_first(tree);
        while (node != NULL) {
            tree->DestructorFn(node->data);
            node = bt_next(tree, node);
        }
    }
    if (tree->Heap != NULL) {
        iHeap.Clear(tree->Heap);
        iHeap.Finalize(tree->Heap);
    }
    tree->Allocator->free(tree);
    return 1;
}

static int Apply(TreeMap *tree,int (*Applyfn)(const void *data,void *arg),void *arg)
{
    if (tree == NULL || Applyfn == NULL)
        return tree_error(tree, "iTree.Apply", CONTAINER_ERROR_BADARG);
    Iterator *it = NewIterator(tree);
    void *obj;

    if (it == NULL) {
        iError.RaiseError("iTree.Apply",CONTAINER_ERROR_NOMEMORY);
        return CONTAINER_ERROR_NOMEMORY;
    }
    for (obj = it->GetFirst(it);
    	 obj != NULL;
    	 obj = it->GetNext(it)) {
    	Applyfn(obj,arg);
    }
    DeleteIterator(it);
    return 1;
}

static int DefaultTreeCompareFunction(const void *left,const void *right,CompareInfo *ExtraArgs)
{
    size_t siz;
    if (ExtraArgs == NULL || ExtraArgs->ContainerLeft == NULL) return 0;
    siz=((TreeMap *)ExtraArgs->ContainerLeft)->ElementSize;
    if (siz == 0)
        return 0;
    return memcmp(left,right,siz);
}

static size_t GetElementSize(TreeMap *d)
{
    if (d == NULL) return (size_t)CONTAINER_ERROR_BADARG;
    return d->ElementSize;
}

static int Contains(TreeMap *d, void *element,void *ExtraArgs)
{
    if (GetElement(d,element,ExtraArgs))
    	return 1;
    return 0;
}

static TreeMap *CreateWithAllocator(size_t ElementSize,const ContainerAllocator *m)
{
    TreeMap *result;
    size_t node_size;

    if (m == NULL)
    	m = CurrentAllocator;
    if (m == NULL) return NULL;
    if (ElementSize > SIZE_MAX - sizeof(struct Node))
        return NULL;
    node_size = sizeof(struct Node) + ElementSize;
    if (node_size > SIZE_MAX - (sizeof(void *) - 1))
        return NULL;
    result = m->malloc(sizeof(*result));
    if (result == NULL)
    	return NULL;
    memset(result,0,sizeof(*result));
    result->VTable = &iTreeMap;
    result->RaiseError = iError.RaiseError;
    result->compare = DefaultTreeCompareFunction;
    result->Heap = iHeap.Create(roundup(node_size),m);
    if (result->Heap == NULL) {
        m->free(result);
        return NULL;
    }
    result->Allocator = m;
    result->ElementSize = ElementSize;
    return result;
}

static TreeMap *Create(size_t ElementSize)
{
    return CreateWithAllocator(ElementSize,CurrentAllocator);
}

static TreeMap *InitializeWith(size_t ElementSize, size_t n, void *data)
{
	TreeMap *result = Create(ElementSize);
	unsigned char *p = data;

	if (result == NULL) return NULL;
	if (ElementSize != 0 && n != 0 && data == NULL) {
            iTreeMap.Finalize(result);
            return NULL;
    }
	while (n-- > 0) {
		if (Add(result,p,NULL) < 0) {
			iTreeMap.Finalize(result);
			return NULL;
		}
		if (ElementSize != 0)
			p += ElementSize;
	}
	return result;
}

#define TREEMAP_FILE_VERSION 1U
#define TREEMAP_FILE_HEADER_SIZE 32U

static void store_u32le(unsigned char *dst, uint32_t value)
{
    dst[0] = (unsigned char)value;
    dst[1] = (unsigned char)(value >> 8);
    dst[2] = (unsigned char)(value >> 16);
    dst[3] = (unsigned char)(value >> 24);
}

static void store_u64le(unsigned char *dst, uint64_t value)
{
    unsigned i;
    for (i = 0; i < 8; ++i)
        dst[i] = (unsigned char)(value >> (i * 8));
}

static uint32_t load_u32le(const unsigned char *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static uint64_t load_u64le(const unsigned char *src)
{
    uint64_t result = 0;
    unsigned i;
    for (i = 0; i < 8; ++i)
        result |= (uint64_t)src[i] << (i * 8);
    return result;
}

static int DefaultSaveFunction(const void *element,void *arg, FILE *Outfile)
{
    const unsigned char *str = element;
    size_t len = *(size_t *)arg;
    return len == fwrite(str,1,len,Outfile);
}

static int Save(const TreeMap *src,FILE *stream, SaveFunction saveFn,void *arg)
{
    struct Node *rvp;
    size_t elemsiz;
    unsigned char header[TREEMAP_FILE_HEADER_SIZE];
    if (src == NULL)
        return tree_error(NULL, "Save", CONTAINER_ERROR_BADARG);
    if (stream == NULL)
        return tree_error((TreeMap *)src, "Save", CONTAINER_ERROR_BADARG);
    if (saveFn == NULL)
        saveFn = DefaultSaveFunction;
    if (src->ElementSize > UINT64_MAX || src->count > UINT64_MAX)
        return tree_error((TreeMap *)src, "Save", CONTAINER_ERROR_NOMEMORY);
    if (fwrite(&TreeMapGuid,sizeof(TreeMapGuid),1,stream) != 1)
        return CONTAINER_ERROR_FILE_WRITE;
    memset(header, 0, sizeof(header));
    store_u32le(header, TREEMAP_FILE_VERSION);
    store_u64le(header + 8, (uint64_t)src->ElementSize);
    store_u64le(header + 16, (uint64_t)src->count);
    store_u32le(header + 24, (uint32_t)src->Flags);
    if (fwrite(header, sizeof(header), 1, stream) != 1)
        return CONTAINER_ERROR_FILE_WRITE;
    if (arg == NULL) {
        elemsiz = src->ElementSize;
        arg = &elemsiz;
    }
    rvp = bt_first(src);
    while (rvp) {
        if (saveFn(rvp->data,arg,stream) <= 0)
            return CONTAINER_ERROR_FILE_WRITE;
        rvp = bt_next(src,rvp);
    }
    return 1;
}

static int DefaultLoadFunction(void *element,void *arg, FILE *Infile)
{
    size_t len = *(size_t *)arg;
    if (len == 0)
        return 1;
    return len == fread(element,1,len,Infile);
}

static TreeMap *Load(FILE *stream, ReadFunction loadFn,void *arg)
{
    size_t i;
    size_t elemSize;
    size_t count;
    TreeMap *result;
    unsigned char *buf;
    unsigned char header[TREEMAP_FILE_HEADER_SIZE];
    uint64_t encoded_size;
    uint64_t encoded_count;
    uint32_t flags;
    uint32_t version;
    int r = 1;
    guid Guid;

    if (stream == NULL) {
        iError.RaiseError("Load",CONTAINER_ERROR_BADARG);
        return NULL;
    }
    if (fread(&Guid,sizeof(Guid),1,stream) != 1) {
        iError.RaiseError("Load",CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    if (memcmp(&Guid,&TreeMapGuid,sizeof(Guid)) != 0) {
        iError.RaiseError("Load",CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    if (fread(header, sizeof(header), 1, stream) != 1) {
        iError.RaiseError("Load",CONTAINER_ERROR_FILE_READ);
        return NULL;
    }
    version = load_u32le(header);
    encoded_size = load_u64le(header + 8);
    encoded_count = load_u64le(header + 16);
    flags = load_u32le(header + 24);
    if (version != TREEMAP_FILE_VERSION || encoded_size > SIZE_MAX ||
        encoded_count > SIZE_MAX) {
        iError.RaiseError("Load",CONTAINER_ERROR_WRONGFILE);
        return NULL;
    }
    elemSize = (size_t)encoded_size;
    count = (size_t)encoded_count;
    result = Create(elemSize);
    if (result == NULL) {
        iError.RaiseError("Load",CONTAINER_ERROR_NOMEMORY);
        return NULL;
    }
    buf = result->Allocator->malloc(elemSize == 0 ? 1 : elemSize);
    if (buf == NULL) {
        iError.RaiseError("Load",CONTAINER_ERROR_NOMEMORY);
        Finalize(result);
        return NULL;
    }
    if (loadFn == NULL) {
        loadFn = DefaultLoadFunction;
        arg = &elemSize;
    }
    for (i=0; i < count; i++) {
        if (loadFn(buf,arg,stream) <= 0) {
            r = CONTAINER_ERROR_FILE_READ;
            break;
        }
        if ((r=Add(result,buf,NULL)) < 0)
            break;
    }
    result->Allocator->free(buf);
    if (r < 0) {
        iError.RaiseError("Load",r);
        Finalize(result);
        return NULL;
    }
    result->Flags = flags;
    return result;
}

static DestructorFunction SetDestructor(TreeMap *cb,DestructorFunction fn)
{
	DestructorFunction oldfn;
	if (cb == NULL)
		return NULL;
	oldfn = cb->DestructorFn;
	if (fn)
		cb->DestructorFn = fn;
	return oldfn;
}
static const ContainerAllocator *GetAllocator(const TreeMap *l)
{
    if (l == NULL)
        return NULL;
    return l->Allocator;
}


TreeMapInterface iTreeMap = {
    Size,
    GetFlags,
    SetFlags,
    Clear,
    Contains,
    Erase,
    Finalize,
    Apply,
    Equal,
    Copy,
    SetErrorFunction,
    Sizeof,
    NewIterator,
    InitIterator,
    DeleteIterator,
    SizeofIterator,
    Save,
    Add,
	AddRange,
    Insert,
    GetElement,
    SetCompareFunction,
    CreateWithAllocator,
    Create,
    GetElementSize,
    Load,
    SetDestructor,
    InitializeWith,
	GetAllocator,
};
