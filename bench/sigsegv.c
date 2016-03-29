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

/* SIGSEGV */
#include <errno.h>

/* uintptr_t */
#include <inttypes.h>

/* struct sigaction, sigaction */
#include <signal.h>

/* printf */
#include <stdio.h>

/* NULL */
#include <stdlib.h>

/* memset */
#include <string.h>

/* mprotect, PROT_READ, PROT_WRITE */
#include <sys/mman.h>

/* CLOCK_MONOTONIC, struct timespec, clock_gettime */
#include <time.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>


#define KB        (1lu<<10) /* 1KB */
#define MB        (1lu<<20) /* 1MB */
#define GB        (1lu<<30) /* 1GB */
#define MEM_SIZE  (2*GB)


static inline void
S_gettime(struct timespec * const t)
{
  struct timespec tt;
  clock_gettime(CLOCK_MONOTONIC, &tt);
  t->tv_sec = tt.tv_sec;
  t->tv_nsec = tt.tv_nsec;
}


static inline unsigned long
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
S_sigsegv_handler(int unused)
{
  if (unused) { (void)0; }
}


int
main(void)
{
  int ret;
  unsigned long t_nsec;
  size_t i, n_pages;
  struct sigaction act;
  struct timespec ts, te;

  memset(&act, 0, sizeof(act));
  act.sa_handler = &S_sigsegv_handler;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, NULL);
  assert(!ret);

  n_pages = MEM_SIZE/(4*KB);

  /* Testing */
  S_gettime(&ts);
  for (i=0; i<n_pages; ++i) {
    ret = raise(SIGSEGV);
    assert(!ret);
  }
  S_gettime(&te);
  t_nsec = S_getelapsed(&ts, &te);

  /* Output results */
  printf("Segmentation fault latency (ns):  %.2f\n", (double)t_nsec/(double)n_pages);

  return EXIT_SUCCESS;
}
