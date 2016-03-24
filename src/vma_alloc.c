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
  /*#define stderr NULL*/
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

/* printf */
#include <stdio.h>

/* NULL */
#include <stdlib.h>

/* uintptr_t */
#include <stdint.h>

/* */
#include "common.h"


#define LOCK_GET(lock,ctr) \
  ret = (lock_try(lock) ? (ctr++, lock_get(lock)) : 0);\
  assert(!ret);
#define LOCK_LET(lock) \
  ret = lock_let(lock);\
  assert(!ret);


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


#define BLOCK_SIZE      4096   /* 2^12 */ /* If using mmap, this should be */
                                          /* alignment required by mmap. */
#define SUPERBLOCK_SIZE 262144 /* 2^18 */
#define BLOCK_CTR_FULL \
  ((BLOCK_SIZE-sizeof(struct block))/sizeof(struct vm_area))
#define SUPERBLOCK_CTR_FULL \
  ((SUPERBLOCK_SIZE-BLOCK_SIZE)/BLOCK_SIZE)


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/*! Forward declarations. */
struct superblock;
struct mpool;

/*! Block data structure -- contains BLOCK_CTR_FULL vmas. */
struct block
{
  size_t vctr, lctr;
  lock_t lock;
  struct vm_area * head;
  struct superblock * superblock;
  struct mpool * mpool;
  struct block * prev;
  struct block * next;
};


/*! Superblock data structure -- contains SUPERBLOCK_CTR_FULL blocks. */
struct superblock
{
  size_t bctr, lctr, blctr;
  lock_t lock;
  struct block * head;
  struct superblock * prev;
  struct superblock * next;
};


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/*! A per-thread initalization variable to specify whether or not their
 *  per-thread memory pool has been initialized. */
static __thread int S_init = 0;


/*! A per-thread instance of a memory pool. */
static __thread struct mpool
{
  size_t lctr;
  lock_t lock;
  struct block * head;
} S_mpool;


/*! A per-process instance of a memory pool. */
static struct
{
  size_t actr;
  size_t lctr, mlctr, blctr, slctr;
  lock_t lock;
  struct superblock * head;
} S_gpool;


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/*! Translate a block to its corresponding superblock. */
static struct superblock *
S_block_2superblock(struct block * const block)
{
  return block->superblock;
}


/*! Translate a vma to its corresponding block. */
static struct block *
S_vma_2block(struct vm_area * const vma)
{
  return (struct block*)((uintptr_t)vma&(~(uintptr_t)(BLOCK_SIZE-1)));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/* TODO This is the step I would like to find a way to do lazily, i.e., as each
 * block/vma is requested. */
/*! Setup a superblock's block list and each block's vma list. */
static void
S_superblock_list_setup(struct superblock * const superblock)
{
  int ret;
  size_t i, j;
  struct vm_area * vma;
  struct block * block;

  /* Setup superblock's block list. */
  superblock->head = (struct block*)((uintptr_t)superblock+BLOCK_SIZE);
  block = superblock->head;
  block->prev = NULL;
  for (i=0; i<SUPERBLOCK_CTR_FULL; ++i) {
    block->vctr = 0;
    block->lctr = 0;
    block->superblock = superblock;
    block->head = (struct vm_area*)((uintptr_t)block+sizeof(struct block));
    if (SUPERBLOCK_CTR_FULL-1 == i) {
      block->next = NULL;
    }
    else {
      block->next = (struct block*)((uintptr_t)block+BLOCK_SIZE);
      block->next->prev = block;
    }

    /* Initialize block's lock. */
    ret = lock_init(&(block->lock));
    assert(!ret);

    /* Setup block's vma list. */
    vma = block->head;
    for (j=0; j<BLOCK_CTR_FULL-1; ++j) {
      vma->vm_next = vma+1;

      vma = vma->vm_next;
    }
    vma->vm_next = NULL;

    block = block->next;
  }
}


/* TODO This is the step I would like to find a way to do lazily, i.e., as each
 * block/vma is requested. */
/*! Destroy a superblock's block list and each block's vma list. */
static void
S_superblock_list_destroy(struct superblock * const superblock)
{
  int ret;
  size_t i;
  struct block * block;

  /* Destroy superblock's block list. */
  superblock->head = (struct block*)((uintptr_t)superblock+BLOCK_SIZE);
  block = superblock->head;
  for (i=0; i<SUPERBLOCK_CTR_FULL; ++i) {
    block->head = (struct vm_area*)((uintptr_t)block+sizeof(struct block));
    if (SUPERBLOCK_CTR_FULL-1 == i) {
      block->next = NULL;
    }
    else {
      block->next = (struct block*)((uintptr_t)block+BLOCK_SIZE);
    }

    /* Destroy block's lock. */
    ret = lock_free(&(block->lock));
    assert(!ret);

    /* Accumulate block wait counter. */
    superblock->blctr += block->lctr;

    block = block->next;
  }
}


/*! Get a superblock with available block[s]. */
static struct superblock *
S_superblock_get_and_lock(void)
{
  int ret;
  void * retp;
  struct superblock * superblock;

  if (NULL == (superblock=S_gpool.head)) {
    /* Allocate new superblock. */
    retp = CALL_SYS_ALLOC(superblock, SUPERBLOCK_SIZE);
    if (SYS_ALLOC_FAIL == retp) {
      return NULL;
    }
    S_gpool.actr++;

    /* Prepend superblock to S_gpool's superblock list. */
    S_gpool.head = superblock;
    superblock->prev = NULL;
    superblock->next = NULL;

    /* Setup superblock. */
    superblock->bctr = 0;
    superblock->lctr = 0;
    superblock->blctr = 0;

    /* Initialize superblock's lock. */
    ret = lock_init(&(superblock->lock));
    assert(!ret);

    /* Setup superblock's block list. */
    S_superblock_list_setup(superblock);
  }

  /* Lock superblock. */
  LOCK_GET(&(superblock->lock), superblock->lctr);
  
  DBG_LOG(stderr, "got superblock %p-%p\n", (void*)superblock,\
    (void*)((char*)superblock+SUPERBLOCK_SIZE));

  return superblock;
}


/*! Free a superblock with no used blocks. */
static void
S_superblock_unlock_and_put(struct superblock * const superblock)
{
  int ret;

  /* Lock S_gpool. */
  LOCK_GET(&(S_gpool.lock), S_gpool.lctr);

  /* Remove superblock from S_gpool's superblock list. */
  if (S_gpool.head == superblock) {
    S_gpool.head = superblock->next;
  }
  else {
    superblock->prev->next = superblock->next;
  }
  if (NULL != superblock->next) {
    superblock->next->prev = superblock->prev;
  }

  /* Unlock S_gpool. */
  LOCK_LET(&(S_gpool.lock));

  /* Unlock superblock. */
  LOCK_LET(&(superblock->lock));

  /* Free superblock's lock. */
  ret = lock_free(&(superblock->lock));
  assert(!ret);

  /* Destroy superblock's block list. */
  S_superblock_list_destroy(superblock);

  /* Lock S_gpool. */
  LOCK_GET(&(S_gpool.lock), S_gpool.lctr);

  /* Accumulate superblock's block wait counter. */
  S_gpool.blctr += superblock->blctr;
  /* Accumulate superblock's wait counter. */
  S_gpool.slctr += superblock->lctr;

  /* Unlock S_gpool. */
  LOCK_LET(&(S_gpool.lock));

  /* Release back to system. */
  ret = CALL_SYS_FREE(superblock, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);

  DBG_LOG(stderr, "put superblock %p-%p\n", (void*)superblock,\
    (void*)((char*)superblock+SUPERBLOCK_SIZE));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/*! Steal a block with available vma[s]. */
static struct block *
S_block_steal_and_lock(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock;

  /* Lock S_gpool. */
  LOCK_GET(&(S_gpool.lock), S_gpool.lctr);

  if (NULL == (superblock=S_superblock_get_and_lock())) {
    /* Unlock S_gpool. */
    LOCK_LET(&(S_gpool.lock));

    return NULL;
  }
  /* superblock will be locked by superblock_get(). */

  /* Get head of superblock's block list. */
  block = superblock->head;

  /* Lock block. */
  LOCK_GET(&(block->lock), block->lctr);

  /* Remove block from front of superblock's block list. */
  superblock->head = block->next;
  if (NULL != block->next) {
    block->next->prev = block->prev;
  }

  /* Increment superblock's block count. */
  superblock->bctr++;

  if (NULL == superblock->head) {
    /* Remove superblock from front of S_gpool's superblock list. */
    S_gpool.head = superblock->next;
    if (NULL != superblock->next) {
      superblock->next->prev = superblock->prev;
    }

    assert(SUPERBLOCK_CTR_FULL == superblock->bctr);
  }

  /* Unlock superblock. */
  LOCK_LET(&(superblock->lock));

  /* Unlock S_gpool. */
  LOCK_LET(&(S_gpool.lock));

  DBG_LOG(stderr, "stole block %p-%p\n", (void*)block,\
    (void*)((char*)block+BLOCK_SIZE));

  return block;
}


/*! Return a block with available vma[s]. */
static void
S_block_unlock_and_return(struct block * const block)
{
  int ret;
  struct superblock * superblock;

  /* Get superblock. */
  superblock = S_block_2superblock(block);

  /* Lock superblock. */
  LOCK_GET(&(superblock->lock), superblock->lctr);

  /* Prepend block to front of superblock's block list. */
  block->next = superblock->head;
  if (NULL != block->next) {
    block->next->prev = block;
  }
  superblock->head = block;

  /* Decrement superblock block count. */
  superblock->bctr--;

  if (0 == superblock->bctr) {
    /* Unlock superblock. */
    LOCK_LET(&(superblock->lock));

    /* Unlock block. */
    LOCK_LET(&(block->lock));

    S_superblock_unlock_and_put(superblock);
    /* S_superblock_unlock_and_put() will unlock superblock. */

  }
  else {
    if (NULL == block->next) {
      /* Lock S_gpool. */
      LOCK_GET(&(S_gpool.lock), S_gpool.lctr);

      /* Prepend superblock to front of S_gpool's superblock list. */
      superblock->next = S_gpool.head;
      if (NULL != superblock->next) {
        superblock->next->prev = superblock;
      }
      S_gpool.head = superblock;

      /* Unlock S_gpool. */
      LOCK_LET(&(S_gpool.lock));

      assert(SUPERBLOCK_CTR_FULL-1 == superblock->bctr);
    }

    /* Unlock superblock. */
    LOCK_LET(&(superblock->lock));

    /* Unlock block. */
    LOCK_LET(&(block->lock));
  }

  DBG_LOG(stderr, "returned block %p-%p\n", (void*)superblock,\
    (void*)((char*)superblock+SUPERBLOCK_SIZE));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


struct vm_area *
vma_alloc(void)
{
  int ret;
  struct vm_area * vma;
  struct block * block;

  if (0 == S_init) {
    S_init = 1;
    S_mpool.lctr = 0;
    ret = lock_init(&(S_mpool.lock));
    assert(!ret);
    S_mpool.head = NULL;
  }

  /* Check for cached block in S_mpool. */
  if (NULL == (block=S_mpool.head)) {
    block = S_block_steal_and_lock();
    if (NULL == block) {
      return NULL;
    }
    /* block will be locked by block_steal_and_lock(). */

    /* Lock S_mpool. */
    LOCK_GET(&(S_mpool.lock), S_mpool.lctr);

    /* Prepend block to S_mpool's block list. */
    S_mpool.head = block;
    block->prev = NULL;
    block->next = NULL;
    block->mpool = &S_mpool;
  }
  else {
    /* Lock block. */
    LOCK_GET(&(block->lock), block->lctr);

    /* Lock mpool. */
    LOCK_GET(&(S_mpool.lock), S_mpool.lctr);
  }

  /* Get head of block's vma list. */
  vma = block->head;

  /* Remove vma from front of block's vma list. */
  block->head = vma->vm_next;

  /* Increment block's vma count. */
  block->vctr++;

  if (NULL == block->head) {
    /* Remove block from front of mpool's block list. */
    S_mpool.head = block->next;
    if (NULL != block->next) {
      block->next->prev = block->prev;
    }

    assert(BLOCK_CTR_FULL == block->vctr);
  }

  /* Unlock S_mpool. */
  LOCK_LET(&(S_mpool.lock));

  /* Unlock block. */
  LOCK_LET(&(block->lock));

  DBG_LOG(stderr, "allocated vma %p\n", (void*)vma);

  return vma;
}


void
vma_free(struct vm_area * const vma)
{
  int ret;
  struct block * block;

  /* Get block. */
  block = S_vma_2block(vma);

  /* Lock block. */
  LOCK_GET(&(block->lock), block->lctr);

  /* Prepend vma to front of block's vma list. */
  vma->vm_next = block->head;
  block->head = vma;

  /* Decrement block's vma count. */
  block->vctr--;

  if (0 == block->vctr) {
    /* Lock block's mpool. */
    LOCK_GET(&(block->mpool->lock), block->mpool->lctr);

    /* Remove block from block's mpool's block list. */
    if (block->mpool->head == block) {
      block->mpool->head = block->next;
    }
    else {
      block->prev->next = block->next;
    }
    if (NULL != block->next) {
      block->next->prev = block->prev;
    }

    /* Unlock block's mpool. */
    LOCK_LET(&(block->mpool->lock));

    S_block_unlock_and_return(block);
    /* S_block_unlock_and_return() will unlock block. */
  }
  else {
    if (NULL == vma->vm_next) {
      /* Lock block's mpool. */
      LOCK_GET(&(block->mpool->lock), block->mpool->lctr);

      /* Prepend block to front of block's mpool's block list. */
      block->next = block->mpool->head;
      if (NULL != block->next) {
        block->next->prev = block;
      }
      block->mpool->head = block;

      /* Unlock block's mpool. */
      LOCK_LET(&(block->mpool->lock));

      assert(BLOCK_CTR_FULL-1 == block->vctr);
    }

    /* Unlock block. */
    LOCK_LET(&(block->lock));
  }

  DBG_LOG(stderr, "freed vma %p\n", (void*)vma);
}


void
vma_gpool_init(void)
{
  int ret;

  S_gpool.actr = 0;
  S_gpool.blctr = 0;
  S_gpool.slctr = 0;
  S_gpool.mlctr = 0;
  S_gpool.lctr = 0;

  ret = lock_init(&(S_gpool.lock));
  assert(!ret);
}


void
vma_gpool_free(void)
{
  int ret;

  /* TODO Until superblock_* cache unused superblocks, this is unnecessary if we
   * say that applications must free all memory they allocate. */
#if 0
  while (S_gpool.head) {
    S_superblock_unlock_and_put(S_gpool.head);
  }
#endif

  ret = lock_free(&(S_gpool.lock));
  assert(!ret);
}


void
vma_gpool_gather(void)
{
  int ret;

  /* Lock S_gpool. */
  LOCK_GET(&(S_gpool.lock), S_gpool.lctr);

  /* Accumulate threads's S_mpool wait counter. */
  S_gpool.mlctr += S_mpool.lctr;

  /* Unlock S_gpool. */
  LOCK_LET(&(S_gpool.lock));
}


void
vma_gpool_show(void)
{
  printf("Number of lock contentions...\n");
  printf("  block:       %lu\n", (long unsigned)S_gpool.blctr);
  printf("  superblock:  %lu\n", (long unsigned)S_gpool.slctr);
  printf("  thread pool: %lu\n", (long unsigned)S_gpool.mlctr);
  printf("  global pool: %lu\n", (long unsigned)S_gpool.lctr);
  printf("Number of system allocations...\n");
  printf("               %lu\n", (long unsigned)S_gpool.actr);
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


#ifdef TEST
/* assert */
#include <assert.h>

/* malloc, free, EXIT_SUCCESS */
#include <stdlib.h>


#define N_ALLOC  (1<<22)
#define N_THREAD 5

int null_ctr=0;


void superblock_1(void);
void superblock_2(void);
void superblock_3(void);
void superblock_4(void);
void superblock_5(void);
void block_1(void);
void block_2(void);
void block_3(void);
void block_4(void);
void block_5(void);
void block_6(void);
void block_7(void);


/*! This will test the functionality of S_superblock_get_and_lock() when S_gpool's
 *  superblock list is empty. */
void superblock_1(void)
{
  int ret;
  size_t ictr, jctr;
  struct vm_area * vma;
  struct block * block;
  struct superblock * superblock;

  /* ---- UNIT ---- */
  superblock = S_superblock_get_and_lock();

  /* ---- ASSERTIONS ---- */
  assert(superblock);
  assert(superblock == S_gpool.head);
  assert(NULL == superblock->prev);
  assert(NULL == superblock->next);
  for (ictr=0,block=superblock->head; block; block=block->next,++ictr) {
    assert(ictr < SUPERBLOCK_CTR_FULL);

    for (jctr=0,vma=block->head; vma; vma=vma->vm_next,++jctr) {
      assert(jctr < BLOCK_CTR_FULL);
    }
    assert(jctr == BLOCK_CTR_FULL);
  }
  assert(ictr == SUPERBLOCK_CTR_FULL);

  /* ---- CLEANUP ---- */
  LOCK_LET(&(superblock->lock));
  ret = lock_free(&(superblock->lock));
  assert(!ret);
  ret = CALL_SYS_FREE(superblock, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
  S_gpool.head = NULL;
}


/*! This will test the functionality of S_superblock_unlock_and_put() when
 *  superblock is the only superblock in S_gpool's superblock list. */
void superblock_2(void)
{
  int ret;
  void * retp;
  struct superblock * superblock;

  /* ---- SETUP ---- */
  retp = CALL_SYS_ALLOC(superblock, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  ret = lock_init(&(superblock->lock));
  assert(!ret);
  superblock->prev = NULL;
  superblock->next = NULL;
  S_gpool.head = superblock;
  LOCK_GET(&(superblock->lock), null_ctr);

  /* ---- UNIT ---- */
  S_superblock_unlock_and_put(superblock);

  /* ---- ASSERTIONS ---- */
  assert(NULL == S_gpool.head);

  /* ---- CLEANUP ---- */
}


/*! This will test the functionality of S_superblock_unlock_and_put() when
 *  superblock is the first superblock in S_gpool's superblock list. */
void superblock_3(void)
{
  int ret;
  void * retp;
  struct superblock * superblock1, * superblock2;

  /* ---- SETUP ---- */
  retp = CALL_SYS_ALLOC(superblock1, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  retp = CALL_SYS_ALLOC(superblock2, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  ret = lock_init(&(superblock1->lock));
  assert(!ret);
  ret = lock_init(&(superblock2->lock));
  assert(!ret);
  superblock1->prev = superblock2;
  superblock1->next = NULL;
  superblock2->prev = NULL;
  superblock2->next = superblock1;
  S_gpool.head = superblock2;
  LOCK_GET(&(superblock2->lock), null_ctr);

  /* ---- UNIT ---- */
  S_superblock_unlock_and_put(superblock2);

  /* ---- ASSERTIONS ---- */
  assert(superblock1 == S_gpool.head);
  assert(NULL == superblock1->prev);
  assert(NULL == superblock1->next);

  /* ---- CLEANUP ---- */
  ret = CALL_SYS_FREE(superblock1, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
  S_gpool.head = NULL;
}


/*! This will test the functionality of S_superblock_unlock_and_put() when
 *  superblock is the last superblock in S_gpool's superblock list. */
void superblock_4(void)
{
  int ret;
  void * retp;
  struct superblock * superblock1, * superblock2;

  /* ---- SETUP ---- */
  retp = CALL_SYS_ALLOC(superblock1, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  retp = CALL_SYS_ALLOC(superblock2, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  ret = lock_init(&(superblock1->lock));
  assert(!ret);
  ret = lock_init(&(superblock2->lock));
  assert(!ret);
  superblock1->prev = superblock2;
  superblock1->next = NULL;
  superblock2->prev = NULL;
  superblock2->next = superblock1;
  S_gpool.head = superblock2;
  LOCK_GET(&(superblock1->lock), null_ctr);

  /* ---- UNIT ---- */
  S_superblock_unlock_and_put(superblock1);

  /* ---- ASSERTIONS ---- */
  assert(superblock2 == S_gpool.head);
  assert(NULL == superblock2->prev);
  assert(NULL == superblock2->next);

  /* ---- CLEANUP ---- */
  ret = CALL_SYS_FREE(superblock2, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
  S_gpool.head = NULL;
}


/*! This will test the functionality of S_superblock_unlock_and_put() when
 *  superblock is an internal node in S_gpool's superblock list. */
void superblock_5(void)
{
  int ret;
  void * retp;
  struct superblock * superblock1, * superblock2, * superblock3;

  /* ---- SETUP ---- */
  retp = CALL_SYS_ALLOC(superblock1, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  retp = CALL_SYS_ALLOC(superblock2, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  retp = CALL_SYS_ALLOC(superblock3, SUPERBLOCK_SIZE);
  assert(SYS_ALLOC_FAIL != retp);
  ret = lock_init(&(superblock1->lock));
  assert(!ret);
  ret = lock_init(&(superblock2->lock));
  assert(!ret);
  ret = lock_init(&(superblock3->lock));
  assert(!ret);
  superblock1->prev = superblock2;
  superblock1->next = NULL;
  superblock2->prev = superblock3;
  superblock2->next = superblock1;
  superblock3->prev = NULL;
  superblock3->next = superblock2;
  S_gpool.head = superblock3;
  LOCK_GET(&(superblock2->lock), null_ctr);

  /* ---- UNIT ---- */
  S_superblock_unlock_and_put(superblock2);

  /* ---- ASSERTIONS ---- */
  assert(S_gpool.head = superblock3);
  assert(NULL == superblock3->prev);
  assert(superblock1 == superblock3->next);
  assert(superblock3 == superblock1->prev);
  assert(NULL == superblock1->next);

  /* ---- CLEANUP ---- */
  ret = CALL_SYS_FREE(superblock1, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
  ret = CALL_SYS_FREE(superblock3, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
  S_gpool.head = NULL;
}


/*! This will test the functionality of S_block_steal_and_lock() when S_gpool's
 *  superblock list is empty. */
void block_1(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock;

  /* ---- UNIT ---- */
  block = S_block_steal_and_lock();

  /* ---- ASSERTIONS ---- */
  assert(block);
  superblock = S_block_2superblock(block);
  assert(superblock == S_gpool.head);
  assert(block != superblock->head);
  assert(block->next == superblock->head);

  /* ---- CLEANUP ---- */
  LOCK_LET(&(block->lock));
  ret = lock_free(&(block->lock));
  assert(!ret);
  ret = lock_free(&(superblock->lock));
  assert(!ret);
  ret = CALL_SYS_FREE(superblock, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);
  S_gpool.head = NULL;
}


/*! This will test the functionality of S_block_steal_and_lock() when the stolen
 *  block is the last block in the superblock and superblock is the only
 *  superblock in S_gpool's superblock list. */
void block_2(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock;

  /* ---- SETUP ---- */
  superblock = S_superblock_get_and_lock();
  superblock->bctr = SUPERBLOCK_CTR_FULL-1; /* fake the next block as the last. */
  superblock->head->next = NULL;            /* ... */
  LOCK_LET(&(superblock->lock));

  /* ---- UNIT ---- */
  block = S_block_steal_and_lock();

  /* ---- ASSERTIONS ---- */
  assert(block);
  assert(superblock == S_block_2superblock(block));
  assert(superblock != S_gpool.head);
  assert(superblock->next == S_gpool.head);
  assert(block != superblock->head);
  assert(NULL == superblock->head);
  assert(NULL == block->next);

  /* ---- CLEANUP ---- */
  S_gpool.head = superblock;
  S_superblock_unlock_and_put(superblock);
}


/*! This will test the functionality of S_block_steal_and_lock() when the stolen
 *  block is the last block in the superblock and superblock is not the only
 *  superblock in S_gpool's superblock list. */
void block_3(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock1, * superblock2;

  /* ---- SETUP ---- */
  superblock1 = S_superblock_get_and_lock();
  LOCK_LET(&(superblock1->lock));
  S_gpool.head = NULL;
  superblock2 = S_superblock_get_and_lock();
  superblock2->next = superblock1;
  superblock1->prev = superblock2;
  superblock2->bctr = SUPERBLOCK_CTR_FULL-1; /* fake the next block as the last. */
  superblock2->head->next = NULL;            /* ... */
  LOCK_LET(&(superblock2->lock));

  /* ---- UNIT ---- */
  block = S_block_steal_and_lock();

  /* ---- ASSERTIONS ---- */
  assert(block);
  assert(superblock2 == S_block_2superblock(block));
  assert(superblock2 != S_gpool.head);
  assert(superblock1 == S_gpool.head);
  assert(NULL == superblock1->prev);
  assert(NULL == superblock1->next);
  assert(block != superblock2->head);
  assert(NULL == superblock2->head);
  assert(NULL == block->next);

  /* ---- CLEANUP ---- */
  S_gpool.head = superblock2;
  S_superblock_unlock_and_put(superblock2);
  S_gpool.head = superblock1;
  S_superblock_unlock_and_put(superblock1);
}


/*! This will test the functionality of S_block_unlock_and_return() to a empty
 *  superblock. */
void block_4(void)
{
  struct block * block;

  /* ---- SETUP ---- */
  block = S_block_steal_and_lock();
  assert(block);

  /* ---- UNIT ---- */
  S_block_unlock_and_return(block);

  /* ---- ASSERTIONS ---- */
  assert(NULL == S_gpool.head);

  /* ---- CLEANUP ---- */
}


/*! This will test the functionality of S_block_unlock_and_return() to a non-full
 *  superblock. */
void block_5(void)
{
  struct block * block1, * block2;
  struct superblock * superblock;

  /* ---- SETUP ---- */
  block1 = S_block_steal_and_lock();
  assert(block1);
  block2 = S_block_steal_and_lock();
  assert(block2);
  superblock = S_block_2superblock(block1);
  assert(superblock == S_block_2superblock(block2));

  /* ---- UNIT ---- */
  S_block_unlock_and_return(block1);

  /* ---- ASSERTIONS ---- */
  assert(superblock == S_block_2superblock(block2));
  assert(superblock == S_gpool.head);
  assert(NULL == superblock->next);
  assert(block1 == superblock->head);

  /* ---- CLEANUP ---- */
  S_superblock_unlock_and_put(superblock);
}


/*! This will test the functionality of S_block_unlock_and_return() to a full
 *  superblock that is the only superblock in S_gpool's superblock list. */
void block_6(void)
{
  struct block * block;
  struct superblock * superblock;

  /* ---- SETUP ---- */
  block = S_block_steal_and_lock();
  assert(block);
  superblock = S_block_2superblock(block);
  S_gpool.head = NULL;
  superblock->bctr = SUPERBLOCK_CTR_FULL; /* fake a full superblock. */
  superblock->head = NULL;                /* ... */

  /* ---- UNIT ---- */
  S_block_unlock_and_return(block);

  /* ---- ASSERTIONS ---- */
  assert(superblock == S_block_2superblock(block));
  assert(superblock == S_gpool.head);
  assert(NULL == superblock->prev);
  assert(NULL == superblock->next);
  assert(block == superblock->head);
  assert(NULL == block->next);
  assert(NULL == block->prev);

  /* ---- CLEANUP ---- */
  S_superblock_unlock_and_put(superblock);
}


/*! This will test the functionality of S_block_unlock_and_return() to a full
 *  superblock that is not the only superblock in S_gpool's superblock list. */
void block_7(void)
{
  int ret;
  struct block * block;
  struct superblock * superblock1, * superblock2;

  /* ---- SETUP ---- */
  block = S_block_steal_and_lock();
  assert(block);
  superblock1 = S_block_2superblock(block);
  superblock1->bctr = SUPERBLOCK_CTR_FULL; /* fake a full superblock. */
  superblock1->head = NULL;                /* ... */
  S_gpool.head = NULL;
  superblock2 = S_superblock_get_and_lock();
  LOCK_LET(&(superblock2->lock));

  /* ---- UNIT ---- */
  S_block_unlock_and_return(block);

  /* ---- ASSERTIONS ---- */
  assert(superblock1 == S_block_2superblock(block));
  assert(superblock1 == S_gpool.head);
  assert(NULL == superblock1->prev);
  assert(superblock2 == superblock1->next);
  assert(block == superblock1->head);
  assert(NULL == block->next);
  assert(NULL == block->prev);

  /* ---- CLEANUP ---- */
  S_superblock_unlock_and_put(superblock1);
  S_superblock_unlock_and_put(superblock2);
  assert(NULL == S_gpool.head);
}


int
main(void)
{
  int i;
  struct vm_area ** vma;

  vma_gpool_init();

  superblock_1();
  superblock_2();
  superblock_3();
  superblock_4();
  superblock_5();

  block_1();
  block_2();
  block_3();
  block_4();
  block_5();
  block_6();
  block_7();

  vma = malloc(N_ALLOC*sizeof(struct vm_area*));
  assert(vma);

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

  free(vma);

  vma_gpool_free();

#ifdef SHOW
  #pragma omp parallel num_threads(N_THREAD)
  vma_gpool_gather();
  vma_gpool_show();
#endif

  return EXIT_SUCCESS;
}
#endif
