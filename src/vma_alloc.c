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


#if 0
  /* omp_get_thread_num() */
  #include <omp.h>
  /* printf */
  #include <stdio.h>
  #define DBG_LOG \
    fprintf(stderr, "[%d] ", (int)omp_get_thread_num());\
    fprintf
#else
  #define stderr NULL
  static inline void noop(void * unused, ...) { if (unused) {} }
  #define DBG_LOG noop
#endif

#if defined(USE_MMAP) && defined(USE_MEMALIGN)
  #undef USE_MMAP
#endif
#if !defined(USE_MMAP) && !defined(USE_MEMALIGN)
  #define USE_MEMALIGN
#endif

#ifdef USE_MMAP
  #if !defined(_BSD_SOURCE) && !defined(_SVID_SOURCE)
    #define _BSD_SOURCE /* Expose MAP_ANONYMOUS */
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
  #if !defined(_POSIX_C_SOURCE) && !defined(_XOPEN_SOURCE)
    #define _POSIX_C_SOURCE 200112L
  #elif defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE < 200112L
    #undef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 200112L
  #elif defined(_XOPEN_SOURCE) && _XOPEN_SOURCE < 600
    #undef _XOPEN_SOURCE
    #define _XOPEN_SOURCE 600
  #endif
  /* posix_memalign, free */
  #include <stdlib.h>
  #define SYS_ALLOC_FAIL NULL
  #define SYS_FREE_FAIL  -1
  #define CALL_SYS_ALLOC(P,S) \
    (0 == posix_memalign((void**)&(P),(size_t)BLOCK_SIZE,S) ? (P) : NULL)
  #define CALL_SYS_FREE(P,S) (free(P), 0)
#endif

/* assert */
#include <assert.h>

/* size_t */
#include <stddef.h>

/* uintptr_t */
#include <stdint.h>

/* */
#include "common.h"


#define LOCK_GET(lock) \
  ret = lock_get(lock);\
  assert(!ret);
#define LOCK_LET(lock) \
  ret = lock_let(lock);\
  assert(!ret);


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Block data structure. */
/******************************************************************************/
#define BLOCK_SIZE      4096   /* 2^12 */ /* If using mmap, this should be */
                                          /* alignment required by mmap. */
#define SUPERBLOCK_SIZE 262144 /* 2^18 */
#define BLOCK_CTR_FULL \
  ((BLOCK_SIZE-sizeof(struct block))/sizeof(struct vm_area))
#define SUPERBLOCK_CTR_FULL \
  ((SUPERBLOCK_SIZE-BLOCK_SIZE)/BLOCK_SIZE)

struct superblock;

struct block
{
  size_t vctr;
  lock_t lock;
  struct block * next;
  struct superblock * superblock;
  struct vm_area * head;
};

struct superblock
{
  size_t bctr;
  lock_t lock;
  struct superblock * prev;
  struct superblock * next;
  struct block * head;
};


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Memory pool data structure */
/******************************************************************************/
#define UNDES_BIN_NUM_GLOBAL 16


/* A per-thread instance of a memory pool. */
static __thread struct
{
  struct block * block;
} mpool = { NULL };

/* A per-process instance of a memory pool. */
static struct
{
  int n_undes;
  struct superblock * superblock;
  struct superblock * undes[UNDES_BIN_NUM_GLOBAL];
  lock_t lock;
} gpool;


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Translate a block to its corresponding superblock. */
/******************************************************************************/
static struct superblock *
block_2superblock(struct block * const block)
{
  return block->superblock;
}


/******************************************************************************/
/* Translate a vma to its corresponding block. */
/******************************************************************************/
static struct block *
vma_2block(struct vm_area * const vma)
{
  return (struct block*)((uintptr_t)vma&(~(uintptr_t)(BLOCK_SIZE-1)));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Allocate a new superblock. */
/******************************************************************************/
static struct superblock *
superblock_alloc(void)
{
  int ret;
  void * retp;
  struct superblock * superblock;

  if (0 != gpool.n_undes) {
    /* Designate superblock from global pool. */
    superblock = gpool.undes[--gpool.n_undes];
  }
  else {
    /* Allocate new superblock. */
    retp = CALL_SYS_ALLOC(superblock, SUPERBLOCK_SIZE);
    if (SYS_ALLOC_FAIL == retp) {
      return NULL;
    }

    /* Initialize superblock's lock. */
    ret = lock_init(&(superblock->lock));
    assert(!ret);
  }

  /* Set up new superblock. */
  superblock->bctr = 0;
  superblock->prev = NULL;
  superblock->next = NULL;
  superblock->head = (struct block*)((char*)superblock+BLOCK_SIZE);

  /* Setup block pointers. */
  /* TODO This is the step I would like to find a way to do lazily, i.e., as
   * each block is requested. */
  block = superblock->head;
  block->prev = NULL;
  block->next = (struct block*)((char*)block+BLOCK_SIZE);
  block->next->prev = block;
  for (i=1; i<SUPERBLOCK_CTR_FULL-1; ++i) {
    block->next = (struct block*)((char*)block+BLOCK_SIZE);
    block->next->prev = block;
    block = block->next;
  }
  block->next = NULL;

  /* Lock superblock. */
  LOCK_GET(&(superblock->lock));

  DBG_LOG(stderr, "allocated new superblock %p-%p\n", (void*)superblock,\
    (void*)((char*)superblock+SUPERBLOCK_SIZE));

  return superblock;
}


/******************************************************************************/
/* Free an existing superblock */
/******************************************************************************/
static void
superblock_free(struct superblock * const superblock)
{
  int ret;

  /* Lock gpool. */
  LOCK_GET(&(gpool.lock));

  /* Unlock superblock. */
  LOCK_LET(&(superblock->lock));

  if (gpool.n_undes < UNDES_BIN_NUM_GLOBAL) {
    /* Put superblock on global undesignated stack. */
    gpool.undes[gpool.n_undes++] = superblock;

    /* Unlock gpool. */
    LOCK_LET(&(gpool.lock));
  }
  else {
    /* Unlock gpool. */
    LOCK_LET(&(gpool.lock));

    /* Free superblock's lock. */
    ret = lock_free(&(superblock->lock));
    assert(!ret);

    /* Release back to system. */
    ret = CALL_SYS_FREE(superblock, SUPERBLOCK_SIZE);
    assert(SYS_FREE_FAIL != ret);
  }

  DBG_LOG(stderr, "freed superblock %p-%p\n", (void*)superblock,\
    (void*)((char*)superblock+SUPERBLOCK_SIZE));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Get next available block. */
/******************************************************************************/
static struct block *
block_alloc(void)
{
  int ret;
  struct block * head, * block;
  struct superblock * superblock;

  DBG_LOG(stderr, "allocating new block\n");

  /* Lock gpool. */
  LOCK_GET(&(gpool.lock));

  /* Check for cached superblock in gpool. */
  superblock = gpool.superblock;

  if (NULL == superblock) {
    superblock = superblock_alloc();
    if (NULL == superblock) {
      /* Unlock gpool. */
      LOCK_LET(&(gpool.lock));

      return NULL;
    }
    /* superblock will be locked by superblock_alloc(). */

    /* Prepend superblock to front of gpool's superblock cache. */
    superblock->prev = NULL;
    superblock->next = gpool.superblock;
    if (NULL != superblock->next) {
      superblock->next->prev = superblock;
    }
    gpool.superblock = superblock;

    /* Reset pointer for head block. This way, dangling pointers will get reset
     * correctly. See note in the if(NULL == block->next) condition below. */
    superblock->head->next = NULL;
  }
  else {
    /* Lock superblock. */
    LOCK_GET(&(superblock->lock));
  }

  /* Get head of superblock's block list. */
  block = superblock->head;
  DBG_LOG(stderr, "  :: 1) %p\n", (void*)superblock->head);

  /* Setup a reset block. */
  if (NULL == block->next) {
    /* Initialize block's lock. */
    ret = lock_init(&(block->lock));
    assert(!ret);

    /* Set up new block. */
    block->vctr = 0;
    block->head = (struct vm_area*)((char*)block+sizeof(struct block));
    block->superblock = superblock;
  }

  /* Increment block count. */
  superblock->bctr++;

  if (SUPERBLOCK_CTR_FULL == superblock->bctr) {
    /* Remove superblock from front of gpool's superblock cache. */
    gpool.superblock = superblock->next;
    if (NULL != superblock->next) {
      superblock->next->prev = NULL;
    }

    /* Unlock gpool. */
    LOCK_LET(&(gpool.lock));
  }
  else {
    /* Unlock gpool. */
    LOCK_LET(&(gpool.lock));

    if (NULL == block->next) {
      /* Set next pointer. */
      block->next = (struct block*)((char*)block+BLOCK_SIZE);

      /* Reset pointer for next block. If this block has never been previously
       * used, then if follows that neither has the next. Resetting pointers in
       * this way makes it so that memset'ing a superblock when it is
       * undesignated, unnecessary. */
      block->next->next = NULL;
    }

    /* Remove block from front of superblock's block list. */
    assert(block->next != block);
    superblock->head = block->next;

    DBG_LOG(stderr, "  :: 2) %p\n", (void*)superblock->head);
    DBG_LOG(stderr, "  ::    %p\n", (void*)block);
    DBG_LOG(stderr, "  ::    %p\n", (void*)block->next);
    if (NULL != block->next) {DBG_LOG(stderr, "  ::    %p\n", (void*)block->next);}
  }

  /* Unlock superblock. */
  LOCK_LET(&(superblock->lock));

  /* Lock block. */
  LOCK_GET(&(block->lock));

  assert(block_2superblock(block) == superblock);

  DBG_LOG(stderr, "allocated new block %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %d\n", (int)superblock->bctr);

  return block;
}


/******************************************************************************/
/* Return a block to the superblock to which it belongs. */
/******************************************************************************/
static void
block_free(struct block * const block)
{
  int ret;
  struct superblock * superblock;

  /* Get superblock. */
  superblock = block_2superblock(block);

  /* Lock superblock. */
  LOCK_GET(&(superblock->lock));

  /* Unlock block. */
  LOCK_LET(&(block->lock));

  /* FIXME If we free the block's lock, then how will we know if it needs to be
   * allocated later. */
  /* Free block's lock. */
  ret = lock_free(&(block->lock));
  assert(!ret);

  /* Decrement superblock's block count. */
  superblock->bctr--;

  if (0 == superblock->bctr) {
    /* Remove superblock from gpool's superblock cache. */
    if (gpool.superblock == superblock) {
      gpool.superblock = superblock->next;
    }
    else {
      superblock->prev->next = superblock->next;
    }
    if (NULL != superblock->next) {
      superblock->next->prev = superblock->prev;
    }

    /* Free superblock. */
    superblock_free(superblock);
    /* superblock_free() will unlock superblock. */

    DBG_LOG(stderr, "freed block %p\n", (void*)block);
    DBG_LOG(stderr, "  :: %p\n", (void*)superblock);
    DBG_LOG(stderr, "  :: 0\n");
  }
  else {
    /* Unlock superblock. */
    LOCK_LET(&(superblock->lock));

    DBG_LOG(stderr, "freed block %p\n", (void*)block);
    DBG_LOG(stderr, "  :: %p\n", (void*)superblock);
    DBG_LOG(stderr, "  :: %lu\n", superblock->bctr);
  }
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Get next available vma. */
/******************************************************************************/
struct vm_area *
vma_alloc(void)
{
  int ret;
  struct vm_area * vma;
  struct block * block;

  /* Check for cached block in mpool. */
  block = mpool.block;

  if (NULL == block) {
    block = block_alloc();
    if (NULL == block) {
      return NULL;
    }
    /* block will be locked by block_alloc(). */

    /* Cache block in mpool. */
    mpool.block = block;
    DBG_LOG(stderr, "  >> initialized cache block in mpool %p\n", (void*)block);
  }
  else {
    /* Lock block. */
    LOCK_GET(&(block->lock));
  }

  /* Get head of block's vma list. */
  vma = block->head;

  /* Increment block's vma count. */
  block->vctr++;

  if (BLOCK_CTR_FULL == block->vctr) {
    /* Reset cache block in mpool. */
    mpool.block = NULL;
    DBG_LOG(stderr, "  >>> reset cache block in mpool\n");
  }
  else {
    if (NULL == vma->vm_next) {
      /* Set next pointer. */
      vma->vm_next = vma+1;

      /* Reset pointer for next vma. If this vma has never been previously used,
       * then if follows that neither has the next. Resetting pointers in this
       * way makes it so that memset'ing a block when it is undesignated,
       * unnecessary. */
      vma->vm_next->vm_next = NULL;
    }

    /* Remove vma from front of block's vma list. */
    block->head = vma->vm_next;
  }

  /* Unlock block. */
  LOCK_LET(&(block->lock));

  DBG_LOG(stderr, "allocated new vma %p\n", (void*)vma);
  DBG_LOG(stderr, "  :: %d\n", (int)block->vctr);

  return vma;
}


/******************************************************************************/
/* Return a vma to the block to which it belongs. */
/******************************************************************************/
void
vma_free(struct vm_area * const vma)
{
  int ret;
  struct block * block;
  struct superblock * superblock;

  /* Get block. */
  block = vma_2block(vma);

  /* Lock block. */
  LOCK_GET(&(block->lock));

  /* Decrement block's vma count. */
  block->vctr--;

  if (0 == block->vctr) {
    /* Reset cache block in mpool. */
    if (mpool.block == block) {
      DBG_LOG(stderr, "  >> reset cache block in mpool\n");
      mpool.block = NULL;
    }

    block_free(block);
    /* block_free() will unlock block. */

    DBG_LOG(stderr, "freed vma %p\n", (void*)vma);
    DBG_LOG(stderr, "  :: %p\n", (void*)block);
    DBG_LOG(stderr, "  :: 0\n");
  }
  else {
    /* Prepend vma to front of block's vma list. */
    vma->vm_next = block->head;
    block->head = vma;

    /* Get superblock. */
    superblock = block_2superblock(block);

    /* Lock superblock. */
    LOCK_GET(&(superblock->lock));

    /* If block is not already in superblock's block list, then add it. */
    /* If superblock is not already in gpool's superblock list, then add it. */
    if (NULL == block->prev &&\
        NULL == block->next &&\
        superblock->head != block)
    {
      /* Prepend block to superblock's block list. */
      block->prev = NULL;
      block->next = superblock->head;
      if (NULL != block->next) {
        block->next->prev = block;
      }
      superblock->head = block;

      /* Lock gpool. */
      LOCK_GET(&(gpool.lock));

      if (NULL == superblock->prev &&\
          NULL == superblock->next &&\
          gpool.head != superblock)
      {
        /* Prepend superblock to front of gpool's superblock list. */
        superblock->prev = NULL;
        superblock->next = gpool.head;
        if (NULL != superblock->next) {
          superblock->next->prev = superblock;
        }
        gpool.head = superblock;
      }

      /* Unlock gpool. */
      LOCK_LET(&(gpool.lock));
    }

    /* Unlock superblock. */
    LOCK_LET(&(superblock->lock));

    /* Unlock block. */
    LOCK_LET(&(block->lock));

    DBG_LOG(stderr, "freed vma %p\n", (void*)vma);
    DBG_LOG(stderr, "  :: %p\n", (void*)block);
    DBG_LOG(stderr, "  :: %lu\n", block->vctr);
  }
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Initialize the memory pool. */
/******************************************************************************/
void
vma_mpool_init(void)
{
  int ret;

  gpool.n_undes = 0;

  ret = lock_init(&(gpool.lock));
  assert(!ret);
}


/******************************************************************************/
/* Free the memory pool. */
/******************************************************************************/
void
vma_mpool_free(void)
{
  int ret, i;

  while (gpool.superblock) {
    superblock_free(gpool.superblock);
  }

  for (i=0; i<gpool.n_undes; ++i) {
    /* Release back to system. */
    ret = CALL_SYS_FREE(gpool.undes[i], SUPERBLOCK_SIZE);
    assert(SYS_FREE_FAIL != ret);
  }

  ret = lock_free(&(gpool.lock));
  assert(!ret);
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


#ifdef TEST
/* assert */
#include <assert.h>

/* uintptr_t */
#include <inttypes.h>

/* malloc, free, EXIT_SUCCESS */
#include <stdlib.h>

#define N_ALLOC  (1<<22)
#define N_THREAD 2

int
main(void)
{
  int i;
  struct vm_area ** vma;

  vma = malloc(N_ALLOC*sizeof(struct vm_area*));
  assert(vma);

  vma_mpool_init();

  #pragma omp parallel for num_threads(N_THREAD)
  for (i=0; i<N_ALLOC/2; ++i) {
    vma[i] = vma_alloc();
    assert(vma[i]);
  }

  #pragma omp parallel for num_threads(N_THREAD)
  for (i=0; i<N_ALLOC/4; ++i) {
    vma_free(vma[i]);
  }

  #pragma omp parallel for num_threads(N_THREAD)
  for (i=N_ALLOC/2; i<N_ALLOC; ++i) {
    vma[i] = vma_alloc();
    assert(vma[i]);
  }

  #pragma omp parallel for num_threads(N_THREAD)
  for (i=N_ALLOC/4; i<N_ALLOC; ++i) {
    vma_free(vma[i]);
  }

  vma_mpool_free();

  free(vma);

  return EXIT_SUCCESS;
}
#endif
