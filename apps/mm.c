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


#define CEILDIV(X,Y) (1+(((X)-1)/(Y)))


struct args
{
  size_t n, m, p;
  size_t q, r, s;
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
S_matmult_kern(size_t const bid, void * const state)
{
  size_t n, m, p, q, r, s;
  size_t i, j, k, ii, jj, kk, ie, je, ke, ib, jb, iis, iie, jjs, jje;
  double sum;
  double const * a, * b;
  double * c;
  struct args const * args;

  args = (struct args const*)state;
  n = args->n;
  m = args->m;
  p = args->p;
  q = args->q;
  r = args->r;
  s = args->s;
  a = args->a;
  b = args->b;
  c = args->c;

  ib = bid/r;
  jb = bid%r;

  iis = ib*CEILDIV(n, q);
  iie = (ib < q-1) ? ((ib+1)*CEILDIV(n, q)) : n;
  jjs = jb*CEILDIV(p, r);
  jje = (jb < r-1) ? ((jb+1)*CEILDIV(p, r)) : p;

  if (1 == s) {
    /* Standard. */
    for (i=iis; i<iie; ++i) {
      for (j=jjs; j<jje; ++j) {
        #define a(R,C) a[R*m+C]
        #define b(R,C) b[R*m+C]
        #define c(R,C) c[R*p+C]
        sum = 0.0;
        for (k=0; k<m; ++k) {
          sum += a(i,k)*b(j,k);
        }
        c(i,j) = sum;
        #undef a
        #undef b
        #undef c
      }
    }
  }
  else {
    /* Cache blocked. */
    for (ii=iis; ii<iie; ii+=s) {
      ie = ii+s < n ? ii+s : n;
      for (jj=jjs; jj<jje; jj+=s) {
        je = jj+s < p ? jj+s : p;
        for (i=ii; i<ie; ++i) {
          for (j=jj; j<je; ++j) {
            #define a(R,C) a[R*m+C]
            #define b(R,C) b[R*m+C]
            #define c(R,C) c[R*p+C]
            sum = 0.0;
            for (kk=0; kk<m; kk+=s) {
              ke = kk+s < m ? kk+s : m;
              for (k=kk; k<ke; ++k) {
                sum += a(i,k)*b(j,k);
              }
            }
            c(i,j) = sum;
            #undef a
            #undef b
            #undef c
          }
        }
      }
    }
  }
}


__ooc_decl ( static void S_matmult_ooc )(size_t const bid, void * const state);


__ooc_defn ( static void S_matmult_ooc )(size_t const bid, void * const state)
{
  S_matmult_kern(bid, state);
}


static void
S_matfill(size_t const n, size_t const m, double * const a)
{
  size_t i, j;

  for (i=0; i<n; ++i) {
    for (j=0; j<m; ++j) {
      #define a(R,C) a[R*m+C]
      a(i,j) = (double)rand()/RAND_MAX;
      #undef a
    }
  }
}


int
main(int argc, char * argv[])
{
  int ret, opt, use_ooc, num_threads, validate;
  unsigned long t1_nsec, t2_nsec, t3_nsec;
  size_t n, m, p, q, r, s;
  size_t i, j, k, bid;
  double tmp;
  struct args args;
  struct timespec ts, te;
  double * a, * b, * c;

  use_ooc = 0;
  validate = 0;
  n = 32768;
  m = 32768;
  p = 1;
  q = 1;
  r = 1;
  s = 1;
  num_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "ovm:n:p:q:r:s:t:"))) {
    switch (opt) {
    case 'o':
      use_ooc = 1;
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
    case 'q':
      q = (size_t)atol(optarg);
      break;
    case 'r':
      r = (size_t)atol(optarg);
      break;
    case 's':
      s = (size_t)atol(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      break;
    case 'v':
      validate = 1;
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-ov] [-n n dim] [-m m dim] [-p p dim] "\
        "[-q q dim] [-r r dim] [-s blksz] [-t num_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(num_threads > 0);

  /* Fix-up input. */
  q = (q < n) ? q : n;
  r = (r < p) ? r : p;
  s = (s < n) ? s : n;
  s = (s < m) ? s : m;
  s = (s < p) ? s : p;

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

  printf("n=%zu, m=%zu, p=%zu, q=%zu, r=%zu, s=%zu, use_ooc=%d, "\
    "num_threads=%d, validate=%d\n", n, m, p, q, r, s, use_ooc, num_threads,\
    validate);

  printf("Generating matrices...\n");
  S_gettime(&ts);
  S_matfill(n, m, a);
  S_matfill(m, p, b);
  S_gettime(&te);
  t1_nsec = S_getelapsed(&ts, &te);

  if (use_ooc) {
    /* Prepare OOC environment. */
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
  args.q = q;
  args.r = r;
  args.s = s;
  args.a = a;
  args.b = b;
  args.c = c;

  printf("Computing matrix multiplication...\n");
  S_gettime(&ts);
  #pragma omp parallel num_threads(num_threads) if(num_threads > 1)
  {
    if (use_ooc) {
      #pragma omp for nowait schedule(static)
      for (bid=0; bid<q*r; ++bid) {
        S_matmult_ooc(bid, &args);
      }
      ooc_wait(); /* Need this to wait for any outstanding fibers. */
      #pragma omp barrier
      ret = ooc_finalize(); /* Need this to and remove the signal handler. */
      assert(!ret);
    }
    else {
      #pragma omp for schedule(static)
      for (bid=0; bid<q*r; ++bid) {
        S_matmult_kern(bid, &args);
      }
    }
  }
  S_gettime(&te);
  t2_nsec = S_getelapsed(&ts, &te);

  if (validate) {
    printf("Validating results...\n");
    S_gettime(&ts);
    for (i=0; i<n; ++i) {
      for (j=0; j<p; ++j) {
        #define a(R,C) a[R*m+C]
        #define b(R,C) b[R*m+C]
        #define c(R,C) c[R*p+C]
        tmp = 0.0;
        for (k=0; k<m; ++k) {
          tmp += a(i,k)*b(j,k);
        }
        assert(tmp == c(i,j));
        #undef a
        #undef b
        #undef c
      }
    }
    S_gettime(&te);
    t3_nsec = S_getelapsed(&ts, &te);
  }

  fprintf(stderr, "Generate time (s) = %.5f\n", (double)t1_nsec/1e9);
  fprintf(stderr, "Compute time (s)  = %.5f\n", (double)t2_nsec/1e9);
  if (validate) {
    fprintf(stderr, "Validate time (s) = %.5f\n", (double)t3_nsec/1e9);
  }

  munmap(a, n*m*sizeof(*a));
  munmap(b, m*p*sizeof(*b));
  munmap(c, n*p*sizeof(*c));

  return EXIT_SUCCESS;
}
