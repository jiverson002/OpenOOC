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

/* CHAR_MAX */
#include <limits.h>

/* jmp_buf, setjmp, siglongjmp */
#include <setjmp.h>

/* printf */
#include <stdio.h>

/* EXIT_SUCCESS */
#include <stdlib.h>

/* memset */
#include <string.h>

/* mmap, munmap, madvise, MADV_RANDOM */
#include <sys/mman.h>

/* time, time_t */
#include <time.h>

/* getopt, sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* OOC library */
#include "src/ooc.h"

/* */
#include "../src/common.h"

/* */
#include "util.h"


/*! Dummy variable to force page in core. */
static volatile char dummy;


static void
S_aio_helper(size_t const i, aioreq_t * const aioreq, size_t const p_size,
             size_t const c_size, size_t const n_pages, size_t const n_clust,
             char * const mem, int const what)
{
  int ret;
  size_t j, k, l;

  j = i % n_clust;  /* get cluster # */
  k = i / n_clust;  /* get entry in cluster */
  l = j * c_size + k * p_size;

  assert(l < n_pages * p_size);
  assert(0 == (l & (p_size - 1)));

  if (what) {
    ret = ooc_aio_read(mem+l, p_size, aioreq);
    assert(!ret);
  }
  else {
    /* Async I/O thread will give the buffer read/write access. */
    ret = mprotect(mem+l, p_size, PROT_READ|PROT_WRITE);
    assert(!ret);

    /* Brute force the kernel to page fault the buffer into core. */
    dummy = *(char volatile*)(mem+l);
  }
}


static void
S_aio_test(int const fill_dram, size_t const p_size, size_t const c_size,
           size_t const n_pages, size_t const n_clust, size_t const n_threads,
           size_t const n_fibers)
{
  double t_sec=0.0;
  char * mem;

  printf("===============================\n");
  printf(" Progress =====================\n");
  printf("===============================\n");

  #pragma omp parallel num_threads(n_threads) shared(t_sec,mem)
  {
    int ret;
    size_t i, fid, m_size;
    ssize_t retval;
    double ts, te;
    aioreq_t * aioreq, * req;

    m_size = n_pages * p_size;
    aioreq = NULL;

    /* Setup per-thread async-io context. */
    if (n_fibers) {
      /* XXX Second parameter should be a pointer to an async-i/o context, but
       * since we are not using Native Async-I/O, NULL is fine. */
      ret = aio_setup((unsigned int)n_fibers, NULL);
      assert(!ret);

      /* Allocate requests for the fibers. */
      aioreq = malloc(n_fibers*sizeof(*aioreq));
      assert(aioreq);
    }

    #pragma omp single
    {
      /* Lock all current (i.e., overhead memory, due to the testing
       * environment). The purpose of this is to reduce the number of minor page
       * faults. */
      ret = mlockall(MCL_CURRENT);
      assert(!ret);

      /* Allocate memory for async-i/o. */
      mem = mmap(NULL, m_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,\
        -1, 0);
      assert(MAP_FAILED != mem);

      printf("  Initializing pages...\n");
    }

    /* Touch all pages once to make sure that they are populated so that they
     * must be swapped for subsequent access. */
    /* TODO Initialize pages in a completely random order... some of the
     * memory used to fill DRAM could be re-purposed to achieve this. */
    #pragma omp for schedule(static)
    for (i=0; i<n_pages; ++i) {
      mem[i * p_size] = (char)((i * p_size) % CHAR_MAX);
    }

    #pragma omp single
    {
      /* Try to clear buffer caches. */
      sync();

      /* Coerce kernel to disable read ahead or caching. */
      ret = madvise(mem, m_size, MADV_RANDOM);
      assert(!ret);

      /* Fill DRAM with locked memory. */
      if (fill_dram) {
        printf("  Filling DRAM...\n");
        S_fill_dram();
      }
    }

    /* Testing */
    printf("  Testing async-i/o latency...\n");
    S_gettime(&ts);
    if (n_fibers) {
      /* Post as many async-i/o requests as possible. */
      for (fid=0,i=0; fid<n_fibers; ++fid,++i) {
        S_aio_helper(i, &(aioreq[fid]), p_size, c_size, n_pages, n_clust, mem,
          1);
      }

      /* Post asyhnc-i/o requests until all n_pages are done. */
      do {
        /* Wait until an async-i/o requests has completed. */
        req = aio_suspend();

        /* Retrieve and validate the return value for the request. */
        retval = aio_return(req);
        assert((ssize_t)p_size == retval);

        /* Incremented completed request count. */
        i++;

        /* Issue another async-i/o request, if there are pages left. */
        if (i<n_pages/n_threads) {
          S_aio_helper(i, req, p_size, c_size, n_pages, n_clust, mem, 1);
        }
      } while (i<n_pages/n_threads);

      /* Wait for any outstanding requests. */
      for (fid=0; fid<n_fibers; ++fid) {
        while (-1 != aio_error(&(aioreq[fid]))) {
          /* Wait until an async-i/o requests has completed. */
          req = aio_suspend();

          /* Retrieve and validate the return value for the request. */
          retval = aio_return(req);
          assert((ssize_t)p_size == retval);
        }
      }
    }
    else {
      #pragma omp for schedule(static)
      for (i=0; i<n_pages; ++i) {
        S_aio_helper(i, req, p_size, c_size, n_pages, n_clust, mem, 0);
      }
    }
    S_gettime(&te);

    #pragma omp critical
    if (S_getelapsed(&ts, &te) > t_sec) {
      t_sec = S_getelapsed(&ts, &te);
    }

    /* Teardown */
    if (n_fibers) {
      /* XXX Parameter should be an async-i/o context, but since we are not
       * using Native Async-I/O, 0 is fine. */
      ret = ooc_aio_destroy(0);
      assert(!ret);

      /* Free memory. */
      free(aioreq);
    }
    #pragma omp single
    {
      ret = munmap(mem, m_size);
      assert(!ret);
    }
  }

  /* Output results. */
  printf("===============================\n");
  printf(" aio statistics ===============\n");
  printf("===============================\n");
  printf("  Time (s)        = %11.5f\n", t_sec);
  printf("  bw (MiB/s)      = %11.0f\n", (double)(n_pages*p_size)/1048576.0/t_sec);
  printf("  latency (ns)    = %11.0f\n", t_sec*1e9/(double)n_pages);
  printf("\n");
}


int
main(int argc, char * argv[])
{
  int opt, fill_dram;
  size_t p_size, c_size;
  size_t n_pages, n_clust, n_threads, n_fibers;
  time_t now;

  /* Get current time. */
  now = time(NULL);

  /* Get page size. */
  p_size = (size_t)sysconf(_SC_PAGESIZE);

  /* Get page-cluster value. */
  c_size = (1lu << S_getpagecluster()) * p_size;

  /* Parse command-line parameters. */
  fill_dram = 0;
  n_pages   = GB / p_size;
  n_threads = 1;
  n_fibers  = 1;
  while (-1 != (opt=getopt(argc, argv, "df:n:t:"))) {
    switch (opt) {
    case 'd':
      fill_dram = 1;
      break;
    case 'f':
      n_fibers = (size_t)atol(optarg);
      break;
    case 'n':
      n_pages = (size_t)atol(optarg);
      break;
    case 't':
      n_threads = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-d] [-f n_fibers] [-n n_pages] "\
        "[-t n_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  /*assert(n_fibers >= 0);*/
  assert(n_pages > 0);
  assert(n_threads > 0);

  /* Compute the testing memory footprint. */
  n_clust = n_pages * p_size / c_size;

  /* Output build info. */
  S_printbuildinfo(&now, p_size, c_size);

  /* Output problem info. */
  printf("===============================\n");
  printf(" Parameters ===================\n");
  printf("===============================\n");
  printf("  # pages         = %11zu\n", n_pages);
  printf("  # threads       = %11zu\n", n_threads);
  printf("  # fibers        = %11zu\n", n_fibers);
  printf("\n");

  /* Do test. */  
  S_aio_test(fill_dram, p_size, c_size, n_pages, n_clust, n_threads, n_fibers);

  return EXIT_SUCCESS;
}
