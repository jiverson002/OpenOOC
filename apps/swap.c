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

/* omp_get_wtime */
#include <omp.h>

/* jmp_buf, setjmp, longjmp */
#include <setjmp.h>

/* struct sigaction, sigaction */
#include <signal.h>

/* printf */
#include <stdio.h>

/* EXIT_SUCCESS, posix_memalign, malloc, free */
#include <stdlib.h>

/* memset */
#include <string.h>

/* madvise, MADV_RANDOM */
#include <sys/mman.h>

/* struct rusage, getrusage */
#include <sys/resource.h>

/* CLOCK_MONOTONIC, struct timespec, clock_gettime */
#include <time.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* Fix for architectures which do not implement MAP_STACK. In these cases, it
 * should add nothing to the bitmask. */
#ifndef MAP_STACK
  #define MAP_STACK 0
#endif


#define XSTR(X) #X
#define STR(X)  XSTR(X)

#define KB (1lu<<10) /* 1KB */
#define MB (1lu<<20) /* 1MB */
#define GB (1lu<<30) /* 1GB */


static void
S_gettime(double * const t)
{
  *t = omp_get_wtime();
}


static double
S_getelapsed(double const * const ts, double * const te)
{
  return *te-*ts;
}


static void
S_fill_dram(void)
{
  unsigned char * ptr;

  /* Allocate GBs (or pages if GB < page). */
  for (;;) {
    ptr = mmap(NULL, GB, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0);
    if (MAP_FAILED == ptr) {
      break;
    }
    memset(ptr, (unsigned char)-1, GB);
  }

  /* Allocate MBs (or pages if MB < page). */
  for (;;) {
    ptr = mmap(NULL, MB, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0);
    if (MAP_FAILED == ptr) {
      break;
    }
    memset(ptr, (unsigned char)-1, MB);
  }

#if 0
  /* Allocate KBs (or pages if KB < page). */
  for (;;) {
    ptr = mmap(NULL, KB, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0);
    if (MAP_FAILED == ptr) {
      break;
    }
    memset(ptr, (unsigned char)-1, KB);
  }
#endif
}


static void
S_swap_helper(size_t const n_gb, char * const mem, size_t const * const map,\
              int const write_flag)
{
  size_t j, k, l, m, n, o, p, jj, kk, ll, mm, nn, oo, pp, idx, p_size;
  size_t const map8[8] = { 5, 3, 7, 1, 0, 2, 6, 4 };

  p_size = (size_t)sysconf(_SC_PAGESIZE);
  /* FIXME map8 is size 8, but 32*KB/p_size could be greater than 8 if p_size <
   * 4096, so here we ensure that system page size is at least 4096. */
  p_size = p_size < 4096 ? 4096 : p_size;

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

                idx = p*GB+o*128*MB+n*16*MB+m*2*MB+l*256*KB+k*32*KB+j*4*KB;

                if (write_flag) {
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
}


static void
S_swap_run(size_t const n_iters, size_t const n_pages, size_t const n_threads,\
           size_t const n_fibers)
{
  int ret;
  size_t p_size, m_size, n_gb;
  double ts, te, t_sec;
  size_t i, p, pp, tmp;
  //struct args args;
  struct rusage usage1, usage2;
  char * pages;
  size_t * map;

  p_size = (size_t)sysconf(_SC_PAGESIZE);
  n_gb   = 1+((n_pages*p_size-1)/GB);
  m_size = n_gb*GB;

  map = mmap(NULL, n_gb*sizeof(*map), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != map);

  /* Shuffle the gigabyte map. */ 
  for (p=0; p<n_gb; ++p) {
    map[p] = p;
  }
  for (p=0; p<n_gb; ++p) {
    pp = (size_t)rand()%n_gb;
    tmp = map[pp];
    map[pp] = map[p];
    map[p] = tmp;
  }

  /* Lock all current (i.e., overhead memory, due to the testing environment).
   * The purpose of this is to reduce the number of minor page faults. */
  ret = mlockall(MCL_CURRENT);
  assert(!ret);

  /* Fill DRAM with locked memory. */
  S_fill_dram();

  /* Allocate memory for swaping. */
  pages = mmap(NULL, m_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,\
    -1, 0);
  assert(MAP_FAILED != pages);

  /* Coerce kernel to disable read ahead or caching. */
  ret = madvise(pages, m_size, MADV_RANDOM);
  assert(!ret);
  /* To truly force kernel to disable readahead on swap file, you must set
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

#if 0
  printf("============================\n");
  printf(" Status ====================\n");
  printf("============================\n");
  printf("  Generating vector...\n");
  S_gettime(&ts);
  S_vecfill(n, a);
  S_gettime(&te);
  t1_sec = S_getelapsed(&ts, &te);

  if (n_fibers) {
    /* Prepare OOC environment. */
    ooc_set_num_fibers((unsigned int)n_fibers);
  }

  /* Setup args struct. */
  args.n_pages = n_pages;
  args.pages = pages;

  printf("  Sorting vector blocks...\n");
  S_gettime(&ts);
  /* Sort each block. */
  if (n_fibers) {
    #pragma omp parallel num_threads(n_threads) private(is,ie,ret)
    {
      for (is=0; is<n; is+=y) {
        ie = is+y < n ? is+y : n;

        #pragma omp single
        {
          /* `flush pages`, i.e., undo memory protections, so that fibers
           * are able to be invoked the next iteration that this memory is
           * touched. */
          ret = mprotect(a+is, (ie-is)*sizeof(*a), PROT_NONE);
          assert(!ret);
          ret = mprotect(b+is, (ie-is)*sizeof(*b), PROT_NONE);
          assert(!ret);
        }

        #pragma omp for nowait schedule(static)
        for (i=is; i<ie; i+=x) {
          S_vecsort_ooc(i, &args);
        }
        ooc_wait(); /* Need this to wait for any outstanding fibers. */
        #pragma omp barrier
      }

      ret = ooc_finalize(); /* Need this to and remove the signal handler. */
      assert(!ret);
    }
  }
  else {
    for (is=0; is<n; is+=y) {
      ie = is+y < n ? is+y : n;

      #pragma omp parallel for num_threads(n_threads) schedule(static)
      for (i=is; i<ie; i+=x) {
        S_vecsort_kern(i, &args);
      }
    }
  }
  S_gettime(&te);
  t2_sec = S_getelapsed(&ts, &te);
#endif

#if 1
  /* Touch pages initially so they exist in swap. */
  for (i=0; i<m_size; ++i) {
    pages[i] = (char)i;
  }

  /* Try to clear buffer caches. */
  sync();

  /* Get usage statistics before tests. */
  ret = getrusage(RUSAGE_SELF, &usage1);
  assert(!ret);

  /* Test. */
  S_gettime(&ts);
  for (i=0; i<m_size; ++i) {
    assert((char)i == pages[i]);
  }
  S_gettime(&te);
  t_sec = S_getelapsed(&ts, &te);
#else
  /* Touch pages once to make sure that they are populated so that they must be
   * swapped for subsequent access. */
  S_swap_helper(n_gb, pages, map, 1);

  /* Get usage statistics before tests. */
  ret = getrusage(RUSAGE_SELF, &usage1);
  assert(!ret);

  /* Testing swap latency. */
  S_gettime(&ts);
  for (i=0; i<n_iters; ++i) {
    S_swap_helper(n_gb, pages, map, 0);
  }
  S_gettime(&te);
  t_sec = S_getelapsed(&ts, &te);

#if 0
  S_gettime(&ts);
  for (i=0; i<n_iters; ++i) {
    S_swap_helper(m_size, pages, map, 1);
  }
  S_gettime(&te);
  w_nsec = S_getelapsed(&ts, &te);
#endif
#endif

  /* Get usage statistics after tests. */
  ret = getrusage(RUSAGE_SELF, &usage2);
  assert(!ret);

  /* Release memory. */
  munmap(map, n_gb*sizeof(*map));
  munmap(pages, m_size);

  printf("%zu/%zu %zu/%zu\n", usage1.ru_minflt, usage1.ru_majflt,\
    usage2.ru_minflt, usage2.ru_majflt);

  /* Output results. */
  printf("============================\n");
  printf(" Swap latency ==============\n");
  printf("============================\n");
  printf("  Time (s)     = %11.5f\n", t_sec);
  printf("  # SysPages   = %11lu\n", n_iters*n_pages);
  printf("  SysPages/s   = %11.0f\n", (double)(n_iters*n_pages)/t_sec);
  printf("  Latency      = %11.5f\n", t_sec/(double)(n_iters*n_pages));
  printf("\n");
  printf("============================\n");
  printf(" I/O Statistics ============\n");
  printf("============================\n");
  printf("  # PageMinor  = %11lu\n", usage2.ru_minflt-usage1.ru_minflt);
  printf("  # PageMajor  = %11lu\n", usage2.ru_majflt-usage1.ru_majflt);
  printf("  # I/O in     = %11lu\n", usage2.ru_inblock-usage1.ru_inblock);
  printf("  # I/O out    = %11lu\n", usage2.ru_oublock-usage1.ru_oublock);

  (void)n_fibers;
  (void)n_threads;
}


int
main(int argc, char * argv[])
{
  int opt;
  size_t n_fibers, n_iters, n_pages, n_threads;
  time_t now;

  now = time(NULL);

  n_fibers  = 0;
  n_iters   = 3;
  n_pages   = 32768;
  n_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "f:i:n:t:"))) {
    switch (opt) {
    case 'f':
      n_fibers = (size_t)atol(optarg);
      break;
    case 'i':
      n_iters = (size_t)atol(optarg);
      break;
    case 'n':
      n_pages = (size_t)atol(optarg);
      break;
    case 't':
      n_threads = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-f n_fibers] [-i n_iters] [-n n_pages]"\
        " [-t n_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  /*assert(n_fibers >= 0);*/
  assert(n_iters > 0);
  assert(n_pages > 0);
  assert(n_threads > 0);

  /* Output build info. */
  printf("============================\n");
  printf(" General ===================\n");
  printf("============================\n");
  printf("  Machine info = %s\n", STR(UNAME));
  printf("  GCC version  = %s\n", STR(GCCV));
  printf("  Build date   = %s\n", STR(DATE));
  printf("  Run date     = %s", ctime(&now));
  printf("  Git commit   = %11s\n", STR(COMMIT));
  printf("  SysPage size = %11lu\n", sysconf(_SC_PAGESIZE));
  printf("\n");

  /* Output problem info. */
  printf("============================\n");
  printf(" Problem ===================\n");
  printf("============================\n");
  printf("  # iters      = %11zu\n", n_iters);
  printf("  # pages      = %11zu\n", n_pages);
  printf("  # fibers     = %11zu\n", n_fibers);
  printf("  # threads    = %11zu\n", n_threads);
  printf("\n");

  S_swap_run(n_iters, n_pages, n_threads, n_fibers);

  return EXIT_SUCCESS;
}
