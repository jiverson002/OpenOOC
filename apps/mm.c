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
  size_t n, m, p;
  size_t q, r;
  size_t x, y, z;
  size_t js, je;
  double const * restrict a;
  double const * restrict b;
  double       * restrict c;
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
S_matfill(size_t const n, size_t const m, double * const a)
{
  size_t i, j;

  for (i=0; i<n; ++i) {
    for (j=0; j<m; ++j) {
      a[i*m+j] = (double)rand()/RAND_MAX;
    }
  }
}


static void
S_matmult_kern(size_t const i, void * const state)
{
  size_t m, p, x, js, je, ks, ke;
  double cv;
  double const * restrict a, * ap, * as, * ae;
  double const * restrict b, * bp, * bs;
  double       * restrict c, * cp, * cs, * ce;
  struct args const * args;

  args = (struct args const*)state;
  m = args->m;
  p = args->p;
  x = args->x;
  js = args->js;
  je = args->je;
  a = args->a;
  b = args->b;
  c = args->c;

  cs = c+i*p+js;
  ce = c+i*p+je;
  if (1 == x) { /* standard */
    as = a+i*m+0;
    ae = a+i*m+m;
    bs = b+js*m+0;
    cp = cs;
    for (; cp<ce; ++cp,bs+=m) {
      ap = as;
      bp = bs;
      cv = *cp;
      for (; ap<ae; ++ap,++bp) {
        cv += *ap * *bp;
      }
      *cp = cv;
    }
  }
  else {        /* tiled */
    for (ks=0; ks<m; ks+=x) {
      ke = ks+x < m ? ks+x : m;
      as = a+i*m+ks;
      ae = a+i*m+ke;
      bs = b+js*m+ks;
      cp = cs;
      for (; cp<ce; ++cp,bs+=m) {
        ap = as;
        bp = bs;
        cv = *cp;
        for (; ap<ae; ++ap,++bp) {
          cv += *ap * *bp;
        }
        *cp = cv;
      }
    }
  }
}


__ooc_decl ( static void S_matmult_ooc )(size_t const bid, void * const state);


__ooc_defn ( static void S_matmult_ooc )(size_t const i, void * const state)
{
  S_matmult_kern(i, state);
}


int
main(int argc, char * argv[])
{
  int ret, opt, num_fibers, num_threads, validate;
  unsigned long t1_nsec, t2_nsec, t3_nsec;
  size_t n, m, p, y, x, z;
  size_t i, j, k;
  size_t is, ie, js, je;
  time_t now;
  struct args args;
  struct timespec ts, te;
  double * a, * b, * c, * v;

  now = time(NULL);

  t1_nsec = 0;
  t2_nsec = 0;
  t3_nsec = 0;

  validate = 0;
  n = 32768;
  m = 32768;
  p = 1;
  x = 1;
  y = 1;
  z = 1;
  num_fibers = 0;
  num_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "vm:n:p:q:r:x:y:z:f:t:"))) {
    switch (opt) {
    case 'f':
      num_fibers = atoi(optarg);
      break;
    case 'n':
      n = (size_t)atol(optarg);
      break;
    case 'm':
      m = (size_t)atol(optarg);
      break;
    case 'p':
      p = (size_t)atol(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      break;
    case 'v':
      validate = 1;
      break;
    case 'x':
      x = (size_t)atol(optarg);
      break;
    case 'y':
      y = (size_t)atol(optarg);
      break;
    case 'z':
      z = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-ov] [-n n dim] [-m m dim] [-p p dim] "\
        "[-x x dim] [-y y dim] [-z z dim] [-f num_fibers] [-t num_threads]\n",\
        argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(num_threads > 0);
  assert(num_fibers >= 0);

  /* Fix-up input. */
  x = (x < n) ? x : n;
  y = (y < p) ? y : p;
  z = (z < m) ? z : m;

  /* Allocate memory. */
  a = mmap(NULL, n*m*sizeof(*a), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != a);
  b = mmap(NULL, m*p*sizeof(*b), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != b);
  c = mmap(NULL, n*p*sizeof(*c), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != c);
  v = mmap(NULL, n*p*sizeof(*v), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != v);

  /* Output build info. */
  printf("==========================\n");
  printf(" General =================\n");
  printf("==========================\n");
  printf("  Machine info = %s\n", STR(UNAME));
  printf("  GCC version  = %s\n", STR(GCCV));
  printf("  Build date   = %s\n", STR(DATE));
  printf("  Run date     = %s", ctime(&now));
  printf("  Git commit   = %9s\n", STR(COMMIT));
  printf("  SysPage size = %9lu\n", sysconf(_SC_PAGESIZE));
  printf("\n");

  /* Output problem info. */
  printf("==========================\n");
  printf(" Problem =================\n");
  printf("==========================\n");
  printf("  n            = %9zu\n", n);
  printf("  m            = %9zu\n", m);
  printf("  p            = %9zu\n", p);
  printf("  y            = %9zu\n", y);
  printf("  x            = %9zu\n", x);
  printf("  z            = %9zu\n", z);
  printf("  # fibers     = %9d\n", num_fibers);
  printf("  # threads    = %9d\n", num_threads);
  printf("\n");

  printf("==========================\n");
  printf(" Status ==================\n");
  printf("==========================\n");
  printf("  Generating matrices...\n");
  S_gettime(&ts);
  S_matfill(n, m, a);
  S_matfill(m, p, b);
  S_gettime(&te);
  t1_nsec = S_getelapsed(&ts, &te);

  if (num_fibers) {
    /* Prepare OOC environment. */
    ooc_set_num_fibers((unsigned int)num_fibers);

    ret = mprotect(a, n*m*sizeof(*a), PROT_NONE);
    assert(!ret);
    ret = mprotect(b, m*p*sizeof(*b), PROT_NONE);
    assert(!ret);
    ret = mprotect(c, n*p*sizeof(*c), PROT_NONE);
    assert(!ret);
  }

  /* Setup args struct. */
  args.n = n;
  args.m = m;
  args.p = p;
  args.x = x;
  args.y = y;
  args.z = z;
  args.a = a;
  args.b = b;
  args.c = c;

  printf("  Computing matrix multiplication...\n");
  S_gettime(&ts);
  if (1 == y && 1 == x && 1 == z) { /* standard */
    args.js = 0;
    args.je = p;
    if (num_fibers) {
      #pragma omp parallel num_threads(num_threads)
      {
        #pragma omp for nowait
        for (i=0; i<n; ++i) {
          S_matmult_ooc(i, &args);
        }
        ooc_wait(); /* Need this to wait for any outstanding fibers. */
        #pragma omp barrier
        ret = ooc_finalize(); /* Need this to and remove the signal handler. */
        assert(!ret);
      }
    }
    else {
      #pragma omp parallel for num_threads(num_threads)
      for (i=0; i<n; ++i) {
        S_matmult_kern(i, &args);
      }
    }
  }
  else {                            /* tiled */
    if (num_fibers) {
      #pragma omp parallel num_threads(num_threads) private(is,ie,js,je)
      {
        for (is=0; is<n; is+=y) {
          ie = is+y < n ? is+y : n;
          for (js=0; js<p; js+=z) {
            je = js+z < p ? js+z : p;
            #pragma omp single
            {
              args.js = js;
              args.je = je;
            }
            #pragma omp for nowait
            for (i=is; i<ie; ++i) {
              S_matmult_ooc(i, &args);
            }
            ooc_wait(); /* Need this to wait for any outstanding fibers. */
            #pragma omp barrier
          }
        }
        ret = ooc_finalize(); /* Need this to and remove the signal handler. */
        assert(!ret);
      }
    }
    else {
      for (is=0; is<n; is+=y) {
        ie = is+y < n ? is+y : n;
        for (js=0; js<p; js+=z) {
          je = js+z < p ? js+z : p;
          args.js = js;
          args.je = je;
          #pragma omp parallel for num_threads(num_threads)
          for (i=is; i<ie; ++i) {
            S_matmult_kern(i, &args);
          }
        }
      }
    }
  }
  S_gettime(&te);
  t2_nsec = S_getelapsed(&ts, &te);

  if (validate) {
    printf("  Validating results...\n");
    S_gettime(&ts);
    #pragma omp parallel for num_threads(num_threads)
    for (i=0; i<n; ++i) {
      for (j=0; j<p; ++j) {
        for (k=0; k<m; ++k) {
          v[i*p+j] += a[i*m+k]*b[j*m+k];
        }
        assert(v[i*p+j] == c[i*p+j]);
      }
    }
    S_gettime(&te);
    t3_nsec = S_getelapsed(&ts, &te);
  }

  printf("\n");
  printf("==========================\n");
  printf(" Timing (s) ==============\n");
  printf("==========================\n");
  printf("  Generate     = %9.5f\n", (double)t1_nsec/1e9);
  printf("  Compute      = %9.5f\n", (double)t2_nsec/1e9);
  if (validate) {
    printf("  Validate     = %9.5f\n", (double)t3_nsec/1e9);
  }

  munmap(a, n*m*sizeof(*a));
  munmap(b, m*p*sizeof(*b));
  munmap(c, n*p*sizeof(*c));
  munmap(v, n*p*sizeof(*v));

  return EXIT_SUCCESS;
}
