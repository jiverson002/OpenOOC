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

/* omp_get_wtime */
#include <omp.h>

/* printf, fopen, fclose, fscanf */
#include <stdio.h>

/* size_t */
#include <stddef.h>

/* EXIT_SUCCESS, EXIT_FAILURE */
#include <stdlib.h>

/* mmap, madvise, PROT_READ, PROT_WRITE, PROT_NONE, MADV_RANDOM */
#include <sys/mman.h>

/* struct rusage, getrusage */
#include <sys/resource.h>

/* time, time_t */
#include <time.h>

/* getopt, sync, sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* */
#include "util.h"


static void
S_swap_helper(int const what, size_t const p_size, size_t const c_size,
              size_t const n_pages, size_t const n_clust, char * const page,
              size_t const i)
{
  size_t j, k, l;

  /* If we organize the pages in a 2D matrix where each row is a cluster, then
   * we will traverse the pages in column major order, so that each cluster is
   * touched only once before every other cluster has been touched. */
  j = i % n_clust;             /* get cluster # (row) */
  k = i / n_clust;             /* get page # in cluster (column) */
  l = j * c_size + k * p_size; /* compute byte offset */

  assert(l < n_pages * p_size);
  assert(0 == (l & (p_size - 1)));

  if (what) {
    page[l] = (char)(l%CHAR_MAX);
  }
  else {
    assert((char)(l%CHAR_MAX) == page[l]);
  }
}


static void
S_swap_test(int const fill_dram, size_t const p_size, size_t const c_size,\
            size_t const n_pages, size_t const n_clust, size_t const n_threads)
{
  int ret;
  size_t i, m_size;
  double ts, te, r_sec, w_sec;
  struct rusage usage1, usage2, usage3;
  char * page;

  printf("===============================\n");
  printf(" Progress =====================\n");
  printf("===============================\n");

  /* Lock all current (i.e., overhead memory, due to the testing environment).
   * The purpose of this is to reduce the number of minor page faults. */
  ret = mlockall(MCL_CURRENT);
  assert(!ret);

  /* Fill DRAM with locked memory. */
  if (fill_dram) {
    printf("  Filling DRAM...\n");
    S_fill_dram();
  }

  /* Compute memory footprint. */
  m_size = n_pages * p_size;

  /* Allocate memory for swapping. */
  page = mmap(NULL, m_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,\
    -1, 0);
  assert(MAP_FAILED != page);

  /*
   * To truly force kernel to disable readahead on swap file, you must set
   * /proc/sys/vm/page-cluster to contain 0, instead of the default value of 3.
   *
   * From the kernel documentation (sysctl/vm.txt):
   *
   *  page-cluster
   *
   *  page-cluster controls the number of pages up to which consecutive pages
   *  are read in from swap in a single attempt. This is the swap counterpart to
   *  page cache readahead. The mentioned consecutivity is not in terms of
   *  virtual/physical addresses, but consecutive on swap space - that means
   *  they were swapped out together.
   *
   *  It is a logarithmic value - setting it to zero means "1 page", setting it
   *  to 1 means "2 pages", setting it to 2 means "4 pages", etc. Zero disables
   *  swap readahead completely.
   *
   *  The default value is three (eight pages at a time). There may be some
   *  small benefits in tuning this to a different value if your workload is
   *  swap-intensive.
   *
   *  Lower values mean lower latencies for initial faults, but at the same time
   *  extra faults and I/O delays for following faults if they would have been
   *  part of that consecutive pages readahead would have brought in.
   *
   *  This is achieved with the following command:
   *     sudo sh -c "echo '0' > /proc/sys/vm/page-cluster"
   */
  /* Coerce kernel to disable read ahead or caching. */
  ret = madvise(page, m_size, MADV_RANDOM);
  assert(!ret);

  printf("  Initializing pages...\n");
  /* Touch all pages once to make sure that they are populated so that they must
   * be swapped for subsequent access. */
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (i=0; i<n_pages; ++i) {
    S_swap_helper(1, p_size, c_size, n_pages, n_clust, page, i);
  }
  printf("  Initialized %.2f MiB...\n", (double)m_size/1048576.0);

  /* Try to clear buffer caches. */
  sync();

  /* Get usage statistics before read test. */
  ret = getrusage(RUSAGE_SELF, &usage1);
  assert(!ret);

  printf("  Testing swap read latency...\n");
  /* Test swap read latency. */
  S_gettime(&ts);
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (i=0; i<n_pages; ++i) {
    S_swap_helper(1, p_size, c_size, n_pages, n_clust, page, i);
  }
  S_gettime(&te);
  r_sec = S_getelapsed(&ts, &te);

  /* Get usage statistics before write test. */
  ret = getrusage(RUSAGE_SELF, &usage2);
  assert(!ret);

  printf("  Testing swap write latency...\n");
  /* Test swap write latency. */
  S_gettime(&ts);
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (i=0; i<n_pages; ++i) {
    S_swap_helper(0, p_size, c_size, n_pages, n_clust, page, i);
  }
  S_gettime(&te);
  w_sec = S_getelapsed(&ts, &te);

  /* Get usage statistics after all tests. */
  ret = getrusage(RUSAGE_SELF, &usage3);
  assert(!ret);

  /* Release memory. */
  munmap(page, m_size);

  /* Output results. */
  printf("\n");
  printf("===============================\n");
  printf(" Swap statistics ==============\n");
  printf("===============================\n");
  printf("  RD time (s)     = %11.5f\n", r_sec);
  printf("  RD bw (MiB/s)   = %11.0f\n", (double)(n_pages*p_size)/1048576.0/r_sec);
  printf("  RD latency (ns) = %11.0f\n", r_sec*1e9/(double)n_pages);
  printf("  RD latency (ns) = %11.0f\n", r_sec*1e9/(double)(usage2.ru_majflt-usage1.ru_majflt));
  printf("  WR time (s)     = %11.5f\n", w_sec);
  printf("  WR bw (MiB/s)   = %11.0f\n", (double)(n_pages*p_size)/1048576.0/w_sec);
  printf("  WR latency (ns) = %11.0f\n", w_sec*1e9/(double)n_pages);
  printf("  WR latency (ns) = %11.0f\n", w_sec*1e9/(double)(usage3.ru_majflt-usage2.ru_majflt));
  printf("\n");
  printf("===============================\n");
  printf(" Page faults ==================\n");
  printf("===============================\n");
  printf("  RD # Minor      = %11lu\n", usage2.ru_minflt-usage1.ru_minflt);
  printf("  RD # Major      = %11lu\n", usage2.ru_majflt-usage1.ru_majflt);
  printf("  WR # Minor      = %11lu\n", usage3.ru_minflt-usage2.ru_minflt);
  printf("  WR # Major      = %11lu\n", usage3.ru_majflt-usage2.ru_majflt);
}


int
main(int argc, char * argv[])
{
  int opt, fill_dram;
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
  fill_dram = 0;
  n_pages   = GB / p_size;
  n_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "dn:t:"))) {
    switch (opt) {
    case 'd':
      fill_dram = 1;
      break;
    case 'n':
      n_pages = (size_t)atol(optarg);
      break;
    case 't':
      n_threads = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-d] [-n n_pages] [-t n_threads]\n", argv[0]);
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
  S_swap_test(fill_dram, p_size, c_size, n_pages, n_clust, n_threads);

  return EXIT_SUCCESS;
}
