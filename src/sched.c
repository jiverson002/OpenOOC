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

/* abort */
#include <stdlib.h>

/* memcpy, memset */
#include <string.h>

/* mprotect, PROT_READ, PROT_WRITE */
#include <sys/mman.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>

/* OOC_NUM_FIBERS */
#include "src/ooc.h"


/******************************************************************************/
/*
 *  Page flag bits flow chart for _sigsegv_handler
 *    - Each page has two bits designated [xy] as flags
 *
 *         _sigsegv_handler                             _async_flush
 *         ================                             ============
 *
 *            resident
 *            --------
 *  +-----+      yes     +---------+            +-----+
 *  | y=1 |------------->| set x=1 |            | y=1 |
 *  +-----+              +---------+            +-----+
 *     |                                           |
 *     | swapped                                   | resident
 *     | -------                                   | --------
 *     |   no                                      |    yes
 *     |                                           V
 *     |      on disk                           +-----+          +-------------+
 *     V      -------                           | x=1 |--------->| async write |
 *  +-----+     yes      +------------+         +-----+  dirty   +-------------+
 *  | x=1 |------------->| async read |            |     -----          |
 *  +-----+              +------------+      clean |      yes           |
 *     |                     |               ----- |                    |
 *     | zero fill           |                no   |                    |
 *     | ---------           |                     V                    |
 *     |    no               |                  +---------+            /
 *     V                     |                  | set y=0 |<----------
 *  +---------+             /                   +---------+
 *  | set y=1 |<-----------
 *  +---------+
 */
/******************************************************************************/


/* Together these arrays make up an out-of-core execution context, henceforth
 * known simply as a fiber. Multiple arrays are used instead of a struct with
 * the various fields to simplify things like passing all fibers' async-io
 * requests to library functions, i.e., aio_suspend(). */
static __thread size_t _iter[OOC_NUM_FIBERS];
static __thread uintptr_t _addr[OOC_NUM_FIBERS];
static __thread ucontext_t _handler[OOC_NUM_FIBERS];
static __thread ucontext_t _trampoline[OOC_NUM_FIBERS];
static __thread ucontext_t _kern[OOC_NUM_FIBERS];
static __thread char _stack[OOC_NUM_FIBERS][SIGSTKSZ];

/* My fiber id. */
/* FIXME _me should not be initialized. */
static __thread int _me=0;

/* The main context, i.e., the context which spawned all of the fibers. */
static __thread ucontext_t _main;

/* The old sigaction to be replaced when we are done. */
static __thread struct sigaction _old_act;

/* System page size. */
static __thread uintptr_t _ps;


static void
_sigsegv_handler(void)
{
  int ret, prot;
  uintptr_t addr;

  if (/* FIXME flags = none */0) {
    /* TODO Post an async-io request. */
    //aio_read(...);

    if (/* FIXME async-io has not finished */0) {
      ret = swapcontext(&(_handler[_me]), &_main);
      assert(!ret);
    }

    /* Grant read protection to page containing offending address. */
    prot = PROT_READ;
  }
  else if (/* FIXME flags = read */0) {
    /* Grant write protection to page containing offending address. */
    prot = PROT_READ|PROT_WRITE;
  }
  else {
    /* error */
    abort();
  }

  /* Apply updates to page containing offending address. */
  addr = _addr[_me]&(~(_ps-1)); /* page align */
  ret = mprotect((void*)addr, _ps, prot);
  assert(!ret);

  /* Switch back to trampoline context, so that it may return. */
  setcontext(&(_trampoline[_me]));

  /* It is erroneous to reach this point. */
  abort();
}


static void
_sigsegv_trampoline(int const sig, siginfo_t * const si, void * const uc)
{
  /* Signal handler context. */
  static __thread ucontext_t tmp_uc;
  /* Alternate stack for signal handler context. */
  static __thread char tmp_stack[SIGSTKSZ];
  int ret;

  assert(SIGSEGV == sig);

  _addr[_me] = (uintptr_t)si->si_addr;

  ret = getcontext(&tmp_uc);
  assert(!ret);
  tmp_uc.uc_stack.ss_sp = tmp_stack;
  tmp_uc.uc_stack.ss_size = SIGSTKSZ;
  tmp_uc.uc_stack.ss_flags = 0;
  memcpy(&(tmp_uc.uc_sigmask), &(_main.uc_sigmask), sizeof(_main.uc_sigmask));

  makecontext(&tmp_uc, (void (*)(void))_sigsegv_handler, 0);

  swapcontext(&(_trampoline[_me]), &tmp_uc);

  if (uc) {}
}


int
ooc_init(void (*kern)(size_t const))
{
  int ret, i;
  struct sigaction act;

  _ps = (uintptr_t)sysconf(_SC_PAGESIZE);

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &_sigsegv_trampoline;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, &_old_act);

  for (i=0; i<OOC_NUM_FIBERS; ++i) {
    ret = getcontext(&(_kern[i]));
    assert(!ret);
    _kern[i].uc_stack.ss_sp = _stack[i];
    _kern[i].uc_stack.ss_size = SIGSTKSZ;
    _kern[i].uc_stack.ss_flags = 0;

    makecontext(&(_kern[i]), (void (*)(void))kern, 1, i);
  }

  return ret;
}


int
ooc_finalize(void)
{
  int ret;

  ret = sigaction(SIGSEGV, &_old_act, NULL);

  return ret;
}


int
ooc_sched(size_t const i)
{
  int ret, j, run=-1, idle=-1;

  /* Search for a fiber that is either blocked or idle. */
  for (j=0; j<OOC_NUM_FIBERS; ++j) {
    if (/* FIXME is runnable */0) {
      run = j;
      break;
    }
    else if (/* FIXME is idle */0) {
      idle = j;
      break;
    }
  }

  for (;;) {
    if (-1 != run) {
      _me = run;
      ret = swapcontext(&_main, &(_handler[_me]));
      assert(!ret);
    }
    else if (-1 != idle) {
      _iter[idle] = i;

      _me = idle;
      ret = swapcontext(&_main, &(_kern[_me]));
      assert(!ret);

      /* This is the only place we can safely break from this loop, since this
       * is the only point that we know that iteration i has been assigned to a
       * fiber. */
      break;
    }
    else {
      /* TODO Wait for a fiber to become runnable. Since we are in the `main`
       * context, no fibers can make progress towards completing their kernel,
       * thus no fiber will become idle, so we just wait on async-io. */
      //aio_suspend(...);
    }
  }

  return ret;
}


#ifdef TEST
/* assert */
#include <assert.h>

/* NULL, EXIT_SUCCESS */
#include <stdlib.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

#define SEGV_ADDR (void*)(0x1)

static void
test_kern(size_t const i)
{
  return;

  if (i) {}
}

int
main(void)
{
  int ret;
  char * mem;

  ret = ooc_init(&test_kern);
  assert(!ret);

  mem = mmap(NULL, 1<<16, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(mem);

  //mem[0] = (char)1; /* Raise a SIGSEGV. */
  //assert(mem == _addr);

#if 0
  mem[1] = (char)1; /* Should not raise SIGSEGV. */
  assert(mem == _addr);
#endif

  ret = munmap(mem, 1<<16);
  assert(!ret);

  ret = ooc_finalize();
  assert(!ret);

  return EXIT_SUCCESS;
}
#endif
