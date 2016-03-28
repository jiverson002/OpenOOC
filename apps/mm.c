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

/* uintptr_t */
#include <inttypes.h>

/* printf */
#include <stdio.h>

/* rand, RAND_MAX, EXIT_SUCCESS */
#include <stdlib.h>

/* memset */
#include <string.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* OOC library */
#include "src/ooc.h"


#define SZ_M 1024
#define SZ_N 1024
#define SZ_P 1


struct args
{
  size_t n, p, i;
  double const * a, * b;
  double * c;
};


static void mm_kern(size_t const i, void * const state);


static void mm_kern(size_t const i, void * const state)
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


__ooc_decl ( static void mm )(size_t const i, void * const state);


__ooc_defn ( static void mm )(size_t const i, void * const state)
{
  mm_kern(i, state);
}


int
main(void)
{
  int ret;
  size_t m, n, p, i, j, k;
  double tmp;
  struct args args;
  double * a, * b, * c;

  m = SZ_M;
  n = SZ_N;
  p = SZ_P;

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

  /* Populate matrices. */
  for (i=0; i<m; ++i) {
    for (j=0; j<n; ++j) {
      a[i*n+j] = (double)rand()/RAND_MAX;
    }
  }
  for (i=0; i<n; ++i) {
    for (j=0; j<p; ++j) {
      b[i*p+j] = (double)rand()/RAND_MAX;
    }
  }
  memset(c, 0, m*p*sizeof(*c));

  /* Prepare OOC environment. */
  ret = mprotect(a, m*n*sizeof(*a), PROT_NONE);
  assert(!ret);
  ret = mprotect(b, n*p*sizeof(*b), PROT_NONE);
  assert(!ret);
  ret = mprotect(c, m*p*sizeof(*c), PROT_NONE);
  assert(!ret);

  /*#pragma omp parallel for num_threads(4)*/
  for (i=0; i<m; ++i) {
    mm(i, &args);
  }
  ooc_wait(); /* Need this to wait for any outstanding fibers. */

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

  ret = ooc_finalize(); /* Need this to and remove the signal handler. */
  assert(!ret);

  munmap(a, m*n*sizeof(*a));
  munmap(b, n*p*sizeof(*b));
  munmap(c, m*p*sizeof(*c));

  return EXIT_SUCCESS;
}
