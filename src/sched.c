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
static __thread aioreq_t S_aioreq[OOC_NUM_FIBERS];

/*! Fiber state lists. */
static __thread int S_n_idle;
static __thread int S_n_wait;
static __thread int S_idle_list[OOC_NUM_FIBERS];
static __thread int S_wait_list[OOC_NUM_FIBERS];

/*! My fiber id. */
static __thread int S_me;

/*! The old sigaction to be replaced when we are done. */
static __thread struct sigaction S_old_act;

/*! The main context, i.e., the context which spawned all of the fibers. */
static __thread ucontext_t S_main;

/*! Indicator variable for library initialization. */
static __thread int S_is_init=0;

/*! Asynchronous I/O context. */
static __thread aioctx_t S_aioctx;

/*! Per-process system page table. */
struct sp_tree vma_tree;

/* System page size. */
__thread uintptr_t ps;


static int
S_is_runnable(int const id)
{
  int ret;
  unsigned char incore=0;
  void * page;

  /* Check if there is a request and if it has completed. */
  if (-1 == (ret=aio_error(&(S_aioreq[id])))) {
    assert(EINVAL == errno);

    /* Page align the offending address. */
    page = (void*)((uintptr_t)S_addr[id]&(~(ps-1)));

    /* Check if the page is resident. */
    ret = mincore(page, ps, &incore);
    assert(!ret);

    return incore;
  }
  assert(EINPROGRESS == ret || 0 == ret);

  return (0 == ret);
}


static void
S_sigsegv_handler1(void)
{
  int ret;
  ssize_t retval;
  void * page;

  /* When a thread receives a SIGSEGV, it checks to see if this is the second
   * consecutive SIGSEGV received on that page. If it is not, then we assume
   * (not always correctly) that the page has no protection, issue a madvise
   * for the page and give it read protection. If we assumed incorrectly, the
   * only impact this has is additional overhead, since after the handler
   * returns, the SIGSEGV will be raised again and the page will be promoted to
   * write protection. */

  /* TODO What will happen if multiple fibers segfault on same page? I think it
   * might be possible that a later fiber could update to read/write, then when
   * an earlier fiber resumes execution, it could overwrite memory protections
   * of later fiber.
   * XXX Since we defer dirty page tracking to OS, we can promote protection to
   * read/write on first segfault. Thus, protections can not be degraded by
   * fibers resuming execution. */

  /* Page align the offending address. */
  page = (void*)((uintptr_t)S_addr[S_me]&(~(ps-1)));

  if (!S_is_runnable(S_me)) {
    /* Post an asynchronous fetch of the page. */
    ret = aio_read(page, ps, &(S_aioreq[S_me]));
    assert(!ret);

    /* Put myself on idle list. */
    S_wait_list[S_n_wait++] = S_me;

    dbg_printf("[%5d.%.3d]     Not runnable, switching to S_main\n",\
      (int)syscall(SYS_gettid), S_me);

    /* Switch back to main context to allow another fiber to execute. */
    ret = swapcontext(&(S_handler[S_me]), &S_main);
    assert(!ret);

    /* Retrieve and validate the return value for the request. */
    retval = aio_return(&(S_aioreq[S_me]));
    assert((ssize_t)ps == retval);
  }
  else {
    dbg_printf("[%5d.%.3d]     Runnable\n", (int)syscall(SYS_gettid), S_me);
  }

  /* Apply updates to page containing offending address. */
  ret = mprotect(page, ps, PROT_READ|PROT_WRITE);
  assert(!ret);

  /* Switch back to trampoline context, so that it may return. */
  setcontext(&(S_trampoline[S_me]));

  /* It is erroneous to reach this point. */
  abort();
}


#if 0
static void
S_sigsegv_handler2(void)
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

  /* FIXME There could be a problem here if multiple fibers from the same thread
   * segfault on the same page, since one fiber will have locked the vma, the
   * next fiber will deadlock when it tries to relock the vma. */

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
  addr = (uintptr_t)S_addr[S_me]&(~(ps-1)); /* page align */
  ret = mprotect((void*)addr, ps, prot);
  assert(!ret);

  /* Unlock the vma. */
  ret = lock_let(&(vma->vm_lock));
  assert(!ret);

  /* Switch back to trampoline context, so that it may return. */
  setcontext(&(S_trampoline[S_me]));

  /* It is erroneous to reach this point. */
  abort();
}
#endif


static void
S_sigsegv_trampoline(int const sig, siginfo_t * const si, void * const uc)
{
  /* Signal handler context. */
  static __thread ucontext_t tmp_uc;
  /* Alternate stack for signal handler context. */
  static __thread char tmp_stack[SIGSTKSZ];
  int ret;

  assert(SIGSEGV == sig);

  dbg_printf("[%5d.%.3d]   Received SIGSEGV\n", (int)syscall(SYS_gettid), S_me);
  dbg_printf("[%5d.%.3d]     Address = %p\n", (int)syscall(SYS_gettid), S_me,\
    si->si_addr);

  S_addr[S_me] = si->si_addr;

  memset(&tmp_uc, 0, sizeof(ucontext_t));
  ret = getcontext(&tmp_uc);
  assert(!ret);
  tmp_uc.uc_stack.ss_sp = tmp_stack;
  tmp_uc.uc_stack.ss_size = SIGSTKSZ;
  tmp_uc.uc_stack.ss_flags = 0;
  memcpy(&(tmp_uc.uc_sigmask), &(S_main.uc_sigmask), sizeof(S_main.uc_sigmask));

  /* FIXME POC */
  makecontext(&tmp_uc, (void (*)(void))S_sigsegv_handler1, 0);

  dbg_printf("[%5d.%.3d]     Switching to S_sigsegv_handler1\n",\
    (int)syscall(SYS_gettid), S_me);

  swapcontext(&(S_trampoline[S_me]), &tmp_uc);

  if (uc) {}
}


static void
S_flush1(void)
{
}


#if 0
static void
S_flush2(void)
{
}
#endif


static void
S_kernel_trampoline(int const i)
{
  S_kernel[i](S_iter[i], S_args[i]);

  /* Before this context returns, it should call a `flush function` where the
   * data it accessed is flushed to disk. */
  /* FIXME POC */
  S_flush1();

  /* Put myself on idle list. */
  S_idle_list[S_n_idle++] = i;

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

  ps = (uintptr_t)OOC_PAGE_SIZE;

  dbg_printf("[%5d.***] Library initialized -- pagesize=%lu\n",\
    (int)syscall(SYS_gettid), (long unsigned)ps);

  memset(&S_main, 0, sizeof(ucontext_t));

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &S_sigsegv_trampoline;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, &S_old_act);
  assert(!ret);

  for (i=0; i<OOC_NUM_FIBERS; ++i) {
    memset(&(S_stack[i]), 0, SIGSTKSZ);

    memset(&(S_handler[i]), 0, sizeof(ucontext_t));
    memset(&(S_trampoline[i]), 0, sizeof(ucontext_t));
    memset(&(S_kern[i]), 0, sizeof(ucontext_t));

    memset(&(S_aioreq[i]), 0, sizeof(aioreq_t));

    ret = S_kern_init(i);
    assert(!ret);

    S_idle_list[S_n_idle++] = i;
  }

  /* Setup async-i/o context. */
  ret = aio_setup(OOC_NUM_FIBERS, &S_aioctx);
  assert(!ret);

  S_is_init = 1;

  return ret;
}


int
ooc_finalize(void)
{
  int ret;

  ret = sigaction(SIGSEGV, &S_old_act, NULL);

  /* Destroy async-i/o context. */
  ret = aio_destroy(S_aioctx);
  assert(!ret);

  S_is_init = 0;

  return ret;
}


void
ooc_wait(void)
{
  int ret, j, wait;

  dbg_printf("[%5d.***] Waiting for outstanding fibers\n",\
    (int)syscall(SYS_gettid));

  while (S_n_wait) {
    /* Check wait list for an available fiber. */
    for (j=0,wait=-1; j<S_n_wait; ++j) {
      if (S_is_runnable(S_wait_list[j])) {
        dbg_printf("[%5d.%.3d]   Outstanding\n", (int)syscall(SYS_gettid),\
          S_wait_list[j]);

        /* Remove entry j from wait list and replace entry j with entry
         * n_wait-1. */
        wait = S_wait_list[j];
        S_wait_list[j] = S_wait_list[--S_n_wait];
        break;
      }
    }

    /* FIXME POC */
    /* XXX Force the scheduler to schedule a fiber, since in the POC, until a
     * thread actually access the pages which are not in core, they will not be
     * faulted in. */
    /*if (-1 == wait) {
      wait = S_wait_list[--S_n_wait];
    }*/

    if (-1 != wait) {
      dbg_printf("[%5d.%.3d]   Runnable, switching to S_handler\n",\
        (int)syscall(SYS_gettid), wait);

      /* Switch fibers. */
      S_me = wait;
      ret = swapcontext(&S_main, &(S_handler[S_me]));
      assert(!ret);
    }
    else {
      /* TODO Have aio_suspend() return the runnable fiber 'somehow', so that we
       * do not have to subsequently search the wait list to find it. */
      /* Wait for a fiber to become runnable. Since we are in the `main`
       * context, all fibers must be blocked on async-io, thus no fiber will
       * become idle, so we just wait on async-io. */
      ret = aio_suspend();
      assert(!ret);
    }
  }
}


void
ooc_sched(void (*kern)(size_t const, void * const), size_t const i,
          void * const args)
{
  int ret, j, wait, idle;

  /* Make sure that library has been initialized. */
  if (!S_is_init) {
    ret = S_init();
    assert(!ret);
  }

  dbg_printf("[%5d.***] Scheduling a new fiber\n", (int)syscall(SYS_gettid));

  for (;;) {
    idle = wait = -1;

    /* Check idle list for an available fiber. */
    if (S_n_idle) {
      /* Remove from idle list. */
      idle = S_idle_list[--S_n_idle];
    }
    /* Check wait list for an available fiber. */
    if (-1 == idle) {
      for (j=0; j<S_n_wait; ++j) {
        if (S_is_runnable(S_wait_list[j])) {
          /* Remove entry j from wait list and replace entry j with entry
           * n_wait-1. */
          wait = S_wait_list[j];
          S_wait_list[j] = S_wait_list[--S_n_wait];
          break;
        }
      }

      /* FIXME POC */
      /* XXX Force the scheduler to schedule a fiber, since in the POC, until a
       * thread actually access the pages which are not in core, they will not
       * be faulted in. */
      /*if (-1 == wait) {
        wait = S_wait_list[--S_n_wait];
      }*/
    }

    if (-1 != wait) {
      /* Switch fibers. */
      S_me = wait;
      ret = swapcontext(&S_main, &(S_handler[S_me]));
      assert(!ret);

      /* XXX At this point the fiber S_me has either successfully completed its
       * execution of the kernel S_kern[S_me] or it received another SIGSEGV and
       * repopulated the signal handler context S_handler[S_me], so that it can
       * resume execution from the point it received the latest SIGSEGV at a
       * later time. */
    }
    else if (-1 != idle) {
      /* Setup fiber environment. */
      S_iter[idle] = i;
      S_kernel[idle] = kern;
      S_args[idle] = args;

      dbg_printf("[%5d.%.3d]   Switching to S_kern for iter %zu\n",\
        (int)syscall(SYS_gettid), idle, i);

      /* Switch fibers. */
      S_me = idle;
      ret = swapcontext(&S_main, &(S_kern[S_me]));
      assert(!ret);

      dbg_printf("[%5d.%.3d]   Returned from S_kern for iter %zu\n",\
        (int)syscall(SYS_gettid), idle, i);

      /* XXX At this point the fiber S_me has either successfully completed its
       * execution of the kernel S_kern[S_me] or it received a SIGSEGV and
       * populated the signal handler context S_handler[S_me], so that it can
       * resume execution from the point it received SIGSEGV at a later time. */

      /* XXX This is the only place we can safely break from this loop, since
       * this is the only point that we know that iteration i has been scheduled
       * to a fiber. */
      break;
    }
    else {
      /* TODO Have aio_suspend() return the runnable fiber 'somehow', so that we
       * do not have to subsequently search the wait list to find it. */
      /* Wait for a fiber to become runnable. Since we are in the `main`
       * context, all fibers must be blocked on async-io, thus no fiber will
       * become idle, so we just wait on async-io. */
      ret = aio_suspend();
      assert(!ret);
    }
  }
}


#if 0
void
ooc_sched2(void (*kern)(size_t const, void * const), size_t const i,
           void * const args)
{
  int ret, j, run, idle;

  /* Make sure that library has been initialized. */
  if (!S_is_init) {
    ret = S_init();
    assert(!ret);
  }

  for (;;) {
    /* Search for a fiber that is either blocked or idle. */
    for (j=0,run=-1,idle=-1; j<OOC_NUM_FIBERS; ++j) {
      if (/* FIXME is runnable */0) {
        run = j;
        break;
      }
      else if (/* FIXME is idle */1) {
        idle = j;
        break;
      }
    }

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
#endif


#ifdef TEST
/* assert */
#include <assert.h>

/* uintptr_t, uint8_t */
#include <inttypes.h>

/* NULL, EXIT_SUCCESS */
#include <stdlib.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

void kern(size_t const i, void * const state);
void kern(size_t const i, void * const state)
{
  char var;
  char * mem=(char*)state;

  mem[0] = 'a'; /* Raise a SIGSEGV. */
  assert(&(mem[0]) == (void*)S_addr[S_me]);

  mem[1] = 'b'; /* Should not raise SIGSEGV. */
  assert(&(mem[0]) == (void*)S_addr[S_me]);

  var = mem[1]; /* Should not raise SIGSEGV. */
  assert(var = 'b');

  if (i) {}
}

int
main(void)
{
  int ret;
  size_t ps;
  char * mem;

  ps = (size_t)sysconf(_SC_PAGESIZE);
  assert((size_t)-1 != ps);

  mem = mmap(NULL, ps, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != mem);

  ooc_sched(&kern, 0, mem);

  ooc_wait();
  ret = ooc_finalize();
  assert(!ret);

  ret = munmap(mem, ps);
  assert(!ret);

  return EXIT_SUCCESS;
}
#endif
