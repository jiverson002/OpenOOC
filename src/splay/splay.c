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

/* uintptr_t */
#include <inttypes.h>

/* size_t */
#include <stddef.h>

/* NULL */
#include <stdlib.h>

#include "splay.h"
#include "../lock/lock.h"


#define MAKE_CHILD(A, WHICH, B)\
  (A)->WHICH = (B);\
  if (B) (B)->p = (A);\
  if ((A)->p == (B)) (A)->p = NULL;


/*------------------------------------------------------------------------------
  Create a new node           
------------------------------------------------------------------------------*/
static void
_sp_nd_init(sp_nd_t * const n, uintptr_t const b, size_t const s)
{
  int ret;

  n->p = NULL;
  n->l = NULL;
  n->r = NULL;
  n->b = b;
  n->s = s;

  /* Initialize n's lock. */
  ret = lock_init(&(n->lock));
  assert(!ret);
}


/*------------------------------------------------------------------------------
  Simple top down splay, not requiring num to be in the tree t.
  What it does is described above.
------------------------------------------------------------------------------*/
static sp_nd_t *
_sp_splay(uintptr_t const b, sp_nd_t * t)
{
  sp_nd_t n, * l, * r, * y;

  if (NULL == t) {
    return t;
  }
  n.p = n.l = n.r = NULL;
  l = r = &n;

  for (;;) {
    if (b < t->b) {
      if (NULL == t->l) {
        break;
      }
      if (b < t->l->b) {
        y = t->l;                         /* rotate right */
        MAKE_CHILD(t, l, y->r);
        MAKE_CHILD(y, r, t);
        t = y;
        if (NULL == t->l) {
          break;
        }
      }
      MAKE_CHILD(r, l, t);                /* link right */
      r = t;
      t = t->l;
    }
    else if (t->b < b) {
      if (NULL == t->r) {
        break;
      }
      if (t->r->b < b) {
        y = t->r;                         /* rotate left */
        MAKE_CHILD(t, r, y->l);
        MAKE_CHILD(y, l, t);
        t = y;
        if (NULL == t->r) {
          break;
        }
      }
      MAKE_CHILD(l, r, t);                /* link left */
      l = t;
      t = t->r;
    }
    else {
      break;
    }
  }
  MAKE_CHILD(l, r, t->l);                 /* assemble */
  MAKE_CHILD(r, l, t->r);
  MAKE_CHILD(t, l, n.r);
  MAKE_CHILD(t, r, n.l);
  t->p = NULL;

  return t;
}


/*------------------------------------------------------------------------------
  Initialize the linked list to an empty list
------------------------------------------------------------------------------*/
int
ooc_sp_init(sp_t * const sp)
{
  int ret;

  sp->root = NULL;
  sp->next = NULL;

  ret = lock_init(&(sp->lock));
  assert(!ret);

  return 0;
}


/*------------------------------------------------------------------------------
  Start from the root and recursively free each subtree of a splay tree
------------------------------------------------------------------------------*/
int
ooc_sp_free(sp_t * const sp)
{
  int ret;

  if (sp) {
    while (sp->root) {
      ooc_sp_remove(sp, sp->root->b);
    }

    ret = lock_free(&(sp->lock));
    assert(!ret);
  }
  return 0;
}


/*------------------------------------------------------------------------------
  Insert item into the tree t, unless it's already there.
  Set root of sp to the resulting tree.
------------------------------------------------------------------------------*/
int
ooc_sp_insert(sp_t * const sp, sp_nd_t * const z, uintptr_t const b,
              size_t const s)
{
  int ret;
  sp_nd_t * t;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  t = sp->root;

  _sp_nd_init(z, b, s);

  if (t == NULL) {
    sp->root = z;
    goto fn_return;
  }

  t = _sp_splay(b, t);

  if (b < t->b) {
    MAKE_CHILD(z, l, t->l);
    MAKE_CHILD(z, r, t);
    t->l = NULL;
    sp->root = z;
    goto fn_return;
  }
  else if (t->b < b) {
    MAKE_CHILD(z, r, t->r);
    MAKE_CHILD(z, l, t);
    t->r = NULL;
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


/*------------------------------------------------------------------------------
  Find a datum in the tree, it MUST exist.
------------------------------------------------------------------------------*/
int
ooc_sp_find_and_lock(sp_t * const sp, uintptr_t const d,
                     sp_nd_t ** const sp_nd_p)
{
  int ret;
  sp_nd_t * n;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  /* splay d to root of tree */
  sp->root = _sp_splay(d, sp->root);

  /* if root equals d, then d exists in tree and can be returned. if not, then
   * d does not exist in tree and nothing will be returned, but a neighbor of
   * where d would be in the tree will be made root and the tree will be
   * slightly more balanced. */
  if (sp->root) {
    if (sp->root->b <= d) {
      if (d <= sp->root->b+sp->root->s) {
        if (!ret) {
          *sp_nd_p = sp->root;
          goto fn_return;
        }
      }
    }
    else {
      for (n=sp->root->l; n && n->r; n=n->r);
      if (n->b <= d) {
        if (d <= n->b+n->s) {
          ret = lock_get(&(n->lock));
          if (!ret) {
            *sp_nd_p = n;
            goto fn_return;
          }
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


/*------------------------------------------------------------------------------
  Remove node with specified datum in the tree, if MUST exist. Set root of sp to
  the resulting tree.
------------------------------------------------------------------------------*/
int
ooc_sp_remove(sp_t * const sp, uintptr_t const b)
{
  int ret;
  sp_nd_t * z, * t;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  t = sp->root;

  if (t) {
    t = _sp_splay(b, t);

    if (b == t->b) {            /* found it */
      if (t->l == NULL) {
        z = t->r;
      }
      else {
        z = _sp_splay(b, t->l);
        z->p = NULL;
        MAKE_CHILD(z, r, t->r);
      }
      sp->root = z;

      ret = lock_free(&(t->lock));
      assert(!ret);

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


/*------------------------------------------------------------------------------
  Iterate the nodes of the splay tree in order
------------------------------------------------------------------------------*/
int
ooc_sp_next(sp_t * const sp, sp_nd_t ** const sp_nd_p)
{
  int ret;
  sp_nd_t * n;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  n = sp->next;

  if (!n) {
    for (n=sp->root; n && n->l; n=n->l);
  }
  else if (!n->r) {
    for (; n->p && n == n->p->r; n=n->p);
    n = n->p;
  }
  else {
    for (n=n->r; n && n->l; n=n->l);
  }

  sp->next = n;

  ret = lock_let(&(sp->lock));
  assert(!ret);

  *sp_nd_p = n;

  return 0;
}


/*------------------------------------------------------------------------------
  Check if splay tree is empty.
------------------------------------------------------------------------------*/
int
ooc_sp_empty(sp_t * const sp)
{
  int ret, retval;

  ret = lock_get(&(sp->lock));
  assert(!ret);

  retval = (NULL == sp->root);

  ret = lock_let(&(sp->lock));
  assert(!ret);

  return retval;
}


#ifdef TEST
/* assert */
#include <assert.h>

/* EXIT_SUCCESS */
#include <stdlib.h>

int
main(void)
{
  int ret, i;
  sp_t sp;
  struct vma * vma_p;
  struct vma vma[100];

  ret = ooc_sp_init(&sp);
  assert(!ret);

  for (i=0; i<100; ++i) {
    ret = ooc_sp_insert(&sp, &(vma[i].nd), (uintptr_t)(i*4096), 4096);
    assert(!ret);
    assert((uintptr_t)(i*4096) == sp.root->b);
    assert(4096 == sp.root->s);
  }

  sp.next = NULL;
  for (i=0; i<100; ++i) {
    ret = ooc_sp_next(&sp, (void*)&vma_p);
    assert(!ret);
    assert((uintptr_t)(i*4096) == vma_p->nd.b);
    assert(4096 == vma_p->nd.s);
  }

  for (i=0; i<100; ++i) {
    ret = ooc_sp_find_and_lock(&sp, (uintptr_t)(i*4096+128), (void*)&vma_p);
    assert(!ret);
    assert((uintptr_t)(i*4096) == vma_p->nd.b);
    assert(4096 == vma_p->nd.s);

    ret = lock_let(&(vma_p->nd.lock));
    assert(!ret);
  }

  ret = ooc_sp_free(&sp); 
  assert(!ret);

  return EXIT_SUCCESS;
}
#endif
