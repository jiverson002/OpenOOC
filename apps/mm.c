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

/* EXIT_SUCCESS */
#include <stdlib.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

/* OOC library */
#include "src/ooc.h"


#define restrict


static size_t m, n, p;
static double * a, * b, * c;


static void mm_kern(size_t const i);
static void mm(void);


static void
mm_kern(size_t const i)
{
  size_t j, k;

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


static void
mm(void)
{
  size_t i;

  ooc_init(&mm_kern);
  for (i=0; i<m; ++i) {
    mm_kern(i);
  }
  ooc_finalize();
}


int
main(
  int argc,
  char * argv[]
)
{
  int ret;

  m = 100;
  n = 100;
  p = 100;

#define SZ(X,Y,S) \
  ((X*Y*S+(size_t)sysconf(_SC_PAGESIZE)-1)&(~((size_t)sysconf(_SC_PAGESIZE)-1)))

  a = mmap(NULL, SZ(m,n,sizeof(*a)), PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(a);
  b = mmap(NULL, SZ(n,p,sizeof(*b)), PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(b);
  c = mmap(NULL, SZ(m,p,sizeof(*c)), PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(c);

  /*ret = mprotect(a, SZ(m,n,sizeof(*a)), PROT_READ);
  assert(!ret);
  ret = mprotect(b, SZ(n,p,sizeof(*b)), PROT_READ);
  assert(!ret);
  ret = mprotect(c, SZ(m,p,sizeof(*c)), PROT_READ|PROT_WRITE);
  assert(!ret);*/

#undef SZ

  mm();

  munmap(a, m*n*sizeof(*a));
  munmap(b, n*p*sizeof(*b));
  munmap(c, m*p*sizeof(*c));

  return EXIT_SUCCESS;

  if (argc || argv) {}
}
