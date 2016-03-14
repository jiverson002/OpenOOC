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
  #define stderr       NULL
  static inline void noop(void * unused, ...) { if (unused) { (void)0; } }
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
  ((SUPERBLOCK_SIZE-sizeof(struct superblock))/BLOCK_SIZE)

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
  struct hdr
  {
    size_t bctr;
    lock_t lock;
    struct superblock * prev;
    struct superblock * next;
    struct block * head;
  } hdr;
  /* This padding is necessary to ensure that the first block is properly
   * BLOCK_SIZE aligned. This works because the superblock itself is BLOCK_SIZE
   * aligned. */
  char _pad[BLOCK_SIZE-sizeof(struct hdr)];
};


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Memory pool data structure */
/******************************************************************************/
#define UNDES_BIN_NUM_THREAD 2
#define UNDES_BIN_NUM_GLOBAL 16


/* A per-thread instance of a memory pool. */
static __thread struct
{
  int n_undes;
  struct superblock * superblock;
  struct superblock * undes[UNDES_BIN_NUM_THREAD];
} mpool = { 0, NULL, { NULL } };

/* A per-process instance of a memory pool. */
static struct
{
  int n_undes;
  struct superblock * undes[UNDES_BIN_NUM_GLOBAL];
  ooc_lock_t lock;
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

  superblock = NULL;

  if (0 == mpool.n_undes) {
    ret = lock_get(&(gpool.lock));
    assert(!ret);
    if (0 == gpool.n_undes) {   /* Allocate new superblock. */
      ret = lock_let(&(gpool.lock));
      assert(!ret);
      retp = CALL_SYS_ALLOC(superblock, SUPERBLOCK_SIZE);
      if (SYS_ALLOC_FAIL == retp) {
        return NULL;
      }
      ret = lock_init(&(superblock->hdr.lock));
      assert(!ret);
    }
    else {                      /* Designate superblock from global pool. */
      superblock = gpool.undes[--gpool.n_undes];
      ret = lock_let(&(gpool.lock));
      assert(!ret);
    }
  }
  else  {                       /* Designate superblock from thread pool. */
    superblock = mpool.undes[--mpool.n_undes];
  }

  /* Set up new superblock. */
  superblock->hdr.bctr = 0;
  superblock->hdr.prev = NULL;
  superblock->hdr.head = (struct block*)((char*)superblock+sizeof(struct superblock));

  /* Set superblock as head of mpool's superblock cache. */
  superblock->hdr.next = mpool.superblock;
  if (NULL != superblock->hdr.next) {
    superblock->hdr.next->hdr.prev = superblock;
  }
  mpool.superblock = superblock;

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

  /* Remove superblock from mpool's superblock cache. */
  if (superblock == mpool.superblock) {
    mpool.superblock = superblock->hdr.next;
  }
  else {
    superblock->hdr.prev->hdr.next = superblock->hdr.next;
  }
  if (NULL != superblock->hdr.next) {
    superblock->hdr.next->hdr.prev = superblock->hdr.prev;
  }

  if (mpool.n_undes < UNDES_BIN_NUM_THREAD) {
    /* Put superblock on thread undesignated stack. */
    mpool.undes[mpool.n_undes++] = superblock;
  }
  else {
    ret = lock_get(&(gpool.lock));
    assert(!ret);
    if (gpool.n_undes < UNDES_BIN_NUM_GLOBAL) {
      /* Put superblock on global undesignated stack. */
      gpool.undes[gpool.n_undes++] = superblock;
      ret = lock_let(&(gpool.lock));
      assert(!ret);
    }
    else {
      ret = lock_let(&(gpool.lock));
      assert(!ret);
      ret = lock_free(&(superblock->hdr.lock));
      assert(!ret);
      /* Release back to system. */
      ret = CALL_SYS_FREE(superblock, SUPERBLOCK_SIZE);
      assert(SYS_FREE_FAIL != ret);
    }
  }

  DBG_LOG(stderr, "freed superblock %p-%p\n", (void*)superblock,\
    (void*)((char*)superblock+SUPERBLOCK_SIZE));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


#if 0
/******************************************************************************/
/* Get next available block. */
/******************************************************************************/
static struct block *
block_alloc(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock;

  block = NULL;

  /* Check mpool's superblock cache. */
  superblock = mpool.superblock;

  if (NULL == superblock) {
    block = NULL;
  }
  else {
    /* Lock the superblock. */
    ret = lock_get(&(superblock->hdr.lock));
    assert(!ret);

    block = superblock->hdr.head;
  }

  if (NULL == block) {
    superblock = superblock_alloc();
    if (NULL == superblock) {
      return NULL;
    }

    /* Lock the superblock -- we don't actually need to lock here, but this way,
     * we can unconditionally unlock later in the function. */
    ret = lock_get(&(superblock->hdr.lock));
    assert(!ret);

    /* Get head of superblock's singly-linked list. */
    block = superblock->hdr.head;

    /* Reset pointer for head block. This way, dangling pointers will get reset
     * correctly. See note in the if(NULL == block->next) condition below. */
    block->next = NULL;
  }

  /* Initialize block lock. */
  ret = lock_init(&(block->lock));
  assert(!ret);

  /* Increment block count. */
  superblock->hdr.bctr++;

  /* FIXME superblock shouldn't be removed from front of mpool's superblock
   * cache until its last block is full. */
  if (SUPERBLOCK_CTR_FULL == superblock->hdr.bctr) {
    /* Remove superblock from front of mpool's superblock cache. */
    mpool.superblock = superblock->hdr.next;
    if (NULL != superblock->hdr.next) {
      superblock->hdr.next->hdr.prev = NULL;
    }
  }
  else {
    if (NULL == block->next) {
      /* Set next pointer. */
      block->next = (struct block*)((char*)block+BLOCK_SIZE);

      /* Reset pointer for next block. If this block has never been previously
       * used, then if follows that neither has the next. Resetting pointers in
       * this way makes it so that memset'ing a superblock when it is
       * undesignated, unnecessary. */
      block->next->next = NULL;
      block->next->head = NULL;
    }
  }

  /* Set up new block. */
  block->vctr = 0;
  block->head = (struct vm_area*)((char*)block+sizeof(struct block));
  block->superblock = superblock;

  /* Unlock the superblock. */
  ret = lock_let(&(superblock->hdr.lock));
  assert(!ret);

  DBG_LOG(stderr, "allocated new block %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %d\n", (int)superblock->hdr.bctr);

  return block;
}
#else
/******************************************************************************/
/* Get next available block. */
/******************************************************************************/
static struct block *
block_alloc(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock;

  /* Check mpool's block cache. */
  block = mpool.block;

  if (NULL == block) {
    /* Lock gpool. */
    ret = lock_get(&(gpool.lock));
    assert(!ret);

    /* Check gpool's superblock cache. */
    superblock = gpool.superblock;

    if (NULL == superblock) {
      /* Unlock gpool. */
      ret = lock_let(&(gpool.lock));
      assert(!ret);

      /* Allocate a new superblock. */
      superblock = superblock_alloc();
      if (NULL == superblock) {
        return NULL;
      }

      /* Lock new superblock. */
      ret = lock_get(&(superblock->hdr.lock));
      assert(!ret);

      /* Lock gpool. */
      ret = lock_get(&(gpool.lock));
      assert(!ret);

      // TODO add new superblock to gpool */

      /* Unlock gpool. */
      ret = lock_let(&(gpool.lock));
      assert(!ret);

      /* Get head of new superblock's block list. */
      block = superblock->hdr.head;
    }
    else {
      /* Lock superblock. */
      ret = lock_get(&(superblock->hdr.lock));
      assert(!ret);

      /* Unlock gpool. */
      ret = lock_let(&(gpool.lock));
      assert(!ret);
    }
  }
  else {
    /* Lock block. */
    ret = lock_get(&(block->lock));
    assert(!ret);

    /* Get superblock pointer. */
    superblock = block_2superblock(block);

    /* Lock superblock. */
    ret = lock_get(&(superblock->hdr.lock));
    assert(!ret);
  }

  /* Increment block count. */
  superblock->hdr.bctr++;

  if (SUPERBLOCK_CTR_FULL == superblock->hdr.bctr) {
    /* Remove superblock from front of mpool's superblock cache. */
    mpool.superblock = superblock->hdr.next;
    if (NULL != superblock->hdr.next) {
      superblock->hdr.next->hdr.prev = NULL;
    }
  }
  else {
    if (NULL == block->next) {
      /* Set next pointer. */
      block->next = (struct block*)((char*)block+BLOCK_SIZE);

      /* Reset pointer for next block. If this block has never been previously
       * used, then if follows that neither has the next. Resetting pointers in
       * this way makes it so that memset'ing a superblock when it is
       * undesignated, unnecessary. */
      block->next->next = NULL;
      block->next->head = NULL;
    }
  }

  /* Set up new block. */
  block->vctr = 0;
  block->head = (struct vm_area*)((char*)block+sizeof(struct block));
  block->superblock = superblock;

  /* Prepend block->next to mpool's block cache. */
  mpool.block = block->next;
  block->next = NULL;
  superblock->hdr.next = mpool.superblock;
  if (NULL != superblock->hdr.next) {
    superblock->hdr.next->hdr.prev = superblock;
  }
  mpool.superblock = superblock;

  DBG_LOG(stderr, "allocated new block %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %d\n", (int)superblock->hdr.bctr);

  return block;
}
#endif


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

  DBG_LOG(stderr, "freeing block %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %p\n", (void*)superblock);
  DBG_LOG(stderr, "  :: %d\n", (int)superblock->hdr.bctr);

  /* Uninitialize block lock. */
  ret = lock_free(&(block->lock));
  assert(!ret);

  /* Lock the superblock. */
  ret = lock_get(&(superblock->hdr.lock));
  assert(!ret);

  /* Decrement superblock's block count. */
  superblock->hdr.bctr--;

  if (0 == superblock->hdr.bctr) {
    /* Unlock the superblock. */
    ret = lock_let(&(superblock->hdr.lock));
    assert(!ret);

    superblock_free(superblock);
  }
  else {
    /* Prepend block to front of superblock's singly-linked list. */
    block->next = superblock->hdr.head;
    superblock->hdr.head = block;

    if (SUPERBLOCK_CTR_FULL-1 == superblock->hdr.bctr) {
      /* Prepend superblock to front of mpool's superblock cache. */
      superblock->hdr.prev = NULL;
      superblock->hdr.next = mpool.superblock;
      if (NULL != superblock->hdr.next) {
        superblock->hdr.next->hdr.prev = superblock;
      }
      mpool.superblock = superblock;
    }

    /* Unlock the superblock. */
    ret = lock_let(&(superblock->hdr.lock));
    assert(!ret);
  }
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


#if 0
/******************************************************************************/
/* Get next available vma. */
/******************************************************************************/
struct vm_area *
vma_alloc(void)
{
  int ret;
  struct vm_area * vma;
  struct block * block;
  struct superblock * superblock;

  /* Check mpool's superblock cache. */
  superblock = mpool.superblock;

  if (NULL == superblock) {
    block = NULL;
  }
  else {
    /* Check superblock's singly-linked list. */
    block = superblock->hdr.head;
  }

  if (NULL == block) {
    vma = NULL;
  }
  else {
    /* Check block's singly-linked list. */
    vma = block->head;
  }

  if (NULL == vma) {
    block = block_alloc();
    if (NULL == block) {
      return NULL;
    }

    /* Get superblock. */
    superblock = block_2superblock(block);

    /* Get head of block's singly-linked list. */
    vma = block->head;

    /* Reset pointer for head vma. This way, dangling pointers will get reset
     * correctly. See note in the if(NULL == vma->vm_next) condition below. */
    vma->vm_next = NULL;
  }

  /* Increment block count. */
  block->vctr++;

  if (BLOCK_CTR_FULL == block->vctr) {
    /* Remove block from superblocks's singly-linked list. */
    superblock->hdr.head = block->next;
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
  }

  /* Remove vma from front of block's singly-linked list. */
  block->head = vma->vm_next;

  DBG_LOG(stderr, "allocated new vma %p\n", (void*)vma);
  DBG_LOG(stderr, "  :: %d\n", (int)block->vctr);

  return vma;
}
#else
/******************************************************************************/
/* Get next available vma. */
/******************************************************************************/
struct vm_area *
vma_alloc(void)
{
  int ret;
  struct vm_area * vma;
  struct block * block;
  struct superblock * superblock;

  /* Check mpool's vma cache. */
  vma = mpool.vma;

  if (NULL == vma) {
    block = block_alloc();
    if (NULL == block) {
      return NULL;
    }

    /* Get head of block's singly-linked list. */
    vma = block->head;

    /* Reset pointer for head vma. This way, dangling pointers will get reset
     * correctly. See note in the if(NULL == vma->vm_next) condition below. */
    vma->vm_next = NULL;
  }
  else {
    /* Get block pointer. */
    block = vma_2block(vma);
  }

  // TODO lock block */

  /* Increment block's vma counter. */
  block->vctr++;

  if (BLOCK_CTR_FULL == block->vctr) {
    // TODO lock superblock

    /* Remove block from superblocks's block cache. */
    superblock->hdr.head = block->next;

    // TODO unlock superblock
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
  }

  /* Remove vma from block's vma cache. */
  block->head = vma->vm_next;

  // TODO unlock block

  /* Remove vma from mpool's vma cache. */
  mpool.vma = vma->vm_next;

  DBG_LOG(stderr, "allocated new vma %p\n", (void*)vma);
  DBG_LOG(stderr, "  :: %d\n", (int)block->vctr);

  return vma;
}
#endif


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
  /* Get superblock. */
  superblock = block_2superblock(block);

  DBG_LOG(stderr, "freeing vma %p\n", (void*)vma);
  DBG_LOG(stderr, "  :: %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %p\n", (void*)block->superblock);
  DBG_LOG(stderr, "  :: %d\n", (int)block->vctr);

  #if 0
  /* Lock the block. */
  ret = lock_get(&(block->lock));
  assert(!ret);
  #endif

  /* TODO Need to lock block for this. */
  /* Decrement block count. */
  block->vctr--;

  if (0 == block->vctr) {
    #if 0
    /* Unlock the block. */
    ret = lock_let(&(block->lock));
    assert(!ret);
    #endif

    block_free(block);
  }
  else {
    /* Prepend vma to front of block singly-linked list. */
    vma->vm_next = block->head;
    block->head = vma;

    if (BLOCK_CTR_FULL-1 == block->vctr) {
      /* Lock the superblock. */
      ret = lock_get(&(superblock->hdr.lock));
      assert(!ret);

      /* Prepend block to superblock's singly-linked list. */
      block->next = superblock->hdr.head;
      superblock->hdr.head = block;

      /* Unlock the superblock. */
      ret = lock_let(&(superblock->hdr.lock));
      assert(!ret);
    }

    #if 0
    /* Unlock the block. */
    ret = lock_let(&(block->lock));
    assert(!ret);
    #endif
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

  /* FIXME */
  /* Need to release per thread undesignated superblocks */

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
#define N_THREAD 1

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
