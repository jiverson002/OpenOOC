/*
Copyright (c) 2016 Jeremy Iverson

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
/*
                An implementation of top-down splaying
                    D. Sleator <sleator@cs.cmu.edu>
                           March 1992

  "Splay trees", or "self-adjusting search trees" are a simple and
  efficient data structure for storing an ordered set.  The data
  structure consists of a binary tree, without parent pointers, and no
  additional fields.  It allows searching, insertion, deletion,
  deletemin, deletemax, splitting, joining, and many other operations,
  all with amortized logarithmic performance.  Since the trees adapt to
  the sequence of requests, their performance on real access patterns is
  typically even better.  Splay trees are described in a number of texts
  and papers [1,2,3,4,5].

  The code here is adapted from simple top-down splay, at the bottom of
  page 669 of [3].  It can be obtained via anonymous ftp from
  spade.pc.cs.cmu.edu in directory /usr/sleator/public.

  The chief modification here is that the splay operation works even if the
  item being splayed is not in the tree, and even if the tree root of the
  tree is NULL.  So the line:

                              t = splay(i, t);

  causes it to search for item with key i in the tree rooted at t.  If it's
  there, it is splayed to the root.  If it isn't there, then the node put
  at the root is the last one before NULL that would have been reached in a
  normal binary search for i.  (It's a neighbor of i in the tree.)  This
  allows many other operations to be easily implemented, as shown below.

  [1] "Fundamentals of data structures in C", Horowitz, Sahni,
       and Anderson-Freed, Computer Science Press, pp 542-547.
  [2] "Data Structures and Their Algorithms", Lewis and Denenberg,
       Harper Collins, 1991, pp 243-251.
  [3] "Self-adjusting Binary Search Trees" Sleator and Tarjan,
       JACM Volume 32, No 3, July 1985, pp 652-686.
  [4] "Data Structure and Algorithm Analysis", Mark Weiss,
       Benjamin Cummins, 1992, pp 119-130.
  [5] "Data Structures, Algorithms, and Performance", Derick Wood,
       Addison-Wesley, 1993, pp 367-375.
*/


/* assert */
#include <assert.h>

/* NULL */
#include <stdlib.h>

/* */
#include "common.h"


#define MAKE_CHILD(A, WHICH, B)\
  (A)->WHICH = (B);\
  if (B) (B)->sp_p = (A);\
  if ((A)->sp_p == (B)) (A)->sp_p = NULL;


/*! Create a new node. */
static void
sp_node_init(struct sp_node * const n)
{
  n->sp_p = NULL;
  n->sp_l = NULL;
  n->sp_r = NULL;
}


/*! Simple top down splay, not requiring d to be in the tree t. */
static struct sp_node *
sp_tree_splay(void * const vm_addr, struct sp_node * t)
{
  struct sp_node n, * l, * r, * y;

  if (!t) {
    return t;
  }
  n.sp_p = n.sp_l = n.sp_r = NULL;
  l = r = &n;

  for (;;) {
    if (vm_addr < t->vm_start) {
      if (!t->sp_l) {
        break;
      }
      if (vm_addr < t->sp_l->vm_start) {
        y = t->sp_l;                        /* rotate right */
        MAKE_CHILD(t, sp_l, y->sp_r);
        MAKE_CHILD(y, sp_r, t);
        t = y;
        if (!t->sp_l) {
          break;
        }
      }
      MAKE_CHILD(r, sp_l, t);               /* link right */
      r = t;
      t = t->sp_l;
    }
    else if (t->vm_start < vm_addr) {
      if (!t->sp_r) {
        break;
      }
      if (t->sp_r->vm_start < vm_addr) {
        y = t->sp_r;                        /* rotate left */
        MAKE_CHILD(t, sp_r, y->sp_l);
        MAKE_CHILD(y, sp_l, t);
        t = y;
        if (!t->sp_r) {
          break;
        }
      }
      MAKE_CHILD(l, sp_r, t);               /* link left */
      l = t;
      t = t->sp_r;
    }
    else {
      break;
    }
  }
  MAKE_CHILD(l, sp_r, t->sp_l);             /* assemble */
  MAKE_CHILD(r, sp_l, t->sp_r);
  MAKE_CHILD(t, sp_l, n.sp_r);
  MAKE_CHILD(t, sp_r, n.sp_l);
  t->sp_p = NULL;

  return t;
}


int
sp_tree_init(struct sp_tree * const sp)
{
  int ret;

  sp->root = NULL;

  ret = lock_init(&(sp->lock));
  assert(!ret);

  return 0;
}


int
sp_tree_free(struct sp_tree * const sp)
{
  int ret;

  while (sp->root) {
    sp_tree_remove(sp, sp->root->vm_start);
  }

  ret = lock_free(&(sp->lock));
  assert(!ret);

  return 0;
}


int
sp_tree_insert(struct sp_tree * const sp, struct sp_node * const z)
{
  int ret;
  struct sp_node * t;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  t = sp->root;

  sp_node_init(z);

  if (!t) {
    sp->root = z;
    goto fn_return;
  }

  t = sp_tree_splay(z->vm_start, t);

  if (z->vm_start < t->vm_start) {
    MAKE_CHILD(z, sp_l, t->sp_l);
    MAKE_CHILD(z, sp_r, t);
    t->sp_l = NULL;
    sp->root = z;
    goto fn_return;
  }
  else if (t->vm_start < z->vm_start) {
    MAKE_CHILD(z, sp_r, t->sp_r);
    MAKE_CHILD(z, sp_l, t);
    t->sp_r = NULL;
    sp->root = z;
    goto fn_return;
  }

  /* We get here if it's already in the tree */
  /* erroneous */
  return -1;

  fn_return:
  ret = lock_let(&(sp->lock));
  assert(!ret);

  return 0;
}


int
sp_tree_find_and_lock(struct sp_tree * const sp, void * const vm_addr,
                      struct sp_node ** const zp)
{
  int ret;
  struct sp_node * n;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  /* splay vm_addr to root of tree */
  sp->root = sp_tree_splay(vm_addr, sp->root);

  /* if root equals vm_addr, then vm_addr exists in tree and can be returned. if
   * not, then vm_addr does not exist in tree and nothing will be returned, but
   * a neighbor of where vm_addr would be in the tree will be made root and the
   * tree will be slightly more balanced. */
  if (sp->root) {
    if (sp->root->vm_start <= vm_addr) {
      if (vm_addr < sp->root->vm_end) {
        ret = lock_get(&(sp->root->vm_lock));
        assert(!ret);

        *zp = sp->root;
        goto fn_return;
      }
    }
    else {
      for (n=sp->root->sp_l; n && n->sp_r; n=n->sp_r);
      if (n->vm_start <= vm_addr) {
        if (vm_addr < n->vm_end) {
          ret = lock_get(&(n->vm_lock));
          assert(!ret);

          *zp = n;
          goto fn_return;
        }
      }
    }
  }

  /* erroneous */
  return -1;

  fn_return:
  ret = lock_let(&(sp->lock));
  assert(!ret);

  return 0;
}


int
sp_tree_remove(struct sp_tree * const sp, void * const vm_addr)
{
  int ret;
  struct sp_node * z, * t;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  t = sp->root;

  if (t) {
    t = sp_tree_splay(vm_addr, t);

    if (vm_addr == t->vm_start) {            /* found it */
      if (!t->sp_l) {
        z = t->sp_r;
      }
      else {
        z = sp_tree_splay(vm_addr, t->sp_l);
        z->sp_p = NULL;
        MAKE_CHILD(z, sp_r, t->sp_r);
      }
      sp->root = z;
      goto fn_return;
    }
  }

  /* erroneous */
  return -1;

  fn_return:
  ret = lock_let(&(sp->lock));
  assert(!ret);

  return 0;
}


#ifdef TEST
/* assert */
#include <assert.h>

/* uintptr_t */
#include <inttypes.h>

/* EXIT_SUCCESS */
#include <stdlib.h>

struct sp_tree vma_tree;

int
main(void)
{
  int ret, i;
  struct sp_node * zp;
  struct sp_node z[100];

  ret = sp_tree_init(&vma_tree);
  assert(!ret);

  for (i=0; i<100; ++i) {
    z[i].vm_start = (void*)((uintptr_t)i*4096);
    z[i].vm_end   = (void*)((char*)z[i].vm_start+4096);

    ret = lock_init(&(z[i].vm_lock));
    assert(!ret);

    ret = sp_tree_insert(&vma_tree, &(z[i]));
    assert(!ret);
    assert((void*)((uintptr_t)i*4096) == vma_tree.root->vm_start);
    assert((void*)((char*)vma_tree.root->vm_start+4096) == vma_tree.root->vm_end);
  }

  for (i=0; i<100; ++i) {
    ret = sp_tree_find_and_lock(&vma_tree, (void*)((uintptr_t)i*4096+128), (void*)&zp);
    assert(!ret);
    assert((void*)((uintptr_t)i*4096) == zp->vm_start);
    assert((void*)((char*)zp->vm_start+4096) == zp->vm_end);

    ret = lock_let(&(zp->vm_lock));
    assert(!ret);
  }

  ret = sp_tree_free(&vma_tree);
  assert(!ret);

  for (i=0; i<100; ++i) {
    ret = lock_free(&(z[i].vm_lock));
    assert(!ret);
  }

  return EXIT_SUCCESS;
}
#endif
