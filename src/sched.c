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

/* mprotect, PROT_READ, PROT_WRITE */
#include <sys/mman.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* OOC_NUM_FIBERS, function prototypes */
#include "include/ooc.h"

/* */
#include "common.h"


/******************************************************************************/
/*
 *  Page flag bits flow chart for S_sigsegv_handler
 *    - Each page has two bits designated [xy] as flags
 *
 *         S_sigsegv_handler                           S_async_flush
 *         =================                           =============
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
static __thread size_t S_iter[OOC_NUM_FIBERS];
static __thread void * S_args[OOC_NUM_FIBERS];
static __thread void (*S_kernel[OOC_NUM_FIBERS])(size_t const, void * const);
static __thread void * S_addr[OOC_NUM_FIBERS];
static __thread ucontext_t S_handler[OOC_NUM_FIBERS];
static __thread ucontext_t S_trampoline[OOC_NUM_FIBERS];
static __thread ucontext_t S_kern[OOC_NUM_FIBERS];
static __thread char S_stack[OOC_NUM_FIBERS][SIGSTKSZ];

/* My fiber id. */
static __thread int S_me;

/* The old sigaction to be replaced when we are done. */
static __thread struct sigaction S_old_act;

/* System page size. */
static __thread uintptr_t S_ps;

/* The main context, i.e., the context which spawned all of the fibers. */
/* TODO Need to convince myself that we don't need a main context for each
 * fiber? */
static __thread ucontext_t S_main;

/* Indicator variable for library initialization. */
static __thread int S_is_init=0;

/* System page table. */
struct sp_tree vma_tree;


static void
S_sigsegv_handler(void)
{
  int ret, prot;
  uintptr_t addr;
  struct vm_area * vma;

  /* TODO Because we may be splitting/merging vma after locking it, we may need
   * to hold a lock with a greater scope, like maybe vma_tree lock until
   * splitting/merging is done. The reason for this is that if I get the lock on
   * vma, then another thread segfaults on a different/same address in vma, then
   * they will find_and_lock on vma and will block when they try to get the
   * lock. If I split vma, then the vma they have found, may no longer include
   * the address which caused their segfault. If I merge vma, then the vma they
   * found may no longer exist. */

  /* TODO Maybe the solution to the above problem is to call a new function
   * called sp_tree_find_mod_and_lock(). This way, the find, split/merge, and
   * lock is all done atomically (while holding the vma_tree lock inside the
   * function called). */

  /* FIXME The unit test is passing right now because after the first page in an
   * vma gets faulted, since no splitting/merging happens, the vma is marked
   * with read flag. After that, any other page in the vma that faults, will see
   * that its vma has read flag and be automatically granted read/write flags.
   * This allows the code to work, but also means that after the first page
   * fault in a vma, every other page fault, even read accesses will cause the
   * vma to be marked dirty. */

  /* Find the vma corresponding to the offending address and lock it. */
  ret = sp_tree_find_and_lock(&vma_tree, S_addr[S_me], (void*)&vma);
  assert(!ret);

  if (!(vma->vm_flags&0x1)) {
    if (vma->vm_flags&0x10) {
      /* TODO Post an async-io request. */
      /*aio_read(...);*/

      if (/* FIXME async-io has not finished */0) {
        ret = swapcontext(&(S_handler[S_me]), &S_main);
        assert(!ret);
      }
    }

    /* Update page flags. */
    vma->vm_flags |= 0x1;

    /* Grant read protection to page containing offending address. */
    prot = PROT_READ;
  }
  else {
    /* Update page flags. */
    vma->vm_flags |= 0x10;

    /* Grant write protection to page containing offending address. */
    prot = PROT_READ|PROT_WRITE;
  }

  /* Apply updates to page containing offending address. */
  addr = (uintptr_t)S_addr[S_me]&(~(S_ps-1)); /* page align */
  ret = mprotect((void*)addr, S_ps, prot);
  assert(!ret);

  /* Unlock the vma. */
  ret = lock_let(&(vma->vm_lock));
  assert(!ret);

  /* Switch back to trampoline context, so that it may return. */
  setcontext(&(S_trampoline[S_me]));

  /* It is erroneous to reach this point. */
  abort();
}


static void
S_sigsegv_trampoline(int const sig, siginfo_t * const si, void * const uc)
{
  /* Signal handler context. */
  static __thread ucontext_t tmp_uc;
  /* Alternate stack for signal handler context. */
  static __thread char tmp_stack[SIGSTKSZ];
  int ret;

  assert(SIGSEGV == sig);

  S_addr[S_me] = si->si_addr;

  ret = getcontext(&tmp_uc);
  assert(!ret);
  tmp_uc.uc_stack.ss_sp = tmp_stack;
  tmp_uc.uc_stack.ss_size = SIGSTKSZ;
  tmp_uc.uc_stack.ss_flags = 0;
  memcpy(&(tmp_uc.uc_sigmask), &(S_main.uc_sigmask), sizeof(S_main.uc_sigmask));

  makecontext(&tmp_uc, (void (*)(void))S_sigsegv_handler, 0);

  swapcontext(&(S_trampoline[S_me]), &tmp_uc);

  if (uc) {}
}


static void
S_flush(void)
{
}


static void
S_kernel_trampoline(int const i)
{
  S_kernel[i](S_iter[i], S_args[i]);

  /* TODO Before this context returns, it should call a `flush function` where
   * the data it accessed is flushed to disk. */
  S_flush();

  /* Switch back to main context, so that a new fiber gets scheduled. */
  setcontext(&S_main);

  /* It is erroneous to reach this point. */
  abort();
}


/* Moved this into its own function to prevent the following gcc error:
   variable ‘i’ might be clobbered by ‘longjmp’ or ‘vfork’ [-Werror=clobbered]
 */
static int
S_kern_init(int const i)
{
  int ret;

  ret = getcontext(&(S_kern[i]));
  assert(!ret);
  S_kern[i].uc_stack.ss_sp = S_stack[i];
  S_kern[i].uc_stack.ss_size = SIGSTKSZ;
  S_kern[i].uc_stack.ss_flags = 0;

  makecontext(&(S_kern[i]), (void (*)(void))&S_kernel_trampoline, 1, i);

  return 0;
}


static int
S_init(void)
{
  int ret, i;
  struct sigaction act;

  S_ps = (uintptr_t)OOC_PAGE_SIZE;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &S_sigsegv_trampoline;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, &S_old_act);
  assert(!ret);

  for (i=0; i<OOC_NUM_FIBERS; ++i) {
    ret = S_kern_init(i);
    assert(!ret);
  }

  S_is_init = 1;

  return ret;
}


int
ooc_finalize(void)
{
  int ret;

  ret = sigaction(SIGSEGV, &S_old_act, NULL);

  S_is_init = 0;

  return ret;
}


void
ooc_sched(void (*kern)(size_t const, void * const), size_t const i,
          void * const args)
{
  int ret, j, run=-1, idle=-1;

  /* Make sure that library has been initialized. */
  if (!S_is_init) {
    ret = S_init();
    assert(!ret);
  }

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
      S_me = run;
      ret = swapcontext(&S_main, &(S_handler[S_me]));
      assert(!ret);
    }
    else if (-1 != idle) {
      S_iter[idle] = i;
      S_kernel[idle] = kern;
      S_args[idle] = args;

      S_me = idle;
      ret = swapcontext(&S_main, &(S_kern[S_me]));
      assert(!ret);

      /* This is the only place we can safely break from this loop, since this
       * is the only point that we know that iteration i has been assigned to a
       * fiber. */
      break;
    }
    else {
      /* TODO Wait for a fiber to become runnable. Since we are in the `main`
       * context, all fibers must be blocked on async-io, thus no fiber will
       * become idle, so we just wait on async-io. */
      /*ret = aio_suspend(_aiolist, OOC_NUM_FIBERS, NULL);
      assert(!ret);*/
    }
  }
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
  struct vm_area * vma;

  /* This would be set in ooc_sched normally. */
  S_me = 0;

  ps = (size_t)sysconf(_SC_PAGESIZE);
  assert((size_t)-1 != ps);

  ret = sp_tree_init(&vma_tree);
  assert(!ret);

  vma = mmap(NULL, 2*ps, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != vma);
  ret = mprotect(vma, ps, PROT_READ|PROT_WRITE);
  assert(!ret);

  vma->vm_start = (void*)((char*)vma+ps);
  vma->vm_end   = (void*)((char*)vma->vm_start+ps);

  ret = S_init();
  assert(!ret);

  ret = sp_tree_insert(&vma_tree, vma);
  assert(!ret);

  ((char*)vma->vm_start)[0] = 'a'; /* Raise a SIGSEGV. */
  assert(&(((char*)vma->vm_start)[0]) == (void*)S_addr[S_me]);

  ((char*)vma->vm_start)[1] = 'b'; /* Should not raise SIGSEGV. */
  assert(&(((char*)vma->vm_start)[0]) == (void*)S_addr[S_me]);
  var = ((char*)vma->vm_start)[1]; /* Should not raise SIGSEGV. */
  assert(var = 'b');

  ret = ooc_finalize();
  assert(!ret);

  ret = sp_tree_remove(&vma_tree, vma->vm_start);
  assert(!ret);

  ret = munmap(vma, 2*ps);
  assert(!ret);

  ret = sp_tree_free(&vma_tree);
  assert(!ret);

  return EXIT_SUCCESS;
}
#endif
