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


/* sysconf, _SC_PAGESIZE */
#include <unistd.h>


/*----------------------------------------------------------------------------*/
/* Implementation constants */
/*----------------------------------------------------------------------------*/
/*! OOC page size. */
#define OOC_PAGE_SIZE sysconf(_SC_PAGESIZE)

/*! Maximum number of fibers per thread. */
#define OOC_NUM_FIBERS 10


/*----------------------------------------------------------------------------*/
/* Simple lock implementation */
/*----------------------------------------------------------------------------*/
#define lock_init ooc_lock_init
#define lock_free ooc_lock_free
#define lock_get  ooc_lock_get
#define lock_let  ooc_lock_let
#define lock_try  ooc_lock_try
#define lock_t    ooc_lock_t
#ifdef _OPENMP
/* omp_lock_t */
#include <omp.h>

typedef omp_lock_t lock_t;

#define ooc_lock_init(lock) (omp_init_lock(lock), 0)
#define ooc_lock_free(lock) (omp_destroy_lock(lock), 0)
#define ooc_lock_get(lock)  (omp_set_lock(lock), 0)
#define ooc_lock_let(lock)  (omp_unset_lock(lock), 0)
#define ooc_lock_try(lock)  (0 == omp_test_lock(lock))
#else
typedef int lock_t;

#define ooc_lock_init(lock) 0
#define ooc_lock_free(lock) 0
#define ooc_lock_get(lock)  0
#define ooc_lock_let(lock)  0
#define ooc_lock_try(lock)  0
#endif


/*----------------------------------------------------------------------------*/
/* */
/*----------------------------------------------------------------------------*/
#define vm_area ooc_vm_area
#define sp_node ooc_vm_area
/*! Virtual memory area. */
struct vm_area
{
  struct vm_area * vm_next;   /* list of VMAs (for vma_alloc() and sp_tree_*) */
  struct vm_area * vm_prev;   /* doubly-linked list of VMAs (for sp_tree_*) */

  struct sp_node * sp_p;      /* parent node (used only by sp_tree_*() */
  struct sp_node * sp_l;      /* left child ... */
  struct sp_node * sp_r;      /* right child ... */

  unsigned long  vm_flags;    /* flags */
  void *         vm_start;    /* VMA start, inclusive */
  void *         vm_end;      /* VMA end, exclusive */

  lock_t         vm_lock;     /* struct lock */
};

#define sp_tree ooc_sp_tree
/*! Splay tree. */
struct sp_tree
{
  struct sp_node * root;      /* root of tree */
  lock_t         lock;        /* struct lock */
};


/*----------------------------------------------------------------------------*/
/* Function prototypes */
/*----------------------------------------------------------------------------*/
/* sp_tree.c */
#define sp_tree_init ooc_sp_tree_init
/*! Initialize the linked list to an empty list. */
int sp_tree_init(struct sp_tree * const sp);

#define sp_tree_free ooc_sp_tree_free
/*! Start from the root and recursively free each subtree of a splay tree. */
int sp_tree_free(struct sp_tree * const sp);

#define sp_tree_insert ooc_sp_tree_insert
/*! Insert node with specified data into the tree, it MUST NOT exist. */
int sp_tree_insert(struct sp_tree * const sp, struct sp_node * const z);

#define sp_tree_find_and_lock ooc_sp_tree_find_and_lock
/*! Find and lock node containing vm_addr in the tree, it MUST exist. */
int sp_tree_find_and_lock(struct sp_tree * const sp, void * const vm_addr,\
                          struct sp_node ** const zp);

#define sp_tree_find_mod_and_lock ooc_sp_tree_find_mod_and_lock
/*! Find and lock node containing vm_addr in the tree, it MUST exist. */
int sp_tree_find_mod_and_lock(struct sp_tree * const sp, void * const vm_addr,\
                              struct sp_node ** const zp);

#define sp_tree_remove ooc_sp_tree_remove
/*! Remove node with specified datum in the tree, if MUST exist. */
int sp_tree_remove(struct sp_tree * const sp_tree, void * const vm_addr);


/* vma_alloc.c */
#define vma_alloc ooc_vma_alloc
/*! Get next available vm_area struct. */
struct vm_area * vma_alloc(void);

#define vma_free ooc_vma_free
/*! Return a vm_area struct to the system. */
void vma_free(struct vm_area * const vma);

#define vma_gpool_init ooc_vma_gpool_init
/*! Initialize the vm_area struct memory pool. */
void vma_gpool_init(void);

#define vma_gpool_free ooc_vma_gpool_free
/*! Free the vm_area struct memory pool. */
void vma_gpool_free(void);

#define vma_gpool_gather ooc_vma_gpool_gather
/*! Gather statistics runtime statistics. */
void vma_gpool_gather(void);

#define vma_gpool_show ooc_vma_gpool_show
/*! Show runtime statistics. */
void vma_gpool_show(void);


/*----------------------------------------------------------------------------*/
/* Extern variables */
/*----------------------------------------------------------------------------*/
#define vma_tree ooc_vma_tree
/*! OOC Virtual Memory Area (VMA) tree - shared by all threads in a process,
 * since said threads all share the same address space. */
extern struct sp_tree vma_tree;


#endif /* OOC_COMMON_H */
