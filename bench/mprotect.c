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

/* printf */
#include <stdio.h>

/* EXIT_SUCCESS, posix_memalign, free */
#include <stdlib.h>

/* memset */
#include <string.h>

/* madvise, MADV_RANDOM */
#include <sys/mman.h>

/* time, time_t */
#include <time.h>

/* getopt, sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* */
#include "util.h"


static void
S_mprotect_helper(size_t const i, size_t const p_size, size_t const c_size,
                  size_t const n_pages, size_t const n_clust, char * const mem)
{
  int ret;
  size_t j, k, l;

  /* See note in swap.c about how this will access pages. */
  j = i % n_clust;  /* get cluster # */
  k = i / n_clust;  /* get entry in cluster */
  l = j * c_size + k * p_size;

  assert(l < n_pages * p_size);
  assert(0 == (l & (p_size - 1)));

  ret = mprotect(mem+l, p_size, PROT_READ|PROT_WRITE);
  assert(!ret);
}


static void
S_mprotect_test(size_t const p_size, size_t const c_size, size_t const n_pages,
                size_t const n_clust, size_t const n_threads)
{
  int ret;
  size_t i;
  double ts, te, t_sec;
  char * mem;

  ret = posix_memalign((void**)&mem, p_size, n_pages*p_size);
  assert(!ret);

  /* Coerce kernel to disable read ahead or caching. */
  ret = madvise(mem, n_pages*p_size, MADV_RANDOM);
  assert(!ret);

  /* Touch mem once to make sure it is populated so that no page faults are
   * incurred. */
  memset(mem, 1, n_pages*p_size);

  /* Testing mprotect latency. */
  /* TODO This test is not accurate because by the time the last page is
   * mprotect'd, there are at most three vma's, the one being protected, and
   * possibly one before and/or after. To get the true latency, we should try to
   * control the state of the vma's where around the page, not sure if this is
   * possible in any meaningful way though. */
  S_gettime(&ts);
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (i=0; i<n_pages; ++i) {
    S_mprotect_helper(i, p_size, c_size, n_pages, n_clust, mem);
  }
  S_gettime(&te);
  t_sec = S_getelapsed(&ts, &te);

  free(mem);

  /* Output results. */
  printf("===============================\n");
  printf(" mprotect statistics ==========\n");
  printf("===============================\n");
  printf("  Time (s)        = %11.5f\n", t_sec);
  printf("  bw (MiB/s)      = %11.0f\n", (double)(n_pages*p_size)/1048576.0/t_sec);
  printf("  latency (ns)    = %11.0f\n", t_sec*1e9/(double)n_pages);
  printf("\n");
}


int
main(int argc, char * argv[])
{
  int opt;
  size_t p_size, c_size;
  size_t n_pages, n_clust, n_threads;
  time_t now;

  /* Get current time. */
  now = time(NULL);

  /* Get page size. */
  p_size = (size_t)sysconf(_SC_PAGESIZE);

  /* Get page-cluster value. */
  c_size = (1lu << S_getpagecluster()) * p_size;

  /* Parse command-line parameters. */
  n_pages   = GB / p_size;
  n_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "n:t:"))) {
    switch (opt) {
    case 'n':
      n_pages = (size_t)atol(optarg);
      break;
    case 't':
      n_threads = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-n n_pages] [-t n_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
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
  printf("  # clusters      = %11zu\n", n_clust);
  printf("  # pages         = %11zu\n", n_pages);
  printf("  # threads       = %11zu\n", n_threads);
  printf("\n");

  /* Do test. */  
  S_mprotect_test(p_size, c_size, n_pages, n_clust, n_threads);

  return EXIT_SUCCESS;
}
