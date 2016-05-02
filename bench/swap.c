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

/* OOC library */
#include "src/ooc.h"

/* */
#include "util.h"


struct args
{
  int what;
  size_t p_size, c_size;
  size_t n_pages, n_clust;
  char * page;
};


static void
S_swap_kern(size_t const i, void * const state)
{
  int what;
  size_t j, k, l;
  size_t p_size, c_size, n_pages, n_clust;
  volatile char * page;
  struct args const * args;

  args = (struct args const*)state;
  what    = args->what;
  p_size  = args->p_size;
  c_size  = args->c_size;
  n_pages = args->n_pages;
  n_clust = args->n_clust;
  page    = args->page;

#if 1
  j = i % n_clust;  /* get cluster # */
  k = i / n_clust;  /* get entry in cluster */
  l = j * c_size + k * p_size;

  assert(l < n_pages * p_size);
  assert(0 == (l & (p_size - 1)));

  if (what) {
    page[l] = (char)(l%CHAR_MAX);
  }
  else {
    assert((char)(l%CHAR_MAX) == page[l]);
  }
#elif 0
  /* Pick a system page */
  jj = i / (8 * 8 * 8 * 8 * 8 * n_gb);
  /* Pick a 32KB chunk */
  kk = (i % (8 * 8 * 8 * 8 * 8 * n_gb)) / (8 * 8 * 8 * 8 * n_gb);
  /* Pick a 256KB chunk */
  ll = (i % (8 * 8 * 8 * 8 * n_gb)) / (8 * 8 * 8 * n_gb);
  /* Pick a 2MB chunk */
  mm = (i % (8 * 8 * 8 * n_gb)) / (8 * 8 * n_gb);
  /* Pick a 16MB chunk */
  nn = (i % (8 * 8 * n_gb)) / (8 * n_gb);
  /* Pick a 128MB chunk */
  oo = (i % (8 * n_gb)) / n_gb;
  /* Pick a 1GB chunk */
  pp = i % n_gb;

  assert(jj < 8);
  assert(kk < 8);
  assert(ll < 8);
  assert(mm < 8);
  assert(nn < 8);
  assert(oo < 8);
  assert(pp < n_gb);

  j = map8[jj] % (32 * KB / p_size);
  k = map8[kk];
  l = map8[ll];
  m = map8[mm];
  n = map8[nn];
  o = map8[oo];
  p = map[pp];

  idx = p*GB+o*128*MB+n*16*MB+m*2*MB+l*256*KB+k*32*KB+j*p_size;

  assert(idx < n_gb*GB);
  assert(0 == (idx & (p_size-1)));

  if (what) {
    page[idx] = (char)idx;
  }
  else {
    /*printf("i=%zu\n", i);
    printf("  jj=%zu, kk=%zu, ll=%zu, mm=%zu, nn=%zu, oo=%zu, pp=%zu\n", jj,\
      kk, ll, mm, nn, oo, pp);
    printf("  j=%zu, k=%zu, l=%zu, m=%zu, n=%zu, o=%zu, p=%zu, idx=%zu\n", j,\
      k, l, m, n, o, p, idx);*/

    assert((char)idx == page[idx]);
  }
#else
  /* FIXME map8 is size 8, but 32*KB/p_size could be greater than 8 or 0
   * if p_size < 4096 or p_size > 32*KB, respectively. So here we ensure that
   * system page size is at least 4096. */
  p_size = p_size < 4096 ? 4096 : p_size;
  p_size = p_size > 32*KB ? 32*KB : p_size;

  /* Pick a system page */
  for (jj=0; jj<32*KB/p_size; ++jj) {
    j = map8[jj];
    /* Pick a 32KB chunk */
    for (kk=0; kk<8; ++kk) {
      k = map8[kk];
      /* Pick a 256KB chunk */
      for (ll=0; ll<8; ++ll) {
        l = map8[ll];
        /* Pick a 2MB chunk */
        for (mm=0; mm<8; ++mm) {
          m = map8[mm];
          /* Pick a 16MB chunk */
          for (nn=0; nn<8; ++nn) {
            n = map8[nn];
            /* Pick a 128MB chunk */
            for (oo=0; oo<8; ++oo) {
              o = map8[oo];
              /* Pick a 1GB chunk */
              for (pp=0; pp<n_gb; ++pp) {
                p = map[pp];

                idx = p*GB+o*128*MB+n*16*MB+m*2*MB+l*256*KB+k*32*KB+j*p_size;

                if (what) {
                  mem[idx] = (char)idx;
                }
                else {
                  assert((char)idx == mem[idx]);
                }
              }
            }
          }
        }
      }
    }
  }
#endif
}


__ooc_decl ( static void S_swap_ooc )(size_t const i, void * const state);


__ooc_defn ( static void S_swap_ooc )(size_t const i, void * const state)
{
  S_swap_kern(i, state);
}


static void
S_swap_test(int const fill_dram, size_t const p_size, size_t const c_size,\
            size_t const n_pages, size_t const n_clust, size_t const n_threads,\
            size_t const n_fibers)
{
  int ret;
  size_t i, m_size;
  double ts, te, r_sec, w_sec;
  struct args args;
  struct rusage usage1, usage2, usage3;
  char * page;

  printf("===============================\n");
  printf(" Progress =====================\n");
  printf("===============================\n");

  if (n_fibers) {
    #pragma omp parallel num_threads(n_threads) private(ret)
    {
      /* Prepare OOC environment -- this is done prior to locking current memory
       * so that page faults are not incurred for loading context data. */
      ooc_set_num_fibers((unsigned int)n_fibers);
    }
  }

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

  /* Allocate memory for swaping. */
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

  /* Setup args struct. */
  args.p_size  = p_size;
  args.c_size  = c_size;
  args.n_pages = n_pages;
  args.n_clust = n_clust;
  args.page    = page;

  printf("  Initializing pages...\n");
  /* Touch all pages once to make sure that they are populated so that they must
   * be swapped for subsequent access. */
  args.what = 1;
  /* TODO Initialize pages in a completely random order... some of the memory
   * used to fill DRAM could be re-purposed to achieve this. */
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (i=0; i<n_pages; ++i) {
    page[i * p_size] = (char)((i * p_size) % CHAR_MAX);
  }

  /* Try to clear buffer caches. */
  sync();

  /* Get usage statistics before read test. */
  ret = getrusage(RUSAGE_SELF, &usage1);
  assert(!ret);

  printf("  Testing swap read latency...\n");
  /* Test swap read latency. */
  S_gettime(&ts);
  args.what = 0;
  if (n_fibers) {
    #pragma omp parallel num_threads(n_threads) private(ret)
    {
      #pragma omp single
      {
        /* `flush pages`, i.e., undo memory protections, so that fibers are able
         * to be invoked the next iteration that this memory is touched. */
        ret = mprotect(page, m_size, PROT_NONE);
        assert(!ret);
      }

      #pragma omp for nowait schedule(static)
      for (i=0; i<n_pages; ++i) {
        S_swap_ooc(i, &args);
      }
      ooc_wait(); /* Need this to wait for any outstanding fibers. */
      #pragma omp barrier

      ret = ooc_finalize(); /* Need this to and remove the signal handler. */
      assert(!ret);
    }
  }
  else {
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (i=0; i<n_pages; ++i) {
      S_swap_kern(i, &args);
    }
  }
  S_gettime(&te);
  r_sec = S_getelapsed(&ts, &te);

  /* Get usage statistics before write test. */
  ret = getrusage(RUSAGE_SELF, &usage2);
  assert(!ret);

  printf("  Testing swap write latency...\n");
  /* Test swap write latency. */
  S_gettime(&ts);
  args.what = 1;
  if (n_fibers) {
    if (0) {
      #pragma omp parallel num_threads(n_threads) private(ret)
      {
        #pragma omp single
        {
          /* `flush pages`, i.e., undo memory protections, so that fibers are
           * able to be invoked the next iteration that this memory is touched.
           * */
          ret = mprotect(page, m_size, PROT_NONE);
          assert(!ret);
        }

        #pragma omp for nowait schedule(static)
        for (i=0; i<n_pages; ++i) {
          S_swap_ooc(i, &args);
        }
        ooc_wait(); /* Need this to wait for any outstanding fibers. */
        #pragma omp barrier

        ret = ooc_finalize(); /* Need this to and remove the signal handler. */
        assert(!ret);
      }
    }
  }
  else {
    #pragma omp parallel for num_threads(n_threads) schedule(static)
    for (i=0; i<n_pages; ++i) {
      S_swap_kern(i, &args);
    }
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
  if (!n_fibers) {
    printf("  WR time (s)     = %11.5f\n", w_sec);
    printf("  WR bw (MiB/s)   = %11.0f\n", (double)(n_pages*p_size)/1048576.0/w_sec);
    printf("  WR latency (ns) = %11.0f\n", w_sec*1e9/(double)n_pages);
  }
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
  size_t n_pages, n_clust, n_fibers, n_threads;
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
  n_fibers  = 0;
  n_threads = 1;
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
  printf("  # clusters      = %11zu\n", n_clust);
  printf("  # pages         = %11zu\n", n_pages);
  printf("  # fibers        = %11zu\n", n_fibers);
  printf("  # threads       = %11zu\n", n_threads);
  printf("\n");

  /* Do test. */  
  S_swap_test(fill_dram, p_size, c_size, n_pages, n_clust, n_threads, n_fibers);

  return EXIT_SUCCESS;
}
