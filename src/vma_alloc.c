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
  #ifndef _GNU_SOURCE
    #define _GNU_SOURCE
  #endif
  /* printf */
  #include <stdio.h>
  /* syscall, SYS_gettid */
  #include <sys/syscall.h>
  #define DBG_LOG \
    fprintf(stderr, "[%d] ", (int)syscall(SYS_gettid));\
    fprintf
#else
  #define stderr       NULL
  #define SYS_gettid   0
  #define syscall(cmd) 0
  static inline void noop(void const * const fmt, ...) { if (fmt) {} }
  #define DBG_LOG noop
#endif

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
    (0 == posix_memalign((void**)&(P),(size_t)BLOCK_SIZE,S) ? (P) : NULL)
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
#define BLOCK_SIZE      4096   /* 2^12 */ /* If using mmap, this should be
                                             alignment required by mmap. */
#define SUPERBLOCK_SIZE 262144 /* 2^18 */
#define BLOCK_CTR_FULL \
  ((BLOCK_SIZE-sizeof(struct block))/sizeof(struct vma)+1)
#define SUPERBLOCK_CTR_FULL \
  ((SUPERBLOCK_SIZE-sizeof(struct superblock))/sizeof(struct block)+1)

struct superblock;

struct block
{
  size_t vctr;
  struct block * prev;
  struct block * next;
  struct superblock * superblock;
  struct vma * head;
  struct vma vma[1];
};

struct superblock
{
  struct hdr
  {
    size_t bctr;
    struct superblock * prev;
    struct superblock * next;
    struct block * head;
  } hdr;
  /* This padding is necessary to ensure that the first block is properly
   * BLOCK_SIZE aligned. */
  char _pad[BLOCK_SIZE-sizeof(struct hdr)];
  struct block block[1];
};


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Memory pool data structure */
/******************************************************************************/
#define UNDES_BIN_NUM_THREAD 2
#define UNDES_BIN_NUM_GLOBAL 16


/* A per-process instance of a memory pool. */
__thread struct mpool
{
  int n_undes;
  struct block * head_block;
  struct superblock * head_superblock;
  struct superblock * undes[UNDES_BIN_NUM_THREAD];
} mpool = { 0, NULL, NULL, { NULL } };

static int n_undes;
static struct superblock * undes[UNDES_BIN_NUM_GLOBAL];
static ooc_lock_t undes_lock;


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Translate a vma to its corresponding block. */
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
vma_2block(struct vma * const vma)
{
  return (struct block*)((uintptr_t)vma&(~(uintptr_t)(BLOCK_SIZE-1)));
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Allocate a new superblock */
/******************************************************************************/
static struct superblock *
superblock_alloc(void)
{
  void * ret;
  struct superblock * superblock;

  ret = CALL_SYS_ALLOC(superblock, SUPERBLOCK_SIZE);
  if (SYS_ALLOC_FAIL == ret) {
    return NULL;
  }

  DBG_LOG(stderr, "allocated new superblock %p\n", (void*)superblock);

  return superblock;
}


/******************************************************************************/
/* Free an existing superblock */
/******************************************************************************/
static void
superblock_free(struct superblock * const superblock)
{
  int ret;

  ret = CALL_SYS_FREE(superblock, SUPERBLOCK_SIZE);
  assert(SYS_FREE_FAIL != ret);

  DBG_LOG(stderr, "freed superblock %p\n", (void*)superblock);
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
  struct block * block;
  struct superblock * superblock;

  block = NULL;
  superblock = mpool.head_superblock;

  if (NULL == superblock) {
    if (0 != mpool.n_undes) { /* Designate superblock from thread pool. */
      superblock = mpool.undes[--mpool.n_undes];
    }
    else {
      ret = lock_get(&(undes_lock));
      assert(!ret);
      if (0 != n_undes) {     /* Designate superblock from global pool. */
        superblock = undes[--n_undes];
        ret = lock_let(&(undes_lock));
        assert(!ret);
      }
      else {                  /* Allocate new superblock. */
        ret = lock_let(&(undes_lock));
        assert(!ret);
        superblock = superblock_alloc();
        if (NULL == superblock) {
          goto fn_fail;
        }
      }
    }

    /* Set up new superblock. */
    superblock->hdr.bctr = 0;
    superblock->hdr.prev = NULL;
    superblock->hdr.next = NULL;
    superblock->hdr.head = NULL;

    /* Set superblock as head of doubly-linked list. */
    superblock->hdr.prev = NULL;
    superblock->hdr.next = mpool.head_superblock;
    if (NULL != superblock->hdr.next) {
      superblock->hdr.next->hdr.prev = superblock;
    }
    mpool.head_superblock = superblock;

    /* Get first block in superblock. */
    block = superblock->block;

    /* Set block's pointer to the superblock. */
    block->superblock = superblock;

    /* Set head of superblock's singly-linked list to first block. */
    superblock->hdr.head = block;

    /* Reset pointer for head block. This way, dangling pointers will get reset
     * correctly. See note in the if(NULL == block->next) condition below. */
    block->next = NULL;
  }
  else {                                    /* Use head superblock. */
    /* Get head block of superblock's singly-linked-list. */
    block = superblock->hdr.head;
  }

  /* Increment block count. */
  superblock->hdr.bctr++;

  /* Remove full superblock from doubly-linked list. */
  if (SUPERBLOCK_CTR_FULL == superblock->hdr.bctr) {
    if (NULL != superblock->hdr.prev) {
      superblock->hdr.prev->hdr.next = superblock->hdr.next;
    }
    else {
      mpool.head_superblock = superblock->hdr.next;
    }
    if (NULL != superblock->hdr.next) {
      superblock->hdr.next->hdr.prev = superblock->hdr.prev;
    }

    superblock->hdr.prev = NULL;
    superblock->hdr.next = NULL;
  }
  else {
    if (NULL == block->next) {
      /* Set next pointer. */
      block->next = (struct block*)((char*)block+BLOCK_SIZE);

      /* Set next block's pointer to the superblock. */
      block->next->superblock = superblock;

      /* Reset pointer for next block. If this block has never been previously
       * used, then if follows that neither has the next. Resetting pointers in
       * this way makes it so that memset'ing a superblock when it is
       * undesignated, unnecessary. */
      block->next->next = NULL;
    }
  }

  /* Remove block from front of superblock's singly-linked list. */
  superblock->hdr.head = block->next;

  DBG_LOG(stderr, "allocated new block %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %d\n", (int)superblock->hdr.bctr);

  fn_fail:
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

  superblock = block_2superblock(block);

  DBG_LOG(stderr, "freeing block %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %p\n", (void*)superblock);
  DBG_LOG(stderr, "  :: %d\n", (int)superblock->hdr.bctr);

  /* Decrement block count. */
  superblock->hdr.bctr--;

  if (0 == superblock->hdr.bctr) {
    if (mpool.n_undes < UNDES_BIN_NUM_THREAD) {
      DBG_LOG(stderr, "  :: %d\n", (int)mpool.n_undes);
      /* Put superblock on thread undesignated stack. */
      mpool.undes[mpool.n_undes++] = superblock;
    }
    else {
      ret = lock_get(&(undes_lock));
      assert(!ret);
      if (n_undes < UNDES_BIN_NUM_GLOBAL) {
        /* Put superblock on global undesignated stack. */
        undes[n_undes++] = superblock;
        ret = lock_let(&(undes_lock));
        assert(!ret);
      }
      else {
        ret = lock_let(&(undes_lock));
        assert(!ret);
        /* Release back to system. */
        superblock_free(superblock);
      }
    }
  }
  else {
    /* Prepend block to front of superblock singly-linked list. */
    block->next = superblock->hdr.head;
    superblock->hdr.head = block;

    /* If not already part of mpool doubly-linked list, add superblock to it. */
    if (NULL == superblock->hdr.prev && NULL == superblock->hdr.next &&\
        superblock != mpool.head_superblock)
    {
      superblock->hdr.prev = NULL;
      superblock->hdr.next = mpool.head_superblock;
      if (NULL != superblock->hdr.next) {
        superblock->hdr.next->hdr.prev = superblock;
      }
      mpool.head_superblock = superblock;
    }
  }
}


/******************************************************************************/
/* ========================================================================== */
/******************************************************************************/


/******************************************************************************/
/* Get next available vma. */
/******************************************************************************/
struct vma *
vma_alloc(void)
{
  struct block * block;
  struct vma * vma;

  vma = NULL;
  block = mpool.head_block;

  if (NULL == block) {
    block = (struct block*)block_alloc();
    if (NULL == block) {
      goto fn_fail;
    }

    /* Set up new block. */
    block->vctr = 0;
    block->prev = NULL;
    block->next = NULL;
    block->head = NULL;

    /* Set block as head of doubly-linked list. */
    block->prev = NULL;
    block->next = mpool.head_block;
    if (NULL != block->next) {
      block->next->prev = block;
    }
    mpool.head_block = block;

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
  block->vctr++;

  /* Remove full block from doubly-linked list. */
  if (BLOCK_CTR_FULL == block->vctr) {
    if (NULL != block->prev) {
      block->prev->next = block->next;
    }
    else {
      mpool.head_block = block->next;
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

  DBG_LOG(stderr, "allocated new vma %p\n", (void*)vma);
  DBG_LOG(stderr, "  :: %d\n", (int)block->vctr);

  fn_fail:
  return vma;
}


/******************************************************************************/
/* Return a vma to the block to which it belongs. */
/******************************************************************************/
void
vma_free(struct vma * const vma)
{
  struct block * block;

  block = vma_2block(vma);

  DBG_LOG(stderr, "freeing vma %p\n", (void*)vma);
  DBG_LOG(stderr, "  :: %p\n", (void*)block);
  DBG_LOG(stderr, "  :: %d\n", (int)block->vctr);

  /* Decrement block count. */
  block->vctr--;

  if (0 == block->vctr) {
    block_free(block);
  }
  else {
    /* Prepend vma to front of block singly-linked list. */
    vma->next = block->head;
    block->head = vma;

    /* If not already part of mpool doubly-linked list, add block to it. */
    if (NULL == block->prev && NULL == block->next &&\
        block != mpool.head_block)
    {
      block->prev = NULL;
      block->next = mpool.head_block;
      if (NULL != block->next) {
        block->next->prev = block;
      }
      mpool.head_block = block;
    }
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

  n_undes = 0;

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

  DBG_LOG(stderr, " :: %d %d\n", mpool.n_undes, n_undes);

  /* FIXME */
  /* Need to release per thread undesignated superblocks */

  for (i=0; i<n_undes; ++i) {
    /* Release back to system. */
    superblock_free(undes[i]);
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

#define N_ALLOC  (BLOCK_CTR_FULL*SUPERBLOCK_CTR_FULL-1)
#define N_THREAD 1

int
main(void)
{
  int i;
  struct vma ** vma;

  vma = malloc(N_ALLOC*sizeof(struct vma*));
  assert(vma);

  printf("%lu %lu\n", sizeof(struct block), sizeof(struct superblock));
  printf("%lu %lu\n", BLOCK_CTR_FULL, SUPERBLOCK_CTR_FULL);

  vma_mpool_init();

  #pragma omp parallel for num_threads(N_THREAD) ordered
  for (i=0; i<N_ALLOC; ++i) {
    #pragma omp ordered
    {
      vma[i] = vma_alloc();
      assert(vma[i]);
    }
  }

  #pragma omp parallel for num_threads(N_THREAD) ordered
  for (i=0; i<N_ALLOC; ++i) {
    #pragma omp ordered
    {
      vma_free(vma[i]);
    }
  }

  vma_mpool_free();

  free(vma);

  return EXIT_SUCCESS;
}
#endif
