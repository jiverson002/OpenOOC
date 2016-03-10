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


/* assert */
#include <assert.h>

/* uintptr_t, uint8_t */
#include <inttypes.h>

/* NULL */
#include <stdlib.h>

/* mmap, mprotect, PROT_READ, PROT_WRITE, MAP_FAILED */
#include <sys/mman.h>

/* OOC_PAGE_SIZE */
#include "include/ooc.h"

/* */
#include "common.h"


#define RNDUP(M,N) (1+(((M)-1)/(N)))
#define ALIGN(M)   (((M)+(size_t)OOC_PAGE_SIZE-1)&(~((size_t)OOC_PAGE_SIZE-1)))


void *
ooc_malloc(size_t const size)
{
  int ret;
  size_t info_sz, data_sz, mmap_sz;
  struct vma * vma;

  /* Compute segment sizes. */
  data_sz = ALIGN(size);
  info_sz = ALIGN(sizeof(struct vma)+RNDUP(data_sz, (size_t)OOC_PAGE_SIZE));
  mmap_sz = info_sz+data_sz;

  /* Allocate memory for new vma with read-only protection. */
  vma = mmap(NULL, mmap_sz, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == vma) {
    goto fn_fail;
  }

  /* Make info segment readable and writeable. */
  ret = mprotect(vma, info_sz, PROT_READ|PROT_WRITE);
  if (ret) {
    goto fn_cleanup;
  }

  /* Setup vma struct. */
  vma->pflags = (uint8_t*)((char*)vma+sizeof(struct vma));

  /* Insert new vma into page table. */
  ret = ptbl_insert(&_ptbl, &(vma->pte), (uintptr_t)((char*)vma+info_sz), size);
  if (ret) {
    goto fn_cleanup;
  }

  /* Return pointer to data segment. */
  return (void*)(vma->pte.b);

  fn_cleanup:
  /* Deallocate memory that was allocated for new vma. */
  ret = munmap(vma, mmap_sz);
  assert(!ret);

  fn_fail:
  /* Return NULL pointer. */
  return NULL;
}


void
ooc_free(void * ptr)
{
  int ret;
  size_t info_sz, data_sz, mmap_sz;
  struct vma * vma;

  /* Find the node corresponding to the offending address. */
  /* FIXME If we structure a vma differently, this could be a constant time
   * address manipulation instead of a splay tree lookup. However, since this is
   * the free function, it may not be that performance critical. */
  ret = ptbl_find_and_lock(&_ptbl, (uintptr_t)ptr, (void*)&vma);
  assert(!ret);

  /* Remove from splay tree. This will be fast, since ptbl_find_and_lock will
   * splay vma to top of tree. */
  ret = ptbl_remove(&_ptbl, vma->pte.b);
  assert(!ret);

  /* Deallocate memory. */
  /* Compute segment sizes. */
  data_sz = ALIGN(vma->pte.s);
  info_sz = ALIGN(sizeof(struct vma)+RNDUP(data_sz, (size_t)OOC_PAGE_SIZE));
  mmap_sz = info_sz+data_sz;

  /* Allocate memory for new vma. */
  ret = munmap(vma, mmap_sz);
  assert(!ret);
}


#ifdef TEST
/* EXIT_SUCCESS */
#include <stdlib.h>

int
main(void)
{
  return EXIT_SUCCESS;
}
#endif
