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

/* jmp_buf, setjmp, longjmp */
#include <setjmp.h>

/* struct sigaction, sigaction */
#include <signal.h>

/* printf */
#include <stdio.h>

/* EXIT_SUCCESS, posix_memalign, malloc, free */
#include <stdlib.h>

/* memset */
#include <string.h>

/* madvise, MADV_RANDOM */
#include <sys/mman.h>

/* CLOCK_MONOTONIC, struct timespec, clock_gettime */
#include <time.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* Fix for architectures which do not implement MAP_STACK. In these cases, it
 * should add nothing to the bitmask. */
#ifndef MAP_STACK
  #define MAP_STACK 0
#endif


#define XSTR(X) #X
#define STR(X)  XSTR(X)

#define KB              (1lu<<10) /* 1KB */
#define MB              (1lu<<20) /* 1MB */
#define GB              (1lu<<30) /* 1GB */
#define MEM_SIZE        (16*GB)
#define NUM_FIBERS      (1<<10)   /* 1024 */
#define NUM_SWAP_ITERS  3
#define NUM_FIBER_ITERS (1<<13)   /* 8192 */


/* The main context, i.e., the context which spawned all of the fibers. */
static jmp_buf    S_longjmp_main;
static jmp_buf    S_siglongjmp_main;
static ucontext_t S_swapcontext_main;

/* The kernel counter to make sure the kernel is being called. */
static long unsigned int S_longjmp_ctr=0;
static long unsigned int S_siglongjmp_ctr=0;
static long unsigned int S_swapcontext_ctr=0;



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
S_swap_helper(char * const mem, size_t const * const map, int const write_flag)
{
  size_t j, k, l, m, n, o, p, jj, kk, ll, mm, nn, oo, pp, idx;
  unsigned long ps;
  size_t const lmap[8] = { 5, 3, 7, 1, 0, 2, 6, 4 };

  ps = (unsigned long)sysconf(_SC_PAGESIZE);

  /* FIXME lmap is size 8, but 32*KB/ps could be greater than 8 if ps < 4096. */
  /* Pick a system page */
  for (jj=0; jj<32*KB/ps; ++jj) {
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

                if (write_flag) {
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


static void
S_swap_run(void)
{
  int ret;
  unsigned long pagesize, n_pages, t_nsec;
  size_t i, p, pp, tmp;
  struct timespec ts, te;
  char * mem;
  size_t * map;

  pagesize = (unsigned long)sysconf(_SC_PAGESIZE);

  map = malloc(MEM_SIZE/GB*sizeof(*map));
  assert(map);

  ret = posix_memalign((void**)&mem, pagesize, MEM_SIZE);
  assert(!ret);

  n_pages = MEM_SIZE/pagesize;

  /* Shuffle the gigabyte map. */ 
  for (p=0; p<MEM_SIZE/GB; ++p) {
    map[p] = p;
  }
  for (p=0; p<MEM_SIZE/GB; ++p) {
    pp = (size_t)rand()%(MEM_SIZE/GB);
    tmp = map[pp];
    map[pp] = map[p];
    map[p] = tmp;
  }

  /* Coerce kernel to disable read ahead or caching. */
  ret = madvise(mem, MEM_SIZE, MADV_RANDOM);
  assert(!ret);

  /* Touch mem once to make sure it is populated so that it must be swapped. */
  S_swap_helper(mem, map, 1);

  /* Testing swap latency. */
  S_gettime(&ts);
  for (i=0; i<NUM_SWAP_ITERS; ++i) {
    S_swap_helper(mem, map, 0);
  }
  S_gettime(&te);
  t_nsec = S_getelapsed(&ts, &te);

#if 0
  S_gettime(&ts);
  for (i=0; i<NUM_SWAP_ITERS; ++i) {
    S_swap_helper(mem, map, 1);
  }
  S_gettime(&te);
  w_nsec = S_getelapsed(&ts, &te);
#endif

  free(map);
  free(mem);

  /* Output results. */
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "Swap latency ==================\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Time (s)         = %10.5f\n", (double)t_nsec/10e9);
  fprintf(stderr, "  # SysPages       = %10lu\n", NUM_SWAP_ITERS*n_pages);
  fprintf(stderr, "  SysPages/s       = %10.0f\n",\
    (double)NUM_SWAP_ITERS*n_pages/t_nsec*10e9);
  fprintf(stderr, "  Latency          = %10.0f\n",\
    (double)t_nsec/NUM_SWAP_ITERS/n_pages);
  fprintf(stderr, "\n");
}


static void
S_mprotect_helper(char * const mem)
{
  int ret;
  size_t j, k, l, m, n, o, jj, kk, ll, mm, nn, oo, idx;
  unsigned long ps;
  size_t const lmap[8] = { 2, 5, 4, 1, 3, 0, 7, 6 };

  ps = (unsigned long)sysconf(_SC_PAGESIZE);

  /* FIXME lmap is size 8, but 32*KB/ps could be greater than 8 if ps < 4096. */
  /* Pick a system page */
  for (jj=0; jj<32*KB/ps; ++jj) {
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
            for (oo=0; oo<6; ++oo) { /* mem is 768MB */
              o = lmap[oo]; /* 0-5 occupy first 6 entries in map */

              idx = o*128*MB+n*16*MB+m*2*MB+l*256*KB+k*32*KB+j*4*KB;

              ret = mprotect(mem+idx, ps, PROT_READ|PROT_WRITE);
              assert(!ret);
            }
          }
        }
      }
    }
  }
}


static void
S_mprotect_run(void)
{
  int ret;
  unsigned long pagesize, n_pages, t_nsec=0;
  size_t i;
  struct timespec ts, te;
  char * mem;

  pagesize = (unsigned long)sysconf(_SC_PAGESIZE);

  ret = posix_memalign((void**)&mem, pagesize, 768*MB);
  assert(!ret);

  n_pages = 768*MB/pagesize;

  /* Coerce kernel to disable read ahead or caching. */
  ret = madvise(mem, 768*MB, MADV_RANDOM);
  assert(!ret);

  /* Touch mem once to make sure it is populated so that no page faults are
   * incurred. */
  memset(mem, 1, 768*MB);

  /* TODO This test is not accurate because by the time the last page is
   * mprotect'd, there are at most three vma's, the one being protected, and
   * possibly one before and/or after. To get the true latency, we should try to
   * control the state of the vma's where around the page, not sure if this is
   * possible in any meaningful way though. */
  /* Testing swap latency. */
  for (i=0; i<NUM_SWAP_ITERS; ++i) {
    /* Re-establish default protection (none). */
    ret = mprotect(mem, 768*MB, PROT_NONE);
    assert(!ret);

    S_gettime(&ts);
    S_mprotect_helper(mem);
    S_gettime(&te);
    t_nsec += S_getelapsed(&ts, &te);
  }

  free(mem);

  /* Output results. */
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "mprotect latency ==============\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Time (s)         = %10.5f\n", (double)t_nsec/10e9);
  fprintf(stderr, "  # SysPages       = %10lu\n", NUM_SWAP_ITERS*n_pages);
  fprintf(stderr, "  SysPages/s       = %10.0f\n",\
    (double)NUM_SWAP_ITERS*n_pages/t_nsec*10e9);
  fprintf(stderr, "  Latency          = %10.0f\n",\
    (double)t_nsec/NUM_SWAP_ITERS/n_pages);
  fprintf(stderr, "\n");
}


static void
S_sigsegv_handler(int unused)
{
  if (unused) { (void)0; }
}


static void
S_sigsegv_run(void)
{
  int ret;
  unsigned long pagesize, n_pages, t_nsec;
  size_t i;
  struct sigaction act;
  struct timespec ts, te;

  pagesize = (unsigned long)sysconf(_SC_PAGESIZE);
  n_pages = MEM_SIZE/pagesize;

  /* Setup SIGSEGV handler. */
  memset(&act, 0, sizeof(act));
  act.sa_handler = &S_sigsegv_handler;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, NULL);
  assert(!ret);

  /* Testing segfault latency. */
  S_gettime(&ts);
  for (i=0; i<NUM_SWAP_ITERS*n_pages; ++i) {
    ret = raise(SIGSEGV);
    assert(!ret);
  }
  S_gettime(&te);
  t_nsec = S_getelapsed(&ts, &te);

  /* Output results. */
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "segfault latency ==============\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Time (s)         = %10.5f\n", (double)t_nsec/10e9);
  fprintf(stderr, "  # segfault       = %10lu\n", NUM_SWAP_ITERS*n_pages);
  fprintf(stderr, "  segfault/s       = %10.0f\n",\
    (double)n_pages*NUM_SWAP_ITERS/t_nsec*10e9);
  fprintf(stderr, "  Latency          = %10.0f\n",\
    (double)t_nsec/NUM_SWAP_ITERS/n_pages);
  fprintf(stderr, "\n");
}


static void
S_longjmp_kern(void)
{
  S_longjmp_ctr++;

  /* Switch back to main context, so that a new fiber gets scheduled. */
  longjmp(S_longjmp_main, 1);

  /* It is erroneous to reach this point. */
  abort();
}


static void
S_longjmp_helper1(jmp_buf * const fiber, unsigned int const fid)
{
  if (!setjmp(fiber[fid])) {
    /* This code is executed on the first call to setjmp. */
  }
  else {
    /* This code is executed once longjmp is called. */
    S_longjmp_kern();
  }
}


static void
S_longjmp_helper2(jmp_buf * const fiber, unsigned int const fid)
{
  if (!setjmp(S_longjmp_main)) {
    /* This code is executed on the first call to sigsetjmp. */
    longjmp(fiber[fid], 1);
  }
  else {
    /* This code is executed once longjmp is called. */
  }
}


static void
S_longjmp_run(void)
{
  unsigned int i, fid, n_fibers;
  unsigned long t_nsec;
  jmp_buf * fiber;
  struct timespec ts, te;

  n_fibers = NUM_FIBERS;

  /* Setup */
  fiber = malloc(n_fibers*sizeof(*fiber));
  assert(fiber);
  for (fid=0; fid<n_fibers; ++fid) {
    S_longjmp_helper1(fiber, fid);
  }

  /* Testing */
  S_gettime(&ts);
  for (i=0; i<NUM_FIBER_ITERS; ++i) {
    for (fid=0; fid<n_fibers; ++fid) {
      S_longjmp_helper2(fiber, fid);
    }
  }
  S_gettime(&te);
  t_nsec = S_getelapsed(&ts, &te);

  /* Teardown */
  free(fiber);

  assert(S_longjmp_ctr == NUM_FIBER_ITERS*n_fibers);

  /* Output results. */
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "longjmp latency ===============\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Time (s)         = %10.5f\n", (double)t_nsec/10e9);
  fprintf(stderr, "  # longjmp        = %10u\n", NUM_FIBER_ITERS*n_fibers);
  fprintf(stderr, "  longjmp/s        = %10.0f\n",\
    (double)NUM_FIBER_ITERS*n_fibers*2/t_nsec*10e9);
  fprintf(stderr, "  Latency          = %10.0f\n",\
    (double)t_nsec/NUM_FIBER_ITERS/n_fibers/2);
  fprintf(stderr, "\n");
}


static void
S_siglongjmp_kern(void)
{
  S_siglongjmp_ctr++;

  /* Switch back to main context, so that a new fiber gets scheduled. */
  siglongjmp(S_siglongjmp_main, 1);

  /* It is erroneous to reach this point. */
  abort();
}


static void
S_siglongjmp_helper1(jmp_buf * const fiber, unsigned int const fid)
{
  if (!sigsetjmp(fiber[fid], 1)) {
    /* This code is executed on the first call to setjmp. */
  }
  else {
    /* This code is executed once longjmp is called. */
    S_siglongjmp_kern();
  }
}


static void
S_siglongjmp_helper2(jmp_buf * const fiber, unsigned int const fid)
{
  if (!sigsetjmp(S_siglongjmp_main, 1)) {
    /* This code is executed on the first call to sigsetjmp. */
    siglongjmp(fiber[fid], 1);
  }
  else {
    /* This code is executed once longjmp is called. */
  }
}


static void
S_siglongjmp_run(void)
{
  unsigned int i, fid, n_fibers;
  unsigned long t_nsec;
  jmp_buf * fiber;
  struct timespec ts, te;

  n_fibers = NUM_FIBERS;

  /* Setup */
  fiber = malloc(n_fibers*sizeof(*fiber));
  assert(fiber);
  for (fid=0; fid<n_fibers; ++fid) {
    S_siglongjmp_helper1(fiber, fid);
  }

  /* Testing */
  S_gettime(&ts);
  for (i=0; i<NUM_FIBER_ITERS; ++i) {
    for (fid=0; fid<n_fibers; ++fid) {
      S_siglongjmp_helper2(fiber, fid);
    }
  }
  S_gettime(&te);
  t_nsec = S_getelapsed(&ts, &te);

  /* Teardown */
  free(fiber);

  assert(S_siglongjmp_ctr == NUM_FIBER_ITERS*n_fibers);

  /* Output results. */
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "siglongjmp latency ============\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Time (s)         = %10.5f\n", (double)t_nsec/10e9);
  fprintf(stderr, "  # siglongjmp     = %10u\n", NUM_FIBER_ITERS*n_fibers);
  fprintf(stderr, "  siglongjmp/s     = %10.0f\n",\
    (double)NUM_FIBER_ITERS*n_fibers*2/t_nsec*10e9);
  fprintf(stderr, "  Latency          = %10.0f\n",\
    (double)t_nsec/NUM_FIBER_ITERS/n_fibers/2);
  fprintf(stderr, "\n");
}


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
S_swapcontext_run(void)
{
  int ret;
  unsigned int i, fid, n_fibers;
  unsigned long t_nsec;
  struct timespec ts, te;
  ucontext_t * fiber;

  n_fibers = NUM_FIBERS;

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
  for (i=0; i<NUM_FIBER_ITERS; ++i) {
    for (fid=0; fid<n_fibers; ++fid) {
      swapcontext(&S_swapcontext_main, fiber+fid); 
    }
  }
  S_gettime(&te);
  t_nsec = S_getelapsed(&ts, &te);

  /* Teardown */
  for (fid=0; fid<n_fibers; ++fid) {
    ret = munmap(fiber[fid].uc_stack.ss_sp, SIGSTKSZ);
    assert(!ret);
  }
  free(fiber);

  assert(S_swapcontext_ctr == NUM_FIBER_ITERS*n_fibers);

  /* Output results. */
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "swapcontext latency ===========\n");
  fprintf(stderr, "===============================\n");
  fprintf(stderr, "  Time (s)         = %10.5f\n", (double)t_nsec/10e9);
  fprintf(stderr, "  # swapcontext    = %10u\n", NUM_FIBER_ITERS*n_fibers);
  fprintf(stderr, "  swapcontext/s    = %10.0f\n",\
    (double)NUM_FIBER_ITERS*n_fibers*2/t_nsec*10e9);
  fprintf(stderr, "  Latency          = %10.0f\n",\
    (double)t_nsec/NUM_FIBER_ITERS/n_fibers/2);
  fprintf(stderr, "\n");
}


int
main(void)
{
  unsigned long pagesize, n_pages;
  time_t now;

  now = time(NULL);

  pagesize = (unsigned long)sysconf(_SC_PAGESIZE);

  n_pages = MEM_SIZE/pagesize;

  /* Output build info. */
  fprintf(stderr, "==============================\n");
  fprintf(stderr, "General ======================\n");
  fprintf(stderr, "==============================\n");
  fprintf(stderr, "  Build date       = %s\n", STR(DATE));
  fprintf(stderr, "  Run date         = %s", ctime(&now));
  fprintf(stderr, "  Git commit       = %9s\n", STR(COMMIT));
  fprintf(stderr, "  SysPage size     = %9lu\n", pagesize);
  fprintf(stderr, "  Memory (MiB)     = %9lu\n", MEM_SIZE/MB);
  fprintf(stderr, "  Memory (SysPage) = %9lu\n", n_pages);
  fprintf(stderr, "  Options          = ");
  fprintf(stderr, "\n\n");

  S_swap_run();
  S_mprotect_run();
  S_sigsegv_run();
  S_longjmp_run();
  S_siglongjmp_run();
  S_swapcontext_run();

  return EXIT_SUCCESS;
}
