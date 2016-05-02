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

/* time, time_t */
#include <time.h>

/* getopt, sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* */
#include "util.h"


static void
S_sigsegv_handler(int unused)
{
  if (unused) { (void)0; }
}


static void
S_sigsegv_test(size_t const p_size, size_t const c_size, size_t const n_pages,
               size_t const n_clust, size_t const n_threads)
{
  int ret;
  size_t i;
  double ts, te, t_sec;
  struct sigaction act;

  /* Setup SIGSEGV handler. */
  memset(&act, 0, sizeof(act));
  act.sa_handler = &S_sigsegv_handler;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, NULL);
  assert(!ret);

  /* Testing segfault latency. */
  S_gettime(&ts);
  #pragma omp parallel for num_threads(n_threads) schedule(static)
  for (i=0; i<n_pages; ++i) {
    ret = raise(SIGSEGV);
    assert(!ret);
  }
  S_gettime(&te);
  t_sec = S_getelapsed(&ts, &te);

  /* Output results. */
  printf("===============================\n");
  printf(" segfault statistics ==========\n");
  printf("===============================\n");
  printf("  Time (s)        = %11.5f\n", t_sec);
  printf("  bw (MiB/s)      = %11.0f\n", (double)(n_pages*p_size)/1048576.0/t_sec);
  printf("  latency (ns)    = %11.0f\n", t_sec*1e9/(double)n_pages);
  printf("\n");

  UNUSED(c_size);
  UNUSED(n_clust);
}


int
main(int argc, char * argv[])
{
  int opt;
  size_t p_size, c_size;
  size_t n_pages, n_clust, n_threads;
  time_t now;

  /* Get current time. */
  now = time(NULL);

  /* Get page size. */
  p_size = (size_t)sysconf(_SC_PAGESIZE);

  /* Get page-cluster value. */
  c_size = (1lu << S_getpagecluster()) * p_size;

  /* Parse command-line parameters. */
  n_pages   = GB / p_size;
  n_threads = 1;
  while (-1 != (opt=getopt(argc, argv, "n:t:"))) {
    switch (opt) {
    case 'n':
      n_pages = (size_t)atol(optarg);
      break;
    case 't':
      n_threads = (size_t)atol(optarg);
      break;
    default: /* '?' */
      fprintf(stderr, "Usage: %s [-n n_pages] [-t n_threads]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }

  /* Validate input. */
  assert(n_pages > 0);
  assert(n_threads > 0);

  /* Compute the testing memory footprint. */
  n_clust = n_pages * p_size / c_size;

  /* Output build info. */
  S_printbuildinfo(&now, p_size, c_size);

  /* Output problem info. */
  printf("===============================\n");
  printf(" Parameters ===================\n");
  printf("===============================\n");
  printf("  # clusters      = %11zu\n", n_clust);
  printf("  # pages         = %11zu\n", n_pages);
  printf("  # threads       = %11zu\n", n_threads);
  printf("\n");

  /* Do test. */  
  S_sigsegv_test(p_size, c_size, n_pages, n_clust, n_threads);

  return EXIT_SUCCESS;
}
