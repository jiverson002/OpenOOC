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

/* fabs */
#include <math.h>

/* omp_get_wtime */
#include <omp.h>

/* printf */
#include <stdio.h>

/* rand_r, RAND_MAX, EXIT_SUCCESS */
#include <stdlib.h>

/* memset */
#include <string.h>

/* mmap, munmap, mprotect, PROT_READ, PROT_WRITE, PROT_NONE, MAP_PRIVATE,
 * MAP_ANONYMOUS */
#include <sys/mman.h>

/* CLOCK_MONOTONIC, struct timespec, clock_gettime */
#include <time.h>

/* getopt */
#include <unistd.h>

/* OOC library */
#include "src/ooc.h"


#define XSTR(X) #X
#define STR(X)  XSTR(X)


struct args
{
  size_t n, y;
  double * restrict a;
};


static void
S_gettime(double * const t)
{
  *t = omp_get_wtime();
}


static double
S_getelapsed(double const * const ts, double const * const te)
{
  return *te-*ts;
}


static void
S_vecfill(size_t const n, double * const a)
{
  size_t i;

  for (i=0; i<n; ++i) {
    a[i] = (double)rand()/RAND_MAX;
  }
}


static int
S_veccomp(void const * const restrict a, void const * const restrict b)
{
  return *(double*)a < *(double*)b ? -1 : 1;
}


static void
S_vecsort_kern(size_t const i, void * const state)
{
  size_t n, y;
  double * restrict a;
  struct args const * args;

  args = (struct args const*)state;
  n = args->n;
  y = args->y;
  a = args->a;

  qsort(a+i, i+y<n?y:n-i, sizeof(*a), S_veccomp);
}


__ooc_decl ( static void S_vecsort_ooc )(size_t const i, void * const state);


__ooc_defn ( static void S_vecsort_ooc )(size_t const i, void * const state)
{
  S_vecsort_kern(i, state);
}


int
main(int argc, char * argv[])
{
  int ret, opt, num_fibers, num_threads, validate;
  double ts, te, t1_sec, t2_sec, t3_sec, t4_sec;
  size_t n, y;
  size_t i, j;
  time_t now;
  struct args args;
  double * a;

  now = time(NULL);

  t1_sec = 0.0;
  t2_sec = 0.0;
  t3_sec = 0.0;
  t4_sec = 0.0;

  validate = 0;
  n = 32768;
  y = 2;
  num_fibers = 0;
  num_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "vn:y:f:t:"))) {
    switch (opt) {
    case 'f':
      num_fibers = atoi(optarg);
      break;
    case 'n':
      n = (size_t)atol(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      break;
    case 'v':
      validate = 1;
      break;
    case 'y':
      y = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-v] [-n n dim] [-y y dim] [-f num_fibers] "\
        "[-t num_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(y >= 2);
  assert(num_threads > 0);
  assert(num_fibers >= 0);

  /* Fix-up input. */
  y = (y < n) ? y : n;

  /* Allocate memory. */
  a = mmap(NULL, n*sizeof(*a), PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,\
    -1, 0);
  assert(MAP_FAILED != a);

  /* Try to disable readahead. */
  ret = madvise(a, n*sizeof(*a), MADV_RANDOM);
  assert(!ret);

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
  printf("  n            = %11zu\n", n);
  printf("  y            = %11zu\n", y);
  printf("  # fibers     = %11d\n", num_fibers);
  printf("  # threads    = %11d\n", num_threads);
  printf("\n");

  printf("============================\n");
  printf(" Status ====================\n");
  printf("============================\n");
  printf("  Generating vector...\n");
  S_gettime(&ts);
  S_vecfill(n, a);
  S_gettime(&te);
  t1_sec = S_getelapsed(&ts, &te);

  if (num_fibers) {
    /* Prepare OOC environment. */
    ooc_set_num_fibers((unsigned int)num_fibers);

    ret = mprotect(a, n*sizeof(*a), PROT_NONE);
    assert(!ret);
  }

  /* Setup args struct. */
  args.n = n;
  args.y = y;
  args.a = a;

  printf("  Sorting vector blocks...\n");
  S_gettime(&ts);
  /* Sort each block. */
  if (num_fibers) {
    #pragma omp parallel num_threads(num_threads)
    {
      #pragma omp for nowait
      for (i=0; i<n; i+=y) {
        S_vecsort_ooc(i, &args);
      }
      ooc_wait(); /* Need this to wait for any outstanding fibers. */
      #pragma omp barrier

      #pragma omp single
      {
        /* `flush pages`, i.e., undo memory protections, so that fibers
         * are able to be invoked the next iteration that this memory is
         * touched. */
        ret = mprotect(a, n*sizeof(*a), PROT_NONE);
        assert(!ret);
      }

      ret = ooc_finalize(); /* Need this to and remove the signal handler. */
      assert(!ret);
    }
  }
  else {
    #pragma omp parallel for num_threads(num_threads)
    for (i=0; i<n; i+=y) {
      S_vecsort_kern(i, &args);
    }
  }
  S_gettime(&te);
  t2_sec = S_getelapsed(&ts, &te);

  printf("  Merging vector blocks...\n");
  S_gettime(&ts);
  /* Merge blocks. */
  if (num_fibers) {
  }
  else {
  }
  S_gettime(&te);
  t3_sec = S_getelapsed(&ts, &te);

  if (validate) {
    printf("  Validating results...\n");
    S_gettime(&ts);
    #pragma omp parallel for num_threads(num_threads)
    for (i=0; i<n; i+=y) {
      for (j=i+1; j<(i+y<n?i+y:n); ++j) {
        assert(a[j] >= a[j-1]);
      }
    }
    /*for (i=1; i<n; ++i) {
      assert(a[i] >= a[i-1]);
    }*/
    S_gettime(&te);
    t4_sec = S_getelapsed(&ts, &te);
  }

  printf("\n");
  printf("============================\n");
  printf(" Timing (s) ================\n");
  printf("============================\n");
  printf("  Generate     = %11.5f\n", t1_sec);
  printf("  Sort blocks  = %11.5f\n", t2_sec);
  printf("  Merge blocks = %11.5f\n", t3_sec);
  if (validate) {
    printf("  Validate     = %11.5f\n", t4_sec);
  }

  ret = munmap(a, n*sizeof(*a));
  assert(!ret);

  return EXIT_SUCCESS;
}
