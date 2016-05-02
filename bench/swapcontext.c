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

/* struct sigaction, sigaction */
#include <signal.h>

/* printf */
#include <stdio.h>

/* EXIT_SUCCESS */
#include <stdlib.h>

/* memset */
#include <string.h>

/* mmap, munmap */
#include <sys/mman.h>

/* time, time_t */
#include <time.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* getopt, sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* */
#include "util.h"


/* Fix for architectures which do not implement MAP_STACK. In these cases, it
 * should add nothing to the bitmask. */
#ifndef MAP_STACK
  #define MAP_STACK 0
#endif


/* The main context, i.e., the context which spawned all of the fibers. */
static __thread ucontext_t S_swapcontext_main;

/* The kernel counter to make sure the kernel is being called. */
static __thread size_t S_swapcontext_ctr=0;


static void
S_swapcontext_kern(void)
{
  S_swapcontext_ctr++;

  /* Switch back to main context, so that a new fiber gets scheduled. */
  setcontext(&S_swapcontext_main);

  /* It is erroneous to reach this point. */
  abort();
}


static void
S_swapcontext_test(size_t const n_iters, size_t const n_threads,
                   size_t const n_fibers)
{
  double t_sec=0.0;

  #pragma omp parallel num_threads(n_threads) shared(t_sec)
  {
    int ret;
    size_t i, fid;
    double ts, te;
    ucontext_t * fiber;

    /* Setup */
    fiber = malloc(n_fibers*sizeof(*fiber));
    assert(fiber);
    for (fid=0; fid<n_fibers; ++fid) {
      ret = getcontext(fiber+fid);
      assert(!ret);

      fiber[fid].uc_stack.ss_sp = mmap(NULL, SIGSTKSZ, PROT_READ|PROT_WRITE,\
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
      assert(MAP_FAILED != fiber->uc_stack.ss_sp);
      fiber[fid].uc_stack.ss_size = SIGSTKSZ;
      fiber[fid].uc_stack.ss_flags = 0;

      makecontext(fiber+fid, (void (*)(void))&S_swapcontext_kern, 0);
    }

    /* Testing */
    S_gettime(&ts);
    for (i=0; i<n_iters; ++i) {
      for (fid=0; fid<n_fibers; ++fid) {
        swapcontext(&S_swapcontext_main, fiber+fid); 
      }
    }
    S_gettime(&te);

    #pragma omp critical
    if (S_getelapsed(&ts, &te) > t_sec) {
      t_sec = S_getelapsed(&ts, &te);
    }

    /* Teardown */
    for (fid=0; fid<n_fibers; ++fid) {
      ret = munmap(fiber[fid].uc_stack.ss_sp, SIGSTKSZ);
      assert(!ret);
    }
    free(fiber);

    assert(S_swapcontext_ctr == n_iters*n_fibers);
  }

  /* Output results. */
  printf("===============================\n");
  printf(" swapcontext statistics =======\n");
  printf("===============================\n");
  printf("  Time (s)        = %11.5f\n", t_sec);
  printf("  latency (ns)    = %11.0f\n", t_sec*1e9/(double)(n_iters*n_fibers));
  printf("\n");
}


int
main(int argc, char * argv[])
{
  int opt;
  size_t p_size, c_size;
  size_t n_iters, n_threads, n_fibers;
  time_t now;

  /* Get current time. */
  now = time(NULL);

  /* Get page size. */
  p_size = (size_t)sysconf(_SC_PAGESIZE);

  /* Get page-cluster value. */
  c_size = (1lu << S_getpagecluster()) * p_size;

  /* Parse command-line parameters. */
  n_iters   = 1024;
  n_threads = 1;
  n_fibers  = 1;
  while (-1 != (opt=getopt(argc, argv, "f:n:t:"))) {
    switch (opt) {
    case 'f':
      n_fibers = (size_t)atol(optarg);
      break;
    case 'n':
      n_iters = (size_t)atol(optarg);
      break;
    case 't':
      n_threads = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-f n_fibers] [-n n_iters] [-t n_threads]\n",
        argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(n_fibers > 0);
  assert(n_iters > 0);
  assert(n_threads > 0);

  /* Output build info. */
  S_printbuildinfo(&now, p_size, c_size);

  /* Output problem info. */
  printf("===============================\n");
  printf(" Parameters ===================\n");
  printf("===============================\n");
  printf("  # iters         = %11zu\n", n_iters);
  printf("  # threads       = %11zu\n", n_threads);
  printf("  # fibers        = %11zu\n", n_fibers);
  printf("\n");

  /* Do test. */  
  S_swapcontext_test(n_iters, n_threads, n_fibers);

  return EXIT_SUCCESS;
}
