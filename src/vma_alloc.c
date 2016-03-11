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


#if defined(USE_MMAP) && defined(USE_MEMALIGN)
  #undef USE_MMAP
#endif
#if !defined(USE_MMAP) && !defined(USE_MEMALIGN)
  #define USE_MEMALIGN
#endif

#ifdef USE_MMAP
  #ifndef _BSD_SOURCE
    #define _BSD_SOURCE
  #endif
  /* mmap, munmap */
  #include <sys/mman.h>
  #define SYS_ALLOC_FAIL MAP_FAILED
  #define SYS_FREE_FAIL  -1
  #define CALL_SYS_ALLOC(P,S) \
    ((P)=mmap(NULL, S, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0))
  #define CALL_SYS_FREE(P,S) munmap(P,S)
#endif

#ifdef USE_MEMALIGN
  #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200112L
  #elif _POSIX_C_SOURCE < 200112L
    #undef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200112L
  #endif
  /* posix_memalign */
  #include <stdlib.h>
  /* memset */
  #include <string.h>
  #define SYS_ALLOC_FAIL NULL
  #define SYS_FREE_FAIL  -1
  #define CALL_SYS_ALLOC(P,S) \
    (0 == posix_memalign((void**)&(P),(size_t)PAGESIZE,S) ? (P) : NULL)
  #define CALL_SYS_FREE(P,S) (free(P), 0)
#endif

/* assert */
#include <assert.h>

/* size_t */
#include <stddef.h>

/* uintptr_t */
#include <stdint.h>

/* sysconf */
#include <unistd.h>

/* */
#include "common.h"


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Block data structure. */
/******************************************************************************/
/******************************************************************************/
/*
 *  struct block:
 *
 *    | size_t | void * | void * | void * | `vma structs' |
 *    +--------+--------+--------+--------+---------------+
 *
 *    Memory blocks must be page aligned and have size equal to one page. They
 *    consist of a header that is a single size_t counter of the number of used
 *    structs in the block. The header maybe followed by some padding to ensure
 *    that the first vma struct in the block is aligned properly.
 *
 *
 *  vma struct:   
 *
 *      ACTIVE  | `used memory'            |
 *              +--------------------------+
 *                        
 *
 *    INACTIVE  | void * | `unused memory' |
 *              +--------+-----------------+
 *                        
 *    Since blocks are page aligned and sized, the block to which a vma struct
 *    belongs can easily be computed by bit-wise anding the address of a vma
 *    struct with the complement of the page size minus one,
 *    block_ptr=((&vma)&(~(pagesize-1))). This allows for constant time checks
 *    to see when a block is empty and should be released. When a vma struct is
 *    inactive, the `void *' field shall point to the next available vma struct.
 *
 */
/******************************************************************************/
#define BLOCK_SIZE     262144 /* 2^18 */
#define BLOCK_CTR_FULL ((BLOCK_SIZE-sizeof(struct block))/sizeof(struct vma))


struct block
{
  size_t ctr;
  struct block * prev;
  struct block * next;
  struct vma * head;
  struct vma vma[];
};


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Memory pool data structure */
/******************************************************************************/
#define UNDES_BIN_NUM 4


/* A per-process instance of a memory pool. */
__thread struct mpool
{
  struct block * head;
} mpool = {
  .head = NULL
};

static int n_undes;
static struct block * undes[UNDES_BIN_NUM];
static ooc_lock_t undes_lock;


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/
static long int PAGESIZE;


/******************************************************************************/
/* Allocate a new block */
/******************************************************************************/
static struct block *
block_alloc(void)
{
  void * ret;
  struct block * block;

  ret = CALL_SYS_ALLOC(block, BLOCK_SIZE);
  if (SYS_ALLOC_FAIL == ret) {
    return NULL;
  }

  return block;
}


/******************************************************************************/
/* Free an existing block */
/******************************************************************************/
static void
block_free(struct block * const block)
{
  int ret;

  ret = CALL_SYS_FREE(block, BLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Translate a vma to its corresponding block. */
/******************************************************************************/
static struct block *
vma_2block(struct vma * const vma)
{
  return (struct block*)((uintptr_t)vma&(~(uintptr_t)(PAGESIZE-1)));
}


/******************************************************************************/
/* Get next available vma. */
/******************************************************************************/
static struct vma *
vma_get(void)
{
  int ret;
  struct block * block;
  struct vma * vma;

  vma = NULL;
  block = mpool.head;

  if (NULL == block) {
    ret = lock_get(&(undes_lock));
    assert(!ret);
    if (0 != n_undes) {                     /* Designate existing block. */
      block = undes[--n_undes];
      ret = lock_let(&(undes_lock));
      assert(!ret);
    }
    else {                                  /* Allocate new block. */
      ret = lock_let(&(undes_lock));
      assert(!ret);
      block = (struct block*)block_alloc();
      if (NULL == block)
        goto fn_fail;
    }

    /* Set up new block. */
    block->ctr = 0;
    block->prev = NULL;
    block->next = NULL;
    block->head = NULL;

    /* Set block as head of doubly-linked list. */
    block->prev = NULL;
    block->next = mpool.head;
    if (NULL != block->next) {
      block->next->prev = block;
    }
    mpool.head = block;

    /* Get first vma in block. */
    vma = block->vma;

    /* Set head of block's singly-linked list to first vma. */
    block->head = vma;

    /* Reset pointer for head vma. This way, dangling pointers will get reset
     * correctly. See note in the if(NULL == vma->next) condition below. */
    vma->next = NULL;
  }
  else {                                    /* Use head block. */
    /* Get head vma of block's singly-linked-list. */
    vma = block->head;
  }

  /* Increment block count. */
  block->ctr++;

  /* Remove full block from doubly-linked list. */
  if (BLOCK_CTR_FULL == block->ctr) {
    if (NULL != block->prev) {
      block->prev->next = block->next;
    }
    else {
      mpool.head = block->next;
    }
    if (NULL != block->next) {
      block->next->prev = block->prev;
    }

    block->prev = NULL;
    block->next = NULL;
  }
  else {
    if (NULL == vma->next) {
      /* Set next pointer. */
      vma->next = vma+1;

      /* Reset pointer for next vma. If this vma has never been previously used,
       * then if follows that neither has the next. Resetting pointers in this
       * way makes it so that memset'ing a block when it is undesignated,
       * unnecessary. */
      vma->next->next = NULL;
    }
  }

  /* Remove vma from front of block's singly-linked list. */
  block->head = vma->next;

  fn_fail:
  return vma;
}


/******************************************************************************/
/* Return a vma to the block to which it belongs. */
/******************************************************************************/
static void
vma_put(struct vma * const vma)
{
  int ret;
  struct block * block;

  block = vma_2block(vma);

  /* Decrement block count. */
  block->ctr--;

  if (0 == block->ctr) {
    ret = lock_get(&(undes_lock));
    assert(!ret);
    if (n_undes < UNDES_BIN_NUM) {
      /* Put block on undesignated stack. */
      undes[n_undes++] = block;
      ret = lock_let(&(undes_lock));
      assert(!ret);
    }
    else {
      ret = lock_let(&(undes_lock));
      assert(!ret);
      /* Release back to system. */
      block_free(block);
    }
  }
  else {
    /* Prepend vma to front of block singly-linked list. */
    vma->next = block->head;
    block->head = vma;

    /* If not already part of mpool doubly-linked list, add block to it. */
    if (NULL == block->prev && NULL == block->next && block != mpool.head) {
      block->prev = NULL;
      block->next = mpool.head;
      if (NULL != block->next) {
        block->next->prev = block;
      }
      mpool.head = block;
    }
  }
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Allocate a vma. */
/******************************************************************************/
struct vma *
vma_alloc(void)
{
  return vma_get();
}


/******************************************************************************/
/* Free a vma. */
/******************************************************************************/
void
vma_free(struct vma * const vma)
{
  vma_put(vma);
}


/******************************************************************************/
/* Initialize the memory pool. */
/******************************************************************************/
void
vma_mpool_init(void)
{
  int ret, i;

  PAGESIZE = sysconf(_SC_PAGESIZE);
  assert(-1 != PAGESIZE);

  for (i=0; i<UNDES_BIN_NUM; ++i) {
    undes[i] = NULL;
  }

  ret = lock_init(&(undes_lock));
  assert(!ret);
}


/******************************************************************************/
/* Free the memory pool. */
/******************************************************************************/
void
vma_mpool_free(void)
{
  int ret, i;

  for (i=0; i<n_undes; ++i) {
    /* Release back to system. */
    ret = CALL_SYS_FREE(undes[i], sizeof(undes[i]));
    assert(SYS_FREE_FAIL != ret);
  }

  ret = lock_free(&(undes_lock));
  assert(!ret);
}



#ifdef TEST
/* assert */
#include <assert.h>

/* uintptr_t */
#include <inttypes.h>

/* EXIT_SUCCESS */
#include <stdlib.h>

int
main(void)
{
  int i;
  struct vma * vma[1000];

  vma_mpool_init();

  #pragma omp parallel for num_threads(4)
  for (i=0; i<1000; ++i) {
    vma[i] = vma_alloc();
    assert(vma[i]);

    vma[i]->pte.b = (uintptr_t)i;
  }

  for (i=0; i<1000; ++i) {
    assert((uintptr_t)i == vma[i]->pte.b);
  }

  for (i=0; i<1000; ++i) {
    vma_free(vma[i]);
  }

  vma_mpool_free();

  return EXIT_SUCCESS;
}
#endif
