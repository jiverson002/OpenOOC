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

/* size_t */
#include <stddef.h>

/* NULL, abort */
#include <stdlib.h>

/* memcpy, memset */
#include <string.h>

/* mmap, mprotect, PROT_READ, PROT_WRITE, MAP_FAILED */
#include <sys/mman.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* OOC_NUM_FIBERS */
#include "include/ooc.h"

/* ooc page table */
#include "splay.h"


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
static __thread void * _args[OOC_NUM_FIBERS];
static __thread void (*_kernel[OOC_NUM_FIBERS])(size_t const, void * const);
static __thread uintptr_t _addr[OOC_NUM_FIBERS];
static __thread ucontext_t _handler[OOC_NUM_FIBERS];
static __thread ucontext_t _trampoline[OOC_NUM_FIBERS];
static __thread ucontext_t _kern[OOC_NUM_FIBERS];
static __thread char _stack[OOC_NUM_FIBERS][SIGSTKSZ];

/* My fiber id. */
static __thread int _me;

/* The old sigaction to be replaced when we are done. */
static __thread struct sigaction _old_act;

/* System page size. */
static __thread uintptr_t _ps;

/* The main context, i.e., the context which spawned all of the fibers. */
/* TODO Need to convince myself that we don't need a main context for each
 * fiber? */
static __thread ucontext_t _main;

/* System page table. */
/* TODO Since this is not thread local, it must be access protected to prevent
 * race conditions between threads. */
static sp_t _sp;


static void
_sigsegv_handler(void)
{
  int ret, prot;
  size_t page;
  uintptr_t addr;
  struct vma * vma;

  /* Find the node corresponding to the offending address. */
  ret = ooc_sp_find(&_sp, _addr[_me], (void*)&vma);
  assert(!ret);

  addr = _addr[_me]&(~(_ps-1)); /* page align */
  page = (addr-vma->nd.b)/_ps;

  if (!(vma->pflags[page]&0x1)) {
    if (vma->pflags[page]&0x10) {
      /* TODO Post an async-io request. */
      /*aio_read(...);*/

      if (/* FIXME async-io has not finished */0) {
        ret = swapcontext(&(_handler[_me]), &_main);
        assert(!ret);
      }
    }

    /* Update page flags. */
    vma->pflags[page] |= 0x1;

    /* Grant read protection to page containing offending address. */
    prot = PROT_READ;
  }
  else {
    /* Update page flags. */
    vma->pflags[page] |= 0x10;

    /* Grant write protection to page containing offending address. */
    prot = PROT_READ|PROT_WRITE;
  }

  /* Apply updates to page containing offending address. */
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


static void
_flush(void)
{
}


static void
_kernel_trampoline(int const i)
{
  _kernel[i](_iter[i], _args[i]);

  /* TODO Before this context returns, it should call a `flush function` where
   * the data it accessed is flushed to disk. The `flush function` should return
   * to the main context. */
  _flush();

  /* Switch back to main context, so that a new fiber gets scheduled. */
  setcontext(&_main);

  /* It is erroneous to reach this point. */
  abort();
}


int
ooc_init(void)
{
  int ret, i;
  struct sigaction act;

  _ps = (uintptr_t)OOC_PAGE_SIZE;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &_sigsegv_trampoline;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, &_old_act);
  assert(!ret);

  for (i=0; i<OOC_NUM_FIBERS; ++i) {
    ret = getcontext(&(_kern[i]));
    assert(!ret);
    _kern[i].uc_stack.ss_sp = _stack[i];
    _kern[i].uc_stack.ss_size = SIGSTKSZ;
    _kern[i].uc_stack.ss_flags = 0;

    makecontext(&(_kern[i]), (void (*)(void))&_kernel_trampoline, 1, i);
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
ooc_sched(void (*kern)(size_t const, void * const), size_t const i,
          void * const args)
{
  int ret, j, run=-1, idle=-1;

  /* Search for a fiber that is either blocked or idle. */
  for (j=0; j<OOC_NUM_FIBERS; ++j) {
    if (/* FIXME is runnable */0) {
      run = j;
      break;
    }
    else if (/* FIXME is idle */1) {
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
      _kernel[idle] = kern;
      _args[idle] = args;

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
      /*ret = aio_suspend(_aiolist, OOC_NUM_FIBERS, NULL);
      assert(!ret);*/
    }
  }

  return ret;
}


#ifdef TEST
/* assert */
#include <assert.h>

/* uintptr_t, uint8_t */
#include <inttypes.h>

/* NULL, EXIT_SUCCESS */
#include <stdlib.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

int
main(void)
{
  int ret;
  char var;
  size_t ps;
  struct vma * vma;

  ps = (size_t)sysconf(_SC_PAGESIZE);
  assert((size_t)-1 != ps);

  ret = ooc_sp_init(&_sp);
  assert(!ret);

  vma = mmap(NULL, 2*ps, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != vma);
  ret = mprotect(vma, ps, PROT_READ|PROT_WRITE);
  assert(!ret);

  /* Setup vma struct. */
  vma->pflags = (uint8_t*)((char*)vma)+sizeof(sp_nd_t);

  ret = ooc_init();
  assert(!ret);

  ret = ooc_sp_insert(&_sp, &(vma->nd), (uintptr_t)((char*)vma+ps), ps);
  assert(!ret);

  var = ((char*)vma->nd.b)[0]; /* Raise a SIGSEGV. */
  assert(&(((char*)vma->nd.b)[0]) == (void*)_addr[_me]);

  var = ((char*)vma->nd.b)[1]; /* Should not raise SIGSEGV. */
  assert(&(((char*)vma->nd.b)[0]) == (void*)_addr[_me]);

  ret = ooc_finalize();
  assert(!ret);

  ret = ooc_sp_remove(&_sp, vma->nd.b);
  assert(!ret);

  ret = munmap(vma, 2*ps);
  assert(!ret);

  ret = ooc_sp_free(&_sp);
  assert(!ret);

  return EXIT_SUCCESS;

  if (var) {}
}
#endif
