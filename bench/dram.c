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

/* printf */
#include <stdio.h>

/* EXIT_SUCCESS */
#include<stdlib.h>

/* mmap, PROT_READ, PROT_WRITE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

/* CLOCK_MONOTONIC, struct timespec, clock_gettime */
#include <time.h>


#define KB        (1lu<<10) /* 1KB */
#define MB        (1lu<<20) /* 1MB */
#define GB        (1lu<<30) /* 1GB */
#define MEM_SIZE  (64*GB)   /* 1GB */
#define NUM_ITERS (8*KB)    /* 8192 */


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
S_touch_mem(char * const mem, size_t const * const map, int const write)
{
  size_t j, k, l, m, n, nn, idx;

  /* Pick a 4KB chunk */
  for (j=0; j<16; ++j) {
    /* Pick a 256KB chunk */
    for (k=0; k<16; ++k) {
      /* Pick a 4MB chunk */
      for (l=0; l<16; ++l) {
        /* Pick a 64MB chunk */
        for (m=0; m<16; ++m) {
          /* Pick a 1GB chunk */
          for (nn=0,n=map[nn]; nn<MEM_SIZE/GB; n=map[++nn]) {
            idx = n*GB+m*64*MB+l*4*MB+k*256*KB+j*4*KB;

            if (write) {
              mem[idx] = (char)idx;
            }
            else {
              assert((char)idx == mem[idx]);
            }
          }
        }
      }
    }
  }
}


int
main(void)
{
  int ret;
  unsigned long r_nsec, w_nsec;
  size_t i, n, nn, tmp, n_pages;
  struct timespec ts, te;
  char * mem;
  size_t map[MEM_SIZE/GB];

  mem = mmap(NULL, MEM_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,\
    -1, 0);
  assert(MAP_FAILED != mem);

  n_pages = MEM_SIZE/(4*KB);

  for (n=0; n<MEM_SIZE/GB; ++n) {
    map[n] = n;
  }
  for (n=0; n<MEM_SIZE/GB; ++n) {
    nn = (size_t)rand()%(MEM_SIZE/GB);
    tmp = map[nn];
    map[nn] = map[n];
    map[n] = tmp;
  }

  /* Touch mem once to make sure it is populated so that it must be swapped. */
  S_touch_mem(mem, map, 1);

  /* Testing */
  S_gettime(&ts);
  for (i=0; i<NUM_ITERS; ++i) {
    S_touch_mem(mem, map, 0);
  }
  S_gettime(&te);
  r_nsec = S_getelapsed(&ts, &te);

  S_gettime(&ts);
  for (i=0; i<NUM_ITERS; ++i) {
    S_touch_mem(mem, map, 1);
  }
  S_gettime(&te);
  w_nsec = S_getelapsed(&ts, &te);

  /* Teardown */
  ret = munmap(mem, MEM_SIZE);
  assert(!ret);

  /* Output results */
  printf("Page read latency (ns):  %.2f\n", (double)r_nsec/(double)(NUM_ITERS*n_pages));
  printf("Page write latency (ns): %.2f\n", (double)w_nsec/(double)(NUM_ITERS*n_pages));

  return EXIT_SUCCESS;
}
