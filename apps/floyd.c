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
  size_t x;
  size_t k;
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
  size_t n, x, k, js, je;
  double av, bv;
  double * restrict a, * ap, * as, * cp, * cs, * ce;
  struct args const * args;

  args = (struct args const*)state;
  n = args->n;
  x = args->x;
  k = args->k;
  a = args->a;

  bv = a(i, k);   // dist[i][k]

  if (1 == x) { /* standard */
    as = &a(k, 0);  // dist[k][*]
    cs = &a(i, 0);  // dist[i][*]
    ce = cs + n;
    for (ap=as,cp=cs; cp<ce; ++ap,++cp) {
      av = *ap;
      if (*cp > bv + av) {
        *cp = bv + av;
      }
    }
  }
  else {        /* tiled */
    for (js=0; js<n; js+=x) {
      je = js+x < n ? js+x : n;
      as = &a(k, js); // dist[k][*]
      cs = &a(i, js); // dist[i][*]
      ce = &a(i, je);
      for (ap=as,cp=cs; cp<ce; ++ap,++cp) {
        av = *ap;
        if (*cp > bv + av) {
          *cp = bv + av;
        }
      }
    }
  }
}


__ooc_decl ( static void S_floyd_ooc )(size_t const i, void * const state);


__ooc_defn ( static void S_floyd_ooc )(size_t const i, void * const state)
{
  S_floyd_kern(i, state);
}


int
main(int argc, char * argv[])
{
  int ret, opt, num_fibers, num_threads, validate;
  double ts, te, t1, t2, t3;
  size_t lock;
  size_t n, y, x;
  size_t i, j, k;
  size_t is, ie;
  time_t now;
  struct args args;
  void * l=NULL;
  double * a, * v;
  char hostname[HOST_NAME_MAX];

  now = time(NULL);

  lock = 0;
  validate = 0;
  n = 32768;
  x = 1;
  y = 1;
  num_fibers = 0;
  num_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "vl:n:x:y:f:t:"))) {
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

      case 'y':
      y = (size_t)atol(optarg);
      break;

      default: /* '?' */
        fprintf(stderr, "Usage: %s [-dv] [-n n dim] "\
          "[-x x dim] [-y y dim] [-f num_fibers] [-t num_threads]\n",\
          argv[0]);
        return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(num_threads > 0);
  assert(num_fibers >= 0);

  /* Fix-up input. */
  y = (y < n) ? y : n;
  x = (x < n) ? x : n;

  /* Lock desired amount of memory. */
  if (lock) {
    l = mmap(NULL, lock, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED, -1, 0);
    assert(MAP_FAILED != l);
  }

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
  printf("  y            = %10zu\n", y);
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
    memcpy(v, a, n*n*sizeof(*a));
  }

  if (num_fibers) {
    ret = mprotect(a, n*n*sizeof(*a), PROT_NONE);
    assert(!ret);
  }

  /* Setup args struct. */
  args.n = n;
  args.x = x;
  args.a = a;

  printf("  Computing Floyd all-pairs shortest path...\n");
  ts = omp_get_wtime();
  if (1 == y) { /* standard */
    if (num_fibers) {
      #pragma omp parallel num_threads(num_threads) private(is,ie,k,ret)
      {
        /* Prepare OOC environment. */
        ooc_set_num_fibers((unsigned int)num_fibers);

        for (k=0; k<n; ++k) {
          #pragma omp single
          {
            args.k = k;
          }

          #pragma omp for nowait
          for (i=0; i<n; ++i) {
            S_floyd_ooc(i, &args);
          }
          ooc_wait(); /* Need this to wait for any outstanding fibers. */
          #pragma omp barrier

          #pragma omp single
          {
            /* `flush pages`, i.e., undo memory protections, so that fibers
             * are able to be invoked the next iteration that this memory is
             * touched. */
            ret = mprotect(a, n*n*sizeof(*a), PROT_NONE);
            assert(!ret);
          }
        }

        /* Finalize OOC environment. */
        ret = ooc_finalize(); /* Need this to and remove the signal handler. */
        assert(!ret);
      }
    }
    else {
      for (k=0; k<n; ++k) {
        args.k = k;
        #pragma omp parallel for num_threads(num_threads)
        for (i=0; i<n; ++i) {
          S_floyd_kern(i, &args);
        }
      }
    }
  }
  else {        /* tiled */
    if (num_fibers) {
      #pragma omp parallel num_threads(num_threads) private(is,ie,k,ret)
      {
        /* Prepare OOC environment. */
        ooc_set_num_fibers((unsigned int)num_fibers);

        for (k=0; k<n; ++k) {
          #pragma omp single
          {
            args.k = k;
          }

          for (is=0; is<n; is+=y) {
            ie = is+y < n ? is+y : n;

            #pragma omp for nowait
            for (i=is; i<ie; ++i) {
              S_floyd_ooc(i, &args);
            }
            ooc_wait(); /* Need this to wait for any outstanding fibers. */
            #pragma omp barrier
          }

          #pragma omp single
          {
            /* `flush pages`, i.e., undo memory protections, so that fibers
             * are able to be invoked the next iteration that this memory is
             * touched. */
            ret = mprotect(a, n*n*sizeof(*a), PROT_NONE);
            assert(!ret);
          }
        }

        /* Finalize OOC environment. */
        ret = ooc_finalize(); /* Need this to and remove the signal handler. */
        assert(!ret);
      }
    }
    else {
      for (k=0; k<n; ++k) {
        args.k = k;

        for (is=0; is<n; is+=y) {
          ie = is+y < n ? is+y : n;

          #pragma omp parallel for num_threads(num_threads)
          for (i=is; i<ie; ++i) {
            S_floyd_kern(i, &args);
          }
        }
      }
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
        assert(v(i,j) == a(i,j));
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
