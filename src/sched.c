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

/* syscall, __NR_* */
#include <sys/syscall.h>

/* ucontext_t, getcontext, makecontext, swapcontext, setcontext */
#include <ucontext.h>

/* function prototypes */
#include "include/ooc.h"

/* */
#include "common.h"


#define USE_AIO 1


/******************************************************************************/
/* To profile with perf under linux, compile with -ggdb and before running call
 * sudo sh -c "echo '0' > /proc/sys/kernel/kptr_restrict". */
/******************************************************************************/


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


/*! Thread context. */
__thread struct thread thread = { .is_init=0 };

/*! Process context. */
struct process process;


/*! Fiber log. */
static FILE * S_log;


static int
S_is_runnable(int const id)
{
#if defined(USE_AIO) && USE_AIO > 0
  int ret;
  unsigned char incore=0;
  void * page;

  /* Check if there is a request and if it has completed. */
  if (-1 == (ret=aio_error(&(F_aioreq(id))))) {
    assert(EINVAL == errno);

    /* Page align the offending address. */
    page = (void*)((uintptr_t)F_addr(id)&(~(T_ps-1)));

    /* Check if the page is resident. */
    ret = mincore(page, T_ps, &incore);
    assert(!ret);

    return incore;
  }
  assert(EINPROGRESS == ret || 0 == ret);

  return (0 == ret);
#else
  (void)id;
  return 1;
#endif
}


static void
S_sigsegv_handler(void)
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
  page = (void*)((uintptr_t)F_addr(T_me)&(~(T_ps-1)));

  if (!S_is_runnable(T_me)) {
    /* Post an asynchronous fetch of the page. */
    F_aioreq(T_me).aio_id = T_me;
    ret = aio_read(page, T_ps, &(F_aioreq(T_me)));
    assert(!ret);

    /* Increment count of waiting fibers. */
    T_n_wait++;

    dbg_printf("[%5d.%.3d]     Not runnable, switching to T_main\n",\
      (int)syscall(SYS_gettid), T_me);

    /* Switch back to main context to allow another fiber to execute. */
    ret = swapcontext(&(F_handler(T_me)), &T_main);
    assert(!ret);

    /* Decrement count of waiting fibers. */
    T_n_wait--;

    /* Retrieve and validate the return value for the request. */
    retval = aio_return(&(F_aioreq(T_me)));
    assert((ssize_t)T_ps == retval);
  }
  else {
    dbg_printf("[%5d.%.3d]     Runnable\n", (int)syscall(SYS_gettid), T_me);

    /* Apply updates to page containing offending address. */
    ret = mprotect(page, T_ps, PROT_READ|PROT_WRITE);
    assert(!ret);
  }

  /* Switch back to trampoline context, so that it may return. */
  setcontext(&(F_trampoline(T_me)));

  /* It is erroneous to reach this point. */
  abort();
}


#if 0
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

  /* FIXME There could be a problem here if multiple fibers from the same thread
   * segfault on the same page, since one fiber will have locked the vma, the
   * next fiber will deadlock when it tries to relock the vma. */

  /* Find the vma corresponding to the offending address and lock it. */
  ret = sp_tree_find_and_lock(&vma_tree, F_addr(T_me), (void*)&vma);
  assert(!ret);

  if (!(vma->vm_flags&0x1)) {
    if (vma->vm_flags&0x10) {
      /* TODO Post an async-io request. */
      /*aio_read(...);*/

      if (/* FIXME async-io has not finished */0) {
        ret = swapcontext(&(F_handler(T_me)), &T_main);
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
  addr = (uintptr_t)F_addr(T_me)&(~(T_ps-1)); /* page align */
  ret = mprotect((void*)addr, T_ps, prot);
  assert(!ret);

  /* Unlock the vma. */
  ret = lock_let(&(vma->vm_lock));
  assert(!ret);

  /* Switch back to trampoline context, so that it may return. */
  setcontext(&(F_trampoline(T_me)));

  /* It is erroneous to reach this point. */
  abort();
}
#endif


static void
S_sigsegv_trampoline(int const sig, siginfo_t * const si, void * const uc)
{
  /* Signal handler context. */
  //ucontext_t tmp_uc;
  /* Alternate stack for signal handler context. */
  /*static __thread char tmp_stack[SIGSTKSZ];*/
  int ret;

  assert(SIGSEGV == sig);

  dbg_printf("[%5d.%.3d]   Received SIGSEGV\n", (int)syscall(SYS_gettid), T_me);
  dbg_printf("[%5d.%.3d]     Address = %p\n", (int)syscall(SYS_gettid), T_me,\
    si->si_addr);

  F_addr(T_me) = si->si_addr;

  memset(&(F_tmp_uc(T_me)), 0, sizeof(F_tmp_uc(T_me)));
  ret = getcontext(&(F_tmp_uc(T_me)));
  assert(!ret);
  F_tmp_uc(T_me).uc_stack.ss_sp = F_tmp_stack(T_me);
  F_tmp_uc(T_me).uc_stack.ss_size = SIGSTKSZ;
  F_tmp_uc(T_me).uc_stack.ss_flags = 0;
  memcpy(&(F_tmp_uc(T_me).uc_sigmask), &(T_main.uc_sigmask),\
    sizeof(T_main.uc_sigmask));

  makecontext(&(F_tmp_uc(T_me)), (void (*)(void))S_sigsegv_handler, 0);

  dbg_printf("[%5d.%.3d]     Switching to S_sigsegv_handler1\n",\
    (int)syscall(SYS_gettid), T_me);

  swapcontext(&(F_trampoline(T_me)), &(F_tmp_uc(T_me)));

  if (uc) {}
}


static void
S_flush(void)
{
}


static void
S_kernel_trampoline(int const i)
{
  F_kernel(i)(F_iter(i), F_args(i));

  /* Before this context returns, it should call a `flush function` where the
   * data it accessed is flushed to disk. */
  S_flush();

  /* Put myself on idle list. */
  T_idle_list[T_n_idle++] = i;

  /* Switch back to main context, so that a new fiber gets scheduled. */
  setcontext(&T_main);

  /* It is erroneous to reach this point. */
  abort();
}


/* Moved this into its own function to prevent the following gcc error:
 * variable ‘i’ might be clobbered by ‘longjmp’ or ‘vfork’ [-Werror=clobbered]
 */
static int
S_kern_init(int const i)
{
  int ret;

  ret = getcontext(&(F_kern(i)));
  assert(!ret);
  F_kern(i).uc_stack.ss_sp = F_stack(i);
  F_kern(i).uc_stack.ss_size = SIGSTKSZ;
  F_kern(i).uc_stack.ss_flags = 0;

  makecontext(&(F_kern(i)), (void (*)(void))&S_kernel_trampoline, 1, i);

  return 0;
}


static int
S_init(void)
{
  int ret, i;
  struct sigaction act;
  //char * lname;

  /* Clear thread context. */
  memset(&thread, 0, sizeof(struct thread));

  /* Set per-thread system page size. */
  T_ps = (uintptr_t)OOC_PAGE_SIZE;

  /* Set per-thread fiber count. */
  T_num_fibers = OOC_MAX_FIBERS;

  /* Set per-thread SIGSEGV handler. */
  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &S_sigsegv_trampoline;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, &T_old_act);
  assert(!ret);

  /* Open fiber log file. */
  //lname = malloc(FILENAME_MAX);
  //sprintf(lname, "f-%d", (int)syscall(SYS_gettid));
  //S_log = fopen(lname, "w");
  //assert(S_log);
  //free(lname);

  /* Allocate memory for idle list. */
  T_idle_list = malloc(OOC_MAX_FIBERS*sizeof(*T_idle_list));
  assert(T_idle_list);

  /* Allocate memory for fibers. */
  T_fiber = malloc(OOC_MAX_FIBERS*sizeof(*T_fiber));
  assert(T_fiber);

  /* Setup per-thread fibers. */
  for (i=0; i<(int)T_num_fibers; ++i) {
    /* Clear fiber context. */
    memset(&(T_fiber[i]), 0, sizeof(struct fiber));

#if 0 && defined(MAP_STACK)
    F_stack(i) = mmap(NULL, SIGSTKSZ, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    assert(MAP_FAILED != F_stack(i));
    F_tmp_stack(i) = mmap(NULL, SIGSTKSZ, PROT_READ|PROT_WRITE,\
      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    assert(MAP_FAILED != F_tmp_stack(i));
#endif

    /* Initialize fiber kernel. */
    ret = S_kern_init(i);
    assert(!ret);

    /* Add fiber to idle list. */
    T_idle_list[T_n_idle++] = i;
  }

  /* Setup per-thread async-io context. */
  ret = aio_setup(T_num_fibers, &T_aioctx);
  assert(!ret);

  /* Mark thread as initialized. */
  T_is_init = 1;

  dbg_printf("[%5d.***] Library initialized -- pagesize=%lu\n",\
    (int)syscall(SYS_gettid), (long unsigned)T_ps);

  return ret;
}


int
ooc_finalize(void)
{
  int ret;

  /* Remove per-thread SIGSEGV handler. */
  ret = sigaction(SIGSEGV, &T_old_act, NULL);

  /* Destroy per-thread async-io context. */
  ret = aio_destroy(T_aioctx);
  assert(!ret);

  /* Free memory for idle list. */
  free(T_idle_list);

  /* Free memory for fibers. */
  free(T_fiber);

#if 0 && defined(MAP_STACK)
  for (i=0; i<(int)T_num_fibers; ++i) {
    ret = munmap(F_stack(i), SIGSTKSZ);
    assert(!ret);
    ret = munmap(F_tmp_stack(i), SIGSTKSZ);
    assert(!ret);
  }
#endif

  /* Close fiber log. */
  //ret = fclose(S_log);
  //assert(!ret);

  /* Mark thread as uninitialized. */
  T_is_init = 0;

  return ret;
}


void
ooc_wait(void)
{
  int ret, wait;
  aioreq_t * aioreq;

  dbg_printf("[%5d.***] Waiting for outstanding %d fibers\n",\
    (int)syscall(SYS_gettid), T_n_wait);

  while (T_n_wait) {
    /* Wait for a fiber to become runnable. Since we are in the `main`
     * context, all fibers must be blocked on async-io, thus no fiber will
     * become idle, so we just wait on async-io. */
    aioreq = aio_suspend();
    assert(aioreq);

    /* Get fiber id. */
    wait = aioreq->aio_id;

    if (-1 != wait) {
      dbg_printf("[%5d.%.3d]   Runnable, switching to F_handler\n",\
        (int)syscall(SYS_gettid), wait);

      /* Switch fibers. */
      T_me = wait;
      ret = swapcontext(&T_main, &(F_handler(T_me)));
      assert(!ret);
    }
    else {
      /* Erroneous. */
      assert(0);
    }
  }
}


void
ooc_sched(void (*kern)(size_t const, void * const), size_t const i,
          void * const args)
{
  int ret, wait, idle;
  aioreq_t * aioreq;

  /* Make sure that library has been initialized. */
  if (!T_is_init) {
    ret = S_init();
    assert(!ret);
  }

  dbg_printf("[%5d.***] Scheduling a new fiber\n", (int)syscall(SYS_gettid));
  dbg_printf("[%5d.***]   %d fibers idle\n", (int)syscall(SYS_gettid),\
    T_n_idle);
  dbg_printf("[%5d.***]   %d fibers waiting\n", (int)syscall(SYS_gettid),\
    T_n_wait);

  for (;;) {
    idle = wait = -1;

    /* Check idle list for an available fiber. */
    if (T_n_idle) {
      /* Remove from idle list. */
      idle = T_idle_list[--T_n_idle];
    }
    /* Wait for a fiber to become runnable. */
    else {
      /* Since we are in the `main` context and no fibers are idle, all fibers
       * must be blocked on async-io, thus we just wait on async-io. */
      aioreq = aio_suspend();
      assert(aioreq);

      /* Get fiber id. */
      wait = aioreq->aio_id;
    }

    if (-1 != idle) {
      /* Setup fiber environment. */
      F_iter(idle) = i;
      F_kernel(idle) = kern;
      F_args(idle) = args;

      dbg_printf("[%5d.%.3d]   Switching to F_kern for iter %zu\n",\
        (int)syscall(SYS_gettid), idle, i);

      //fprintf(S_log, "s %d %f\n", idle, omp_get_wtime());

      /* Switch fibers. */
      T_me = idle;
      ret = swapcontext(&T_main, &(F_kern(T_me)));
      assert(!ret);

      //fprintf(S_log, "e %d %f\n", idle, omp_get_wtime());

      dbg_printf("[%5d.%.3d]   Returned from F_kern for iter %zu\n",\
        (int)syscall(SYS_gettid), idle, i);

      /* XXX At this point the fiber T_me has either successfully completed its
       * execution of the kernel F_kern(T_me) or it received a SIGSEGV and
       * populated the signal handler context F_handler(T_me), so that it can
       * resume execution from the point it received SIGSEGV at a later time. */

      /* XXX This is the only place we can safely break from this loop, since
       * this is the only point that we know that iteration i has been scheduled
       * to a fiber. */
      break;
    }
    else if (-1 != wait) {
      //fprintf(S_log, "s %d %f\n", wait, omp_get_wtime());

      /* Switch fibers. */
      T_me = wait;
      ret = swapcontext(&T_main, &(F_handler(T_me)));
      assert(!ret);

      //fprintf(S_log, "e %d %f\n", wait, omp_get_wtime());

      /* XXX At this point the fiber T_me has either successfully completed its
       * execution of the kernel F_kern(T_me) or it received another SIGSEGV and
       * repopulated the signal handler context F_handler(T_me), so that it can
       * resume execution from the point it received the latest SIGSEGV at a
       * later time. */
    }
    else {
      /* Erroneous. */
      assert(0);
    }
  }
}


void
ooc_set_num_fibers(unsigned int const num_fibers)
{
  int ret, i;

  /* Make sure that library has been initialized. */
  if (!T_is_init) {
    ret = S_init();
    assert(!ret);
  }

  T_num_fibers = num_fibers;

  /* Setup per-thread fibers. */
  T_n_idle = 0;
  for (i=0; i<(int)T_num_fibers; ++i) {
    /* Add fiber to idle list. */
    T_idle_list[T_n_idle++] = i;
  }

  dbg_printf("[%5d.***] Set number of fibers to %u / %d\n",\
    (int)syscall(SYS_gettid), T_num_fibers, T_n_idle);
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

void kern(size_t const i, void * const state);
void kern(size_t const i, void * const state)
{
  char var;
  char * mem=(char*)state;

  mem[0] = 'a'; /* Raise a SIGSEGV. */
  assert(&(mem[0]) == (void*)F_addr(T_me));

  mem[1] = 'b'; /* Should not raise SIGSEGV. */
  assert(&(mem[0]) == (void*)F_addr(T_me));

  var = mem[1]; /* Should not raise SIGSEGV. */
  assert(var = 'b');

  if (i) {}
}

int
main(void)
{
  int ret;
  char * mem;

  T_ps = (size_t)sysconf(_SC_PAGESIZE);
  assert((size_t)-1 != T_ps);

  mem = mmap(NULL, T_ps, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(MAP_FAILED != mem);

  ooc_sched(&kern, 0, mem);

  ooc_wait();
  ret = ooc_finalize();
  assert(!ret);

  ret = munmap(mem, T_ps);
  assert(!ret);

  return EXIT_SUCCESS;
}
#endif
