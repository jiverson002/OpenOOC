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

/* printf, fprintf, stderr */
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


struct args
{
  size_t n, p, i;
  double const * a, * b;
  double * c;
};


static void
S_gettime(struct timespec * const t)
{
  struct timespec tt;
  clock_gettime(CLOCK_MONOTONIC, &tt);
  t->tv_sec = tt.tv_sec;
  t->tv_nsec = tt.tv_nsec;
}


static unsigned long
S_getelapsed(struct timespec const * const ts, struct timespec const * const te)
{
  struct timespec t;
  if (te->tv_nsec < ts->tv_nsec) {
    t.tv_nsec = 1000000000L + te->tv_nsec - ts->tv_nsec;
    t.tv_sec = te->tv_sec - 1 - ts->tv_sec;
  }
  else {
    t.tv_nsec = te->tv_nsec - ts->tv_nsec;
    t.tv_sec = te->tv_sec - ts->tv_sec;
  }
  return (unsigned long)(t.tv_sec * 1000000000L + t.tv_nsec);
}


static void
S_mm_kern(size_t const i, void * const state)
{
  size_t n, p, j, k;
  double const * a, * b;
  double * c;
  struct args const * args;

  args = (struct args const*)state;
  n = args->n;
  p = args->p;
  a = args->a;
  b = args->b;
  c = args->c;

  for (j=0; j<p; ++j) {
    #define a(R,C) a[R*n+C]
    #define b(R,C) b[R*p+C]
    #define c(R,C) c[R*p+C]
    c(i,j) = a(i,0)*b(0,j);
    for (k=1; k<n; ++k) {
      c(i,j) += a(i,k)*b(k,j);
    }
    #undef a
    #undef b
    #undef c
  }
}


__ooc_decl ( static void S_mm )(size_t const i, void * const state);


__ooc_defn ( static void S_mm )(size_t const i, void * const state)
{
  S_mm_kern(i, state);
}


int
main(int argc, char * argv[])
{
  int ret, opt, use_ooc, num_threads;
  unsigned int seed;
  unsigned long t1_nsec, t2_nsec, t3_nsec;
  size_t m, n, p, i, j, k;
  double tmp;
  struct args args;
  struct timespec ts, te;
  double * a, * b, * c;

  m = 32768;
  n = 32768;
  p = 1;
  use_ooc = 0;
  num_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "om:n:p:t:"))) {
    switch (opt) {
    case 'o':
      use_ooc = 1;
      break;
    case 'm':
      m = (size_t)atol(optarg);
      break;
    case 'n':
      n = (size_t)atol(optarg);
      break;
    case 'p':
      p = (size_t)atol(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-o] [-m m dim] [-n n dim] [-p p dim] "\
        "[-t num_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(num_threads > 0);

  /* Allocate memory. */
  a = mmap(NULL, m*n*sizeof(*a), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != a);
  b = mmap(NULL, n*p*sizeof(*b), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != b);
  c = mmap(NULL, m*p*sizeof(*c), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != c);

  args.n = n;
  args.p = p;
  args.a = a;
  args.b = b;
  args.c = c;

  printf("m=%zu, n=%zu, p=%zu, use_ooc=%d, num_threads=%d\n", m, n, p, use_ooc,\
    num_threads);

  printf("Generating matrices...\n");
  S_gettime(&ts);
  /* Populate matrices. */
  #pragma omp parallel for num_threads(num_threads) if(num_threads > 1)\
    private(seed,j)
  for (i=0; i<m; ++i) {
    for (j=0; j<n; ++j) {
      #define a(R,C) a[R*n+C]
      a[i*n+j] = (double)rand_r(&seed)/RAND_MAX;
      #undef a
    }
  }
  #pragma omp parallel for num_threads(num_threads) if(num_threads > 1)\
    private(seed,j)
  for (i=0; i<n; ++i) {
    for (j=0; j<p; ++j) {
      #define b(R,C) a[R*p+C]
      b[i*p+j] = (double)rand_r(&seed)/RAND_MAX;
      #undef b
    }
  }
  memset(c, 0, m*p*sizeof(*c));
  S_gettime(&te);
  t1_nsec = S_getelapsed(&ts, &te);

  if (use_ooc) {
    /* Prepare OOC environment. */
    ret = mprotect(a, m*n*sizeof(*a), PROT_NONE);
    assert(!ret);
    ret = mprotect(b, n*p*sizeof(*b), PROT_NONE);
    assert(!ret);
    ret = mprotect(c, m*p*sizeof(*c), PROT_NONE);
    assert(!ret);
  }

  printf("Computing matrix multiplication...\n");
  S_gettime(&ts);
  #pragma omp parallel num_threads(num_threads) if(num_threads > 1)
  {
    if (use_ooc) {
      #pragma omp for nowait
      for (i=0; i<m; ++i) {
        S_mm(i, &args);
      }
      ooc_wait(); /* Need this to wait for any outstanding fibers. */
      #pragma omp barrier
      ret = ooc_finalize(); /* Need this to and remove the signal handler. */
      assert(!ret);
    }
    else {
      #pragma omp for
      for (i=0; i<m; ++i) {
        S_mm_kern(i, &args);
      }
    }
  }
  S_gettime(&te);
  t2_nsec = S_getelapsed(&ts, &te);

  printf("Validating results...\n");
  S_gettime(&ts);
  #pragma omp parallel for num_threads(num_threads) if(num_threads > 1)\
    private(j,k,tmp)
  for (i=0; i<m; ++i) {
    for (j=0; j<p; ++j) {
      #define a(R,C) a[R*n+C]
      #define b(R,C) b[R*p+C]
      #define c(R,C) c[R*p+C]
      tmp = a(i,0)*b(0,j);
      for (k=1; k<n; ++k) {
        tmp += a(i,k)*b(k,j);
      }
      assert(tmp == c(i,j));
      #undef a
      #undef b
      #undef c
    }
  }
  S_gettime(&te);
  t3_nsec = S_getelapsed(&ts, &te);

  fprintf(stderr, "Generate time (s) = %.5f\n", (double)t1_nsec/10e9);
  fprintf(stderr, "Compute time (s)  = %.5f\n", (double)t2_nsec/10e9);
  fprintf(stderr, "Validate time (s) = %.5f\n", (double)t3_nsec/10e9);

  munmap(a, m*n*sizeof(*a));
  munmap(b, n*p*sizeof(*b));
  munmap(c, m*p*sizeof(*c));

  return EXIT_SUCCESS;
}
