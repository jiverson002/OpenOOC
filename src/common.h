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


#ifndef OOC_COMMON_H
#define OOC_COMMON_H


/*----------------------------------------------------------------------------*/
/* Simple lock implementation */
/*----------------------------------------------------------------------------*/
#ifdef _OPENMP
/* omp_lock_t */
#include <omp.h>

typedef omp_lock_t ooc_lock_t;

#define lock_init(lock) (omp_init_lock(lock), 0)
#define lock_free(lock) (omp_destroy_lock(lock), 0)
#define lock_get(lock)  (omp_set_lock(lock), 0)
#define lock_let(lock)  (omp_unset_lock(lock), 0)

#else

typedef int ooc_lock_t;

#define lock_init(lock) 0
#define lock_free(lock) 0
#define lock_get(lock)  0
#define lock_let(lock)  0
#endif


/*----------------------------------------------------------------------------*/
/* OOC page table */
/*----------------------------------------------------------------------------*/
/*------------------------------------------------------------------------------
  Splay tree node
------------------------------------------------------------------------------*/
struct pte
{
  struct pte * p;
  struct pte * l;
  struct pte * r;
  uintptr_t b;
  size_t s;
  ooc_lock_t lock;
};

/*------------------------------------------------------------------------------
  Splay tree
------------------------------------------------------------------------------*/
struct ptbl
{
  struct pte * root;
  struct pte * next;
  ooc_lock_t lock;
};

/*------------------------------------------------------------------------------
  Virtual memory allocation
------------------------------------------------------------------------------*/
struct vma
{
  struct pte pte;
  uint8_t * pflags;
};


/* OOC page table. */
extern struct ptbl _ptbl;


int ptbl_init(struct ptbl * const ptbl);
int ptbl_free(struct ptbl * const ptbl);
int ptbl_insert(struct ptbl * const ptbl, struct pte * const z,\
                uintptr_t const b, size_t const s);
int ptbl_find_and_lock(struct ptbl * const ptbl, uintptr_t const d,\
                       struct pte ** const pte_p);
int ptbl_remove(struct ptbl * const ptbl, uintptr_t const d);
int ptbl_next(struct ptbl * const ptbl, struct pte ** const pte_p);
int ptbl_empty(struct ptbl * const ptbl);


#endif /* OOC_COMMON_H */
