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


#define FIX_LISTLT(A, B)\
  (A)->vm_prev = (B)->vm_prev;\
  (A)->vm_next = (B);\
  if ((A)->vm_prev) (A)->vm_prev->vm_next = (A);\
  (B)->vm_prev = (A);

#define FIX_LISTGT(A, B)\
  (A)->vm_prev = (B);\
  (A)->vm_next = (B)->vm_next;\
  if ((A)->vm_next) (A)->vm_next->vm_prev = (A);\
  (B)->vm_next = (A);

#define FIX_LISTRM(A)\
  if ((A)->vm_prev) (A)->vm_prev->vm_next = (A)->vm_next;\
  if ((A)->vm_next) (A)->vm_next->vm_prev = (A)->vm_prev;

#define MAKE_CHILD(A, WHICH, B)\
  (A)->WHICH = (B);\
  if (B) (B)->sp_p = (A);\
  if ((A)->sp_p == (B)) (A)->sp_p = NULL;


/*! Create a new node. */
static void
S_sp_node_init(struct sp_node * const n)
{
  n->sp_p = NULL;
  n->sp_l = NULL;
  n->sp_r = NULL;

  n->vm_prev = NULL;
  n->vm_next = NULL;
}


/*! Simple top down splay, not requiring vm_addr to be in the tree t. */
static struct sp_node *
S_sp_tree_splay(void * const vm_addr, struct sp_node * t)
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
        /* FIXME This control statement does not evaluate to true in any of
         * the tests. */
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

  /* Lock splay tree. */
  ret = lock_get(&(sp->lock));
  assert(!ret);

  S_sp_node_init(z);

  if (!sp->root) {
    sp->root = z;

    /* Unlock splay tree. */
    ret = lock_let(&(sp->lock));
    assert(!ret);

    return 0;
  }

  t = S_sp_tree_splay(z->vm_start, sp->root);

  if (z->vm_start < t->vm_start) {
    FIX_LISTLT(z, t);
    MAKE_CHILD(z, sp_l, t->sp_l);
    MAKE_CHILD(z, sp_r, t);
    t->sp_l = NULL;
    sp->root = z;
  }
  else {
    assert(t->vm_start < z->vm_start);

    FIX_LISTGT(z, t);
    MAKE_CHILD(z, sp_r, t->sp_r);
    MAKE_CHILD(z, sp_l, t);
    t->sp_r = NULL;
    sp->root = z;
  }

  /* Unlock splay tree. */
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

  /* Lock splay tree. */
  ret = lock_get(&(sp->lock));
  assert(!ret);

  /* Sanity check: root cannot be NULL. */
  assert(sp->root);

  /* Splay vm_addr to root of tree. This will result in either: 1) sp->root
   * containing vm_addr in its range or 2) sp->root->prev containing vm_addr in
   * its range. */
  sp->root = S_sp_tree_splay(vm_addr, sp->root);

  if (sp->root->vm_start <= vm_addr) {
    /* Sanity check: vm_addr must be contained in sp->root. */
    assert(vm_addr < sp->root->vm_end);

    n = sp->root;
  }
  else {
    /* Sanity check: vm_addr must be contained in sp->root->vm_prev. */
    assert(sp->root->vm_prev->vm_start <= vm_addr);
    assert(vm_addr < sp->root->vm_prev->vm_end);

    n = sp->root->vm_prev;
  }

  /* Lock the node containing vm_addr. */
  ret = lock_get(&(n->vm_lock));
  assert(!ret);

  /* Unlock splay tree. */
  ret = lock_let(&(sp->lock));
  assert(!ret);

  /* Set output variable. */
  *zp = n;

  return 0;
}


/*! NOTE vm_addr must be OOC_PAGE_SIZE aligned. */
int
sp_tree_find_mod_and_lock(struct sp_tree * const sp, void * const vm_addr,
                          struct sp_node ** const zp)
{
  int ret;
  struct sp_node * prev, * n=NULL, * t, * z;

  /* Lock splay tree. */
  ret = lock_get(&(sp->lock));
  assert(!ret);

  /* Sanity check: root cannot be NULL. */
  assert(sp->root);

  /* Splay vm_addr to root of tree. This will result in either: 1) sp->root
   * containing vm_addr in its range, 2) sp->root->prev containing vm_addr in
   * its range, or 3) neither, but sp->root will have greatest vm_start <=
   * vm_addr. */
  sp->root = S_sp_tree_splay(vm_addr, sp->root);

  if (sp->root->vm_start <= vm_addr && vm_addr < sp->root->vm_end) {
    n = sp->root;
  }
  else if ((prev=sp->root->vm_prev)) {
    if (prev->vm_start <= vm_addr && vm_addr < prev->vm_end) {
      n = sp->root->vm_prev;
    }
  }

  /* TODO Maybe don't call sp_tree_insert, since we have already splay'd
   * vm_addr to root. Or maybe it will be very fast, since we have already
   * done it. */
  if (!n) {
    /* Add new vma. */
    /* Create new vma for vm_addr. */
    z = vma_alloc();
    z->vm_start = vm_addr;
    z->vm_end = (void*)((char*)z->vm_start+OOC_PAGE_SIZE);
    S_sp_node_init(z);

    /* Insert new node. */
    ret = sp_tree_insert(sp, z);
    assert(!ret);

    /* Try 3-way merge. */
  }
  else if (n->vm_start != vm_addr &&\
           n->vm_end != (void*)((char*)vm_addr+OOC_PAGE_SIZE))
  {
    /* Do mid split -- no merge is possible. */
    /* Create new vma for vm_addr. */
    z = vma_alloc();
    z->vm_start = vm_addr;
    z->vm_end = (void*)((char*)z->vm_start+OOC_PAGE_SIZE);
    S_sp_node_init(z);

    /* Create new vma for suffix range. */
    t = vma_alloc();
    t->vm_start = z->vm_end;
    t->vm_end = n->vm_end;
    S_sp_node_init(t);

    /* Adjust range for prefix range. */
    n->vm_end = z->vm_start;

    /* Insert new nodes. */
    ret = sp_tree_insert(sp, z);
    assert(!ret);
    ret = sp_tree_insert(sp, t);
    assert(!ret);
  }
  else if (n->vm_start == vm_addr) {
    if (n->vm_prev && /* new memory protections match n->prev */0) {
      /* Do prefix migrate. */
      n->vm_start = (void*)((char*)vm_addr+OOC_PAGE_SIZE);
      n->vm_prev->vm_end = n->vm_start;
    }
    else {
      /* Do prefix split. */
      /* Create new vma for vm_addr. */
      z = vma_alloc();
      z->vm_start = vm_addr;
      z->vm_end = (void*)((char*)z->vm_start+OOC_PAGE_SIZE);
      S_sp_node_init(z);

      /* Adjust suffix range. */
      n->vm_start = z->vm_end;

      /* Insert new node. */
      ret = sp_tree_insert(sp, z);
      assert(!ret);
    }
  }
  else if (n->vm_end  == (void*)((char*)vm_addr+OOC_PAGE_SIZE)) {
    if (n->vm_next && /* new memory protections match n->next */0) {
      /* Do suffix migrate. */
      n->vm_end = vm_addr;
      n->vm_next->vm_start = n->vm_end;
    }
    else {
      /* Do suffix split. */
      /* Create new vma for vm_addr. */
      z = vma_alloc();
      z->vm_start = vm_addr;
      z->vm_end = (void*)((char*)z->vm_start+OOC_PAGE_SIZE);
      S_sp_node_init(z);

      /* Adjust prefix range. */
      n->vm_end = z->vm_start;

      /* Insert new node. */
      ret = sp_tree_insert(sp, z);
      assert(!ret);
    }
  }
  else {
    /* Try 3-way merge. */
  }

  /* Lock the node containing vm_addr. */
  ret = lock_get(&(n->vm_lock));
  assert(!ret);

  /* Unlock splay tree. */
  ret = lock_let(&(sp->lock));
  assert(!ret);

  /* Set output variable. */
  *zp = n;

  return 0;
}


int
sp_tree_remove(struct sp_tree * const sp, void * const vm_addr)
{
  int ret;
  struct sp_node * z, * t;

  /* Lock splay tree. */
  ret = lock_get(&(sp->lock));
  assert(!ret);

  /* Sanity check: root cannot be NULL. */
  assert(sp->root);

  t = S_sp_tree_splay(vm_addr, sp->root);

  /* Sanity check: vm_addr must be the vm_start of some node in sp. */
  assert(vm_addr == t->vm_start);

  FIX_LISTRM(t);

  if (t->sp_l) {
    z = S_sp_tree_splay(vm_addr, t->sp_l);
    z->sp_p = NULL;
    MAKE_CHILD(z, sp_r, t->sp_r);
  }
  else {
    z = t->sp_r;
  }

  sp->root = z;

  /* Unlock splay tree. */
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

#define N_NODES 100

struct sp_tree vma_tree;

int
main(void)
{
  int ret, i;
  uintptr_t vm_start, vm_end, vm_addr;
  struct sp_node * zp;
  struct sp_node z[N_NODES+1];

  ret = sp_tree_init(&vma_tree);
  assert(!ret);

  /* TODO Instead of just inserting nodes according to some pattern, as is done
   * below, we should deliberately chose the vm_start and vm_end addresses so
   * that each execution path through the code is executed. */
  for (i=0; i<N_NODES; ++i) {
    if (i%2 == 0) {
      vm_start = ((uintptr_t)i*4096);
    }
    else {
      vm_start = ((uintptr_t)(N_NODES-(N_NODES%2==1)-i)*4096);
    }
    vm_end = vm_start+4096;

    z[i].vm_start = (void*)vm_start;
    z[i].vm_end   = (void*)vm_end;

    ret = lock_init(&(z[i].vm_lock));
    assert(!ret);

    ret = sp_tree_insert(&vma_tree, &(z[i]));
    assert(!ret);

    assert((void*)vm_start == vma_tree.root->vm_start);
    assert((void*)vm_end   == vma_tree.root->vm_end);
  }

  /****************************************************************************/
  /* NOTE The following three tests are no long supported by the splay tree
   * implementation. */
  /****************************************************************************/
  /* Try to insert an element already in the tree. */
  /*z[N_NODES].vm_start = (void*)((uintptr_t)0);
  z[N_NODES].vm_end   = (void*)((char*)z[N_NODES].vm_start+4096);
  ret = sp_tree_insert(&vma_tree, &(z[N_NODES]));
  assert(-1 == ret);*/

  /* Try to search for an element not in the tree. */
  /*vm_addr = (uintptr_t)N_NODES*4096;
  ret = sp_tree_find_and_lock(&vma_tree, (void*)vm_addr, (void*)&zp);
  assert(-1 == ret);*/

  /* Try to remove an element not in the tree. */
  /*vm_addr = (uintptr_t)N_NODES*4096;
  ret = sp_tree_remove(&vma_tree, (void*)vm_addr);
  assert(-1 == ret);*/
  /****************************************************************************/

  /* Find lowest value element. */
  vm_start = 0;
  vm_end = vm_start+4096;
  ret = sp_tree_find_and_lock(&vma_tree, (void*)vm_start, (void*)&zp);
  assert(0 == ret);
  assert((void*)vm_start == zp->vm_start);
  assert((void*)vm_end == zp->vm_end);
  ret = lock_let(&(zp->vm_lock));
  assert(!ret);

  /* Walk the tree via the doubly-linked list. */
  for (i=0; i<N_NODES && zp; ++i,zp=zp->vm_next) {
    vm_start = ((uintptr_t)i*4096);
    vm_end = vm_start+4096;

    assert((void*)vm_start == zp->vm_start);
    assert((void*)vm_end   == zp->vm_end);
  }
  assert(i == N_NODES);

  for (i=0; i<N_NODES; ++i) {
    if (i%2 == 0) {
      vm_addr = (uintptr_t)(i*4096+128);
    }
    else {
      vm_addr = ((uintptr_t)(N_NODES-(N_NODES%2==1)-i)*4096+128);
    }

    ret = sp_tree_find_and_lock(&vma_tree, (void*)vm_addr, (void*)&zp);
    assert(!ret);

    assert((void*)(vm_addr-128) == zp->vm_start);
    assert((void*)((char*)zp->vm_start+4096) == zp->vm_end);

    ret = lock_let(&(zp->vm_lock));
    assert(!ret);
  }

  /* Remove a couple nodes from tree. */
  for (i=0; i<2; ++i) {
    if (i%2 == 0) {
      vm_addr = ((uintptr_t)i*4096);
    }
    else {
      vm_addr = ((uintptr_t)(N_NODES-(N_NODES%2==1)-i)*4096);
    }

    ret = sp_tree_remove(&vma_tree, (void*)vm_addr);
    assert(!ret);
  }

  /* Walk the tree via the doubly-linked list. */
  for (i=1; i<N_NODES-1 && zp; ++i,zp=zp->vm_next) {
    vm_start = ((uintptr_t)i*4096);
    vm_end = vm_start+4096;

    assert((void*)vm_start == zp->vm_start);
    assert((void*)vm_end   == zp->vm_end);
  }
  assert(i == N_NODES-1);

  ret = sp_tree_free(&vma_tree);
  assert(!ret);

  for (i=0; i<N_NODES; ++i) {
    ret = lock_free(&(z[i].vm_lock));
    assert(!ret);
  }

  return EXIT_SUCCESS;
}
#endif
