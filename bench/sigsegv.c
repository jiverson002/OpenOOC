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


/* System page size. */
static uintptr_t S_ps;


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
S_sigsegv_handler(int const sig, siginfo_t * const si, void * const uc)
{
  int ret;
  void * page;

  /* Page align the offending address. */
  page = (void*)((uintptr_t)si->si_addr&(~(S_ps-1)));

  /* Apply updates to page containing offending address. */
  ret = mprotect(page, S_ps, PROT_READ|PROT_WRITE);
  assert(!ret);

  if (sig || uc) { (void)0; }
}


static void
S_touch_mem(char * const mem, size_t const * const map, int const write)
{
  size_t j, k, l, m, n, o, p, jj, kk, ll, mm, nn, oo, pp, idx;
  size_t const lmap[8] = { 5, 3, 7, 1, 0, 2, 6, 4 };

  /* Pick a 4KB chunk */
  for (jj=0; jj<8; ++jj) {
    j = lmap[jj];
    /* Pick a 32KB chunk */
    for (kk=0; kk<8; ++kk) {
      k = lmap[kk];
      /* Pick a 256KB chunk */
      for (ll=0; ll<8; ++ll) {
        l = lmap[ll];
        /* Pick a 2MB chunk */
        for (mm=0; mm<8; ++mm) {
          m = lmap[mm];
          /* Pick a 16MB chunk */
          for (nn=0; nn<8; ++nn) {
            n = lmap[nn];
            /* Pick a 128MB chunk */
            for (oo=0; oo<8; ++oo) {
              o = lmap[oo];
              /* Pick a 1GB chunk */
              for (pp=0; pp<MEM_SIZE/GB; ++pp) {
                p = map[pp];

                idx = p*GB+o*128*MB+n*16*MB+m*2*MB+l*256*KB+k*32*KB+j*4*KB;

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
  }
}


int
main(void)
{
  int ret;
  unsigned long r_nsec, w_nsec;
  size_t p, pp, tmp, n_pages;
  struct sigaction act;
  struct timespec ts, te;
  char * mem;
  size_t map[MEM_SIZE/GB];

  S_ps = (uintptr_t)sysconf(_SC_PAGESIZE);

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &S_sigsegv_handler;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, NULL);
  assert(!ret);

  mem = mmap(NULL, MEM_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS,\
    -1, 0);
  assert(MAP_FAILED != mem);

  n_pages = MEM_SIZE/(4*KB);

  for (p=0; p<MEM_SIZE/GB; ++p) {
    map[p] = p;
  }
  for (p=0; p<MEM_SIZE/GB; ++p) {
    pp = (size_t)rand()%(MEM_SIZE/GB);
    tmp = map[pp];
    map[pp] = map[p];
    map[p] = tmp;
  }

  ret = madvise(mem, MEM_SIZE, MADV_RANDOM);
  assert(!ret);

  /* Touch mem once to make sure it is populated so that it must be swapped. */
  S_touch_mem(mem, map, 1);

  ret = mprotect(mem, MEM_SIZE, PROT_NONE);
  assert(!ret);
                                                                                 
  /* Testing */
  S_gettime(&ts);
  S_touch_mem(mem, map, 0);
  S_gettime(&te);
  r_nsec = S_getelapsed(&ts, &te);

  ret = mprotect(mem, MEM_SIZE, PROT_NONE);
  assert(!ret);

  S_gettime(&ts);
  S_touch_mem(mem, map, 1);
  S_gettime(&te);
  w_nsec = S_getelapsed(&ts, &te);

  /* Teardown */
  ret = munmap(mem, MEM_SIZE);
  assert(!ret);

  /* Output results */
  printf("Segmentation fault read latency (ns):  %.2f\n", (double)r_nsec/(double)n_pages);
  printf("Segmentation fault write latency (ns): %.2f\n", (double)w_nsec/(double)n_pages);

  return EXIT_SUCCESS;
}
