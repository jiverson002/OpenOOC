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

/* __WORDSIZE */
#include <limits.h>

/* jmp_buf, longjmp */
#include <setjmp.h>

/* memcpy */
#include <string.h>

/* mprotect, PROT_READ, PROT_WRITE */
#include <sys/mman.h>

/* ucontext_t */
#include <ucontext.h>

/* sysconf, _SC_PAGESIZE */
#include <unistd.h>


/* Function prototype */
void
ooc_sigsegv(int const _sig, siginfo_t * const _si, void * const _uc);


/* The stack environment to return to via longjmp. */
__thread jmp_buf ooc_ret_env;

/* The thread context at the point SIGSEGV was raised. */
__thread ucontext_t ooc_ret_uc;

/* Memory location which caused fault */
static __thread void * segv_addr;


/*******************************************************************************
 * Function to post asynchronous I/O requests.
 ******************************************************************************/
static void
ooc_async(void)
{
  /* TEMPORARY: Update memory protection of offending system page. */
  {
    size_t pgsize = (size_t)sysconf(_SC_PAGESIZE);
    uintptr_t addr = (uintptr_t)segv_addr & ~((uintptr_t)pgsize-1);
    int ret = mprotect((void*)addr, pgsize, PROT_READ|PROT_WRITE);
    assert(!ret);
  }

  /* Jump back to the stack environment which resulted in this function being
   * called. */
  longjmp(ooc_ret_env, 1);
}


/*******************************************************************************
 *  SIGSEGV handler.
 ******************************************************************************/
void
ooc_sigsegv(int const _sig, siginfo_t * const _si, void * const _uc)
{
  assert(SIGSEGV == _sig);

  /* Save the memory location which caused the fault. */
  segv_addr = _si->si_addr;

  /* Save the thread context at the point SIGSEGV was raised. */
  memcpy(&ooc_ret_uc, _uc, sizeof(ooc_ret_uc));

  /* Hijack instruction pointer and point it towards a new address that the
   * kernel signal handling mechanism should return to instead of the
   * instruction which originally generated the signal. */
#if 64 == __WORDSIZE
  ((ucontext_t*)_uc)->uc_mcontext.gregs[REG_RIP] = (greg_t)&ooc_async;
#else
  ((ucontext_t*)_uc)->uc_mcontext.gregs[REG_EIP] = (greg_t)&ooc_async;
#endif
}


#ifdef TEST
/* assert */
#include <assert.h>

/* setjmp */
#include <setjmp.h>

/* sigaction */
#include <signal.h>

/* NULL, EXIT_SUCCESS, EXIT_FAILURE */
#include <stdlib.h>

/* memset */
#include <string.h>

/* mmap, munmap, PROT_NONE, MAP_PRIVATE, MAP_ANONYMOUS */
#include <sys/mman.h>

#define SEGV_ADDR (void*)(0x1)

int
main(int argc, char * argv[])
{
  int ret;
  struct sigaction act;
  char * mem;

  memset(&act, 0, sizeof(act));
  act.sa_sigaction = &ooc_sigsegv;
  act.sa_flags = SA_SIGINFO;
  ret = sigaction(SIGSEGV, &act, NULL);
  assert(!ret);

  mem = mmap(NULL, 1<<16, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(mem);

  ret = setjmp(ooc_ret_env);
  if (!ret) {
    mem[0] = (char)1; /* Raise a SIGSEGV. */
    return EXIT_FAILURE;
  }

  assert(1 == ret);
  assert(mem == segv_addr);

  mem[1] = (char)1; /* Should not raise SIGSEGV. */

  ret = munmap(mem, 1<<16);
  assert(!ret);

  return EXIT_SUCCESS;

  if (argc || argv) {}
}
#endif
