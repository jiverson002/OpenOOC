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

/* EXIT_SUCCESS */
#include <stdlib.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

/* OOC library */
#include "src/ooc.h"


struct args
{
  size_t n, p, i;
  double const * a, * b;
  double * c;
};


static void mm_kern(size_t const, void * const);


static void
mm_kern(size_t const i, void * const state)
{
  size_t n, p, j, k;
  double const * a, * b;
  double * c;
  struct args * args;

  args = (struct args*)state;
  n = args->n;
  p = args->p;
  a = args->a;
  b = args->b;
  c = args->c;

#define a(R,C) a[R*n+C]
#define b(R,C) b[R*p+C]
#define c(R,C) c[R*p+C]

  for (j=0; j<n; ++j) {
    c(i,j) = a(i,0)*b(0,j);
    for (k=1; k<p; ++k) {
      c(i,j) += a(i,k)*b(k,j);
    }
  }

#undef a
#undef b
#undef c
}


int
main(void)
{
  size_t m, n, p, i;
  struct args args;
  double * a, * b, * c;

  m = 100;
  n = 100;
  p = 100;

  a = ooc_malloc(m*n*sizeof(*a));
  assert(a);
  b = ooc_malloc(n*p*sizeof(*b));
  assert(b);
  c = ooc_malloc(m*p*sizeof(*c));
  assert(c);

  args.n = n;
  args.p = p;
  args.a = a;
  args.b = b;
  args.c = c;

  #pragma omp parallel num_threads(1)
  {
    OOC_INIT

    #pragma omp for
    for (i=0; i<m; ++i) {
      OOC_CALL(mm_kern)(i, &args);
    }

    OOC_FINAL
  }

  ooc_free(a);
  ooc_free(b);
  ooc_free(c);

  return EXIT_SUCCESS;
}
