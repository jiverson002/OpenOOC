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

/* DBL_EPSILON */
#include <float.h>

/* HOST_NAME_MAX */
#include <limits.h>

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

/* time, ctime */
#include <time.h>

/* gethostname, getopt */
#include <unistd.h>

/* OOC library */
#include "src/ooc.h"


#define XSTR(X) #X
#define STR(X)  XSTR(X)

#define ROWMJR(R,C,NR,NC) (R*NC+C)
#define COLMJR(R,C,NR,NC) (C*NR+R)
/* define access directions for matrices */
#define a(R,C) a[ROWMJR(R,C,n,n)]
#define v(R,C) v[ROWMJR(R,C,n,n)]


struct args
{
  size_t n;
  size_t k;
  size_t js, je;
  double * restrict a;
};


static void
S_matfill(size_t const n, double * const a)
{
  size_t i, j;

  for (i=0; i<n; ++i) {
    for (j=0; j<n; ++j) {
      a(i, j) = (double)rand()/RAND_MAX;
    }
  }
}


static void
S_floyd_kern(size_t const i, void * const state)
{
  size_t n, j, k, js, je;
  double av, bv;
  double * restrict a;
  struct args const * args;

  args = (struct args const*)state;
  n = args->n;
  k = args->k;
  js = args->js;
  je = args->je;
  a = args->a;

  bv = a(i, k);
  for (j=js; j<je; ++j) {
    av = a(k, j);
    if (a(i, j) > bv + av) {
      a(i, j) = bv + av;
    }
  }
}


__ooc_decl ( static void S_floyd_ooc )(size_t const i, void * const state);


__ooc_defn ( static void S_floyd_ooc )(size_t const i, void * const state)
{
  S_floyd_kern(i, state);
}


static void
S_floyd_block_os(struct args * const restrict args, size_t const is, size_t const ie, size_t const ks, size_t const ke)
{
  size_t i, k;
  for (k=ks; k<ke; ++k) { 
    args->k = k;

    #pragma omp for nowait
    for (i=is; i<ie; ++i) {
      S_floyd_kern(i, args);
    }
    #pragma omp barrier
  }
}


static void
S_floyd_block_ooc(struct args * const restrict args, size_t const is, size_t const ie, size_t const ks, size_t const ke)
{
  int ret;
  size_t i, k;
  size_t const n = args->n;
  double * const restrict a = args->a;
  for (k=ks; k<ke; ++k) {
    args->k = k;

    #pragma omp for nowait
    for (i=is; i<ie; ++i) {
      S_floyd_ooc(i, args);
    }
    ooc_wait(); /* Need this to wait for any outstanding fibers. */
    #pragma omp barrier

    #pragma omp single
    {
      /* `flush pages`, i.e., undo memory protections, so that
       * fibers are able to be invoked the next iteration that this
       * memory is touched. */
      ret = mprotect(a+k*n, n*sizeof(*(a)), PROT_NONE);
      assert(!ret);
    }
  }
}


static void
S_floyd_untiled(size_t const n, size_t const x, double * const restrict a, void (*floyd_block)(struct args*, size_t, size_t, size_t, size_t))
{
  int ret;
  size_t k;
  struct args args;

  /* Setup args struct. */
  args.n = n;
  args.a = a;
  args.js = 0;
  args.je = n;

  for (k=0; k<n; ++k) {
    // compute entire matrix as a single block
    floyd_block(&args, 0, n, k, k+1);

    // FIXME: hack
    if (floyd_block == S_floyd_block_ooc) {
      #pragma omp single
      {
        /* `flush pages`, i.e., undo memory protections, so that fibers
         * are able to be invoked the next iteration that this memory is
         * touched. */
        ret = mprotect(a, n*n*sizeof(*a), PROT_NONE);
        assert(!ret);
      }
    }
  }

  /* suppress unused parameter warning */
  (void)x;
}


static void
S_floyd_tiled(size_t const n, size_t const x, double * const restrict a, void (*floyd_block)(struct args*, size_t, size_t, size_t, size_t))
{
  int ret;
  size_t is, ie, js, je, ks, ke;
  struct args args;

  /* Setup args struct. */
  args.n = n;
  args.a = a;

  for (ks=0; ks<n; ks+=x) {
    ke = ks+x < n ? ks+x : n;

    // compute block (ks,ks)
    is = ks;
    ie = ke;
    args.js = ks;
    args.je = ke;
    floyd_block(&args, ks, ke, is, ie);

    // compute blocks (ks, *)
    is = ks;
    ie = ke;
    for (js=0; js<n; js+=x) {
      je = js+x < n ? js+x : n;
      args.js = js;
      args.je = je;

      // skip block (ks,ks), it has already been computed
      if (js == ks) {
        continue;
      }

      floyd_block(&args, ks, ke, is, ie);
    }

    // compute blocks (*, ks)
    args.js = ks;
    args.je = ke;
    for (is=0; is<n; is+=x) {
      ie = is+x < n ? is+x : n;

      // skip block (ks,ks), it has already been computed
      if (is == ks) {
        continue;
      }

      floyd_block(&args, ks, ke, is, ie);
    }

    // compute blocks (*, *)
    for (is=0; is<n; is+=x) {
      ie = is+x < n ? is+x : n;

      // skip block (ks,*), it has already been computed
      if (is == ks) {
        continue;
      }

      for (js=0; js<n; js+=x) {
        je = js+x < n ? js+x : n;
        args.js = js;
        args.je = je;

        // skip block (*,ks), it has already been computed
        if (js == ks) {
          continue;
        }

        floyd_block(&args, ks, ke, is, ie);
      }
    }

    // FIXME: hack
    if (floyd_block == S_floyd_block_ooc) {
      #pragma omp single
      {
        /* `flush pages`, i.e., undo memory protections, so that fibers are able
         * to be invoked the next iteration that this memory is touched. */
        ret = mprotect(a, n*n*sizeof(*a), PROT_NONE);
        assert(!ret);
      }
    }
  }
}


int
main(int argc, char * argv[])
{
  int ret, opt, num_fibers, num_threads, validate;
  double ts, te, t1, t2, t3;
  size_t lock;
  size_t n, x;
  size_t i, j, k;
  time_t now;
  void * l=NULL;
  double * a, * v;
  void (*floyd)(size_t, size_t, double *, void (*)(struct args*, size_t , size_t, size_t, size_t));
  char hostname[HOST_NAME_MAX];

  now = time(NULL);

  lock = 0;
  validate = 0;
  n = 32768;
  x = 1;
  num_fibers = 0;
  num_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "vl:n:x:f:t:"))) {
    switch (opt) {
      case 'l':
      lock = (size_t)atol(optarg);
      break;

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

      case 'x':
      x = (size_t)atol(optarg);
      break;

      default: /* '?' */
        fprintf(stderr, "Usage: %s [-dv] [-n n dim] "\
          "[-x x dim] [-f num_fibers] [-t num_threads]\n",\
          argv[0]);
        return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(num_threads > 0);
  assert(num_fibers >= 0);

  /* Fix-up input. */
  x = (x < n) ? x : n;

  /* Allocate memory. */
  a = mmap(NULL, n*n*sizeof(*a), PROT_READ|PROT_WRITE,\
    MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != a);
  if (validate) {
    v = mmap(NULL, n*n*sizeof(*v), PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    assert(MAP_FAILED != v);
  }

  /* Try to disable readahead. */
  ret = madvise(a, n*n*sizeof(*a), MADV_RANDOM);
  assert(!ret);

  ret = gethostname(hostname, sizeof(hostname));
  assert(!ret);

  /* Output build info. */
  printf("===========================\n");
  printf(" General ==================\n");
  printf("===========================\n");
  printf("  GCC version  = %s\n", STR(GCCV));
  printf("  Build date   = %s\n", STR(DATE));
  printf("  Machine info = %10s\n", hostname);
  printf("  Run date     = %s", ctime(&now));
  printf("  Git commit   = %10s\n", STR(COMMIT));
  printf("  SysPage size = %10lu\n", sysconf(_SC_PAGESIZE));
  printf("\n");

  /* Output problem info. */
  printf("===========================\n");
  printf(" Problem ==================\n");
  printf("===========================\n");
  printf("  lock memory  = %10zu\n", lock);
  printf("  n            = %10zu\n", n);
  printf("  x            = %10zu\n", x);
  printf("  # fibers     = %10d\n", num_fibers);
  printf("  # threads    = %10d\n", num_threads);
  printf("\n");

  printf("===========================\n");
  printf(" Status ===================\n");
  printf("===========================\n");
  printf("  Generating matrices...\n");
  printf("  A @ %p (%zu) -- %p (%zu)\n", (void*)a, (size_t)a, (void*)(a+n*n), (size_t)(a+n*n));
  
  ts = omp_get_wtime();
  S_matfill(n, a);
  te = omp_get_wtime();
  t1 = te-ts;

  /* Copy original matrix for validation */
  if (validate) {
    memcpy(v, a, n*n*sizeof(*v));
  }

  if (num_fibers) {
    ret = mprotect(a, n*n*sizeof(*a), PROT_NONE);
    assert(!ret);
  }

  /* Lock desired amount of memory. */
  if (lock) {
    l = mmap(NULL, lock, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0);
    assert(MAP_FAILED != l);
  }

  printf("  Computing Floyd all-pairs shortest path...\n");
  ts = omp_get_wtime();
  if (1 == x) {     /* standard */
    floyd = S_floyd_untiled;
  }
  else {            /* tiled */
    floyd = S_floyd_tiled;
  }
  if (num_fibers) { /* ooc */
    #pragma omp parallel num_threads(num_threads) private(ret)
    {
      /* Prepare OOC environment. */
      ooc_set_num_fibers((unsigned int)num_fibers);

      floyd(n, x, a, S_floyd_block_ooc);

      /* Finalize OOC environment. */
      ret = ooc_finalize(); /* Need this to and remove the signal handler. */
      assert(!ret);
    }
  }
  else {            /* os */
    #pragma omp parallel num_threads(num_threads)
    {
      floyd(n, x, a, S_floyd_block_os);
    }
  }
  te = omp_get_wtime();
  t2 = te-ts;

  /* Grant read/write protections back to the memory. */
  if (num_fibers) {
    ret = mprotect(a, n*n*sizeof(*a), PROT_READ|PROT_WRITE);
    assert(!ret);
  }

  /* Unmap locked memory before trying to validate -- speeds up validating. */
  if (lock) {
    munmap(l, lock);
  }

  if (validate) {
    printf("  Validating results...\n");
    ts = omp_get_wtime();
    for (k=0; k<n; ++k) {
      #pragma omp parallel for num_threads(num_threads)
      for (i=0; i<n; ++i) {
        for (j=0; j<n; ++j) {
          if (v(i, j) > v(i, k) + v(k, j)) {
            v(i, j) = v(i, k) + v(k, j);
          }
        }
      }
    }
    for (i=0; i<n; ++i) {
      for (j=0; j<n; ++j) {
        if (v(i,j) - a(i,j) > DBL_EPSILON) {
          printf("%f %f\n", v(i,j), a(i,j));
        }
        assert(v(i,j) - a(i,j) <= DBL_EPSILON);
      }
    }
    te = omp_get_wtime();
    t3 = te-ts;
  }

  printf("\n");
  printf("===========================\n");
  printf(" Timing (s) ===============\n");
  printf("===========================\n");
  printf("  Generate     = %10.5f\n", (double)t1);
  printf("  Compute      = %10.5f\n", (double)t2);
  if (validate) {
    printf("  Validate     = %10.5f\n", (double)t3);
  }

  /* Unmap remaining memory */
  munmap(a, n*n*sizeof(*a));
  if (validate) {
    munmap(v, n*n*sizeof(*v));
  }

  return EXIT_SUCCESS;
}
